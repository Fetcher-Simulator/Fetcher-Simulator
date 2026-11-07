#ifndef OPENMW_MWSOUND_PROCEDURALSAMPLE_H
#define OPENMW_MWSOUND_PROCEDURALSAMPLE_H

#include <atomic>
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace MWSound
{
    // Published once by the loader. Ready/Error use release/acquire; the PCM,
    // fingerprint and diagnostic are immutable after publication.
    struct ProceduralSample
    {
        struct Zone
        {
            unsigned char lowKey, highKey, lowVelocity, highVelocity, root, alternate, alternates;
            std::uint32_t start, length;
            unsigned char flags = 0, group = 0, offBy = 0;
            float releaseDecay = 4; // dB per held second, release-zone Q8 metadata
        };
        enum Status { Loading, Ready, Error };
        std::atomic<Status> status{Loading};
        std::vector<float> pcm; // mono 48 kHz; clips <=30 s, banks <=128 MiB
        std::vector<Zone> zones; // nonempty only for immutable multisample banks
        bool hasReleases = false; // v2 flag 2: recorded note-off zones
        bool hasHammer = false; // v2 flag 4: recorded upward connected attacks
        std::array<float,256> waveMin{},waveMax{};
        bool waveformReady=false;
        float sourcePeak=0; // Raw source peak, published with Ready; normalization is control-thread only.
        struct View
        {
            std::array<float,256> minimum{},maximum{};
            double first=0,finish=0;
        };
        std::optional<View> view(double first,double finish,bool reverse)const
        {
            if(pcm.size()<2 || !zones.empty() || !std::isfinite(first) || !std::isfinite(finish)
                || first<0 || finish<=first || finish>pcm.size()/48000.0+1e-9)return {};
            const auto a=std::min(pcm.size()-1,std::size_t(first*48000));
            const auto b=std::min(pcm.size(),std::size_t(std::ceil(finish*48000)));
            View out;out.first=first;out.finish=finish;
            for(std::size_t n=0;n<256;++n)
            {
                const auto low=a+n*(b-a)/256,high=std::max(low+1,a+(n+1)*(b-a)/256);
                float mn=1,mx=-1;
                for(auto i=low;i<high;++i){const auto value=pcm[reverse?pcm.size()-1-i:i];mn=std::min(mn,value);mx=std::max(mx,value);}
                out.minimum[n]=mn;out.maximum[n]=mx;
            }
            return out;
        }
        std::optional<double> zeroCrossing(double position,bool reverse)const
        {
            if(pcm.size()<2 || !zones.empty() || !std::isfinite(position) || position<0 || position>pcm.size()/48000.0)return {};
            const auto center=static_cast<std::size_t>(position*48000);
            const auto first=center>240?center-240:0,last=std::min(pcm.size()-2,center+240);
            double nearest=0,distance=1e9;
            for(auto n=first;n<=last;++n)
            {
                const double a=pcm[reverse?pcm.size()-1-n:n],b=pcm[reverse?pcm.size()-2-n:n+1];
                if(a==0 || (a<0 && b>=0) || (a>0 && b<=0))
                {
                    const double at=n+(a==0?0:-a/(b-a)),delta=std::abs(at-position*48000);
                    if(delta<distance){distance=delta;nearest=at/48000;}
                }
            }
            return distance<1e9?std::optional<double>(nearest):std::nullopt;
        }
        struct Transients { std::vector<double> points;std::size_t found=0; };
        // Explicit editor request on the control thread, never the audio callback.
        // Two-ms energy bins keep a 30-second excerpt bounded to 15000 bins.
        std::optional<Transients> transients(double sensitivity,double spacing,bool reverse,bool snap)const
        {
            if(pcm.size()<480 || pcm.size()>1440000 || !zones.empty() || !std::isfinite(sensitivity)
                || !std::isfinite(spacing) || sensitivity<0 || sensitivity>1 || spacing<.01 || spacing>2)return {};
            constexpr std::size_t hop=96;
            const auto bins=(pcm.size()+hop-1)/hop;
            std::vector<double> energy(bins),fast(bins);
            double window=0,peak=0;
            for(std::size_t n=0;n<bins;++n)
            {
                const auto first=n*hop,last=std::min(pcm.size(),first+hop);double sum=0;
                for(auto i=first;i<last;++i){const double value=pcm[reverse?pcm.size()-1-i:i];sum+=value*value;}
                energy[n]=sum/(last-first);window+=energy[n];if(n>=5)window-=energy[n-5];
                fast[n]=std::sqrt(std::max(0.0,window/std::min<std::size_t>(n+1,5)));peak=std::max(peak,fast[n]);
            }
            Transients out;if(peak<1e-7)return out;
            std::vector<double> novelty(bins);double envelope=0;
            const double ratio=1.65-.6*sensitivity,floor=peak*(.075-.06*sensitivity),decay=std::exp(-.002/.06);
            for(std::size_t n=0;n<bins;++n)
            {
                const double change=fast[n]-envelope*ratio;
                if(change>floor)novelty[n]=change/peak;
                envelope=std::max(fast[n],envelope*decay);
            }
            struct Candidate { double at,score; };std::vector<Candidate> candidates;
            const double duration=std::floor(pcm.size()/48.0)/1000;
            for(std::size_t n=0;n<bins;++n)if(novelty[n]>0)
            {
                bool maximum=true;
                for(auto j=n>5?n-5:0;j<std::min(bins,n+6);++j)
                    if(novelty[j]>novelty[n] || (j<n && novelty[j]==novelty[n])){maximum=false;break;}
                if(!maximum)continue;
                double at=(n>4?n-4:0)*.002;
                if(snap)if(auto crossing=zeroCrossing(at,reverse))at=*crossing;
                at=std::floor(at*1000+.5)/1000;
                if(at>=.01 && at<=duration-.01)candidates.push_back({at,novelty[n]});
            }
            std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.score==b.score?a.at<b.at:a.score>b.score;});
            std::vector<double> accepted;
            for(const auto& candidate:candidates)
            {
                bool near=false;for(double at:accepted)if(std::abs(at-candidate.at)<spacing-1e-9){near=true;break;}
                if(!near)accepted.push_back(candidate.at);
            }
            out.found=accepted.size();accepted.resize(std::min<std::size_t>(15,accepted.size()));
            std::sort(accepted.begin(),accepted.end());out.points=std::move(accepted);return out;
        }
        void buildWaveform() // loader thread only, raw excerpts only
        {
            if(pcm.empty())return;
            sourcePeak=0;for(float value:pcm)sourcePeak=std::max(sourcePeak,std::abs(value));
            for(std::size_t n=0;n<waveMin.size();++n)
            {
                const auto first=n*pcm.size()/waveMin.size();
                const auto last=std::max(first+1,(n+1)*pcm.size()/waveMin.size());
                const auto bounds=std::minmax_element(pcm.begin()+first,pcm.begin()+last);
                waveMin[n]=*bounds.first;waveMax[n]=*bounds.second;
            }
            waveformReady=true;
        }
        std::string hash, error;
    };
}
#endif
