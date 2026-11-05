#ifndef OPENMW_MWSOUND_PROCEDURALSAMPLE_H
#define OPENMW_MWSOUND_PROCEDURALSAMPLE_H

#include <atomic>
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <cstdint>
#include <string>
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
        void buildWaveform() // loader thread only, raw excerpts only
        {
            if(pcm.empty())return;
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
