#ifndef OPENMW_MWSOUND_PROCEDURALRELEASE_H
#define OPENMW_MWSOUND_PROCEDURALRELEASE_H
#include "proceduralsample.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace MWSound
{
    // Recorded physical note endings, separate from held-note polyphony. Eight
    // bounded tails never steal a held string. Bank publication/reset owns the
    // immutable PCM lifetime; this object does not allocate or load anything.
    class ProceduralRelease
    {
        struct Binding
        {
            ProceduralSample* data=nullptr;unsigned key=0;int note=0,group=-1;
            float velocity=0,gain=1;std::uint64_t frame=0,order=0;
        };
        struct Tail
        {
            ProceduralSample* data=nullptr;unsigned start=0,length=0;
            double position=0,pitch=0,gain=0;std::uint64_t order=0;
        };
        std::array<Binding,32> mHeld{};
        std::array<Tail,8> mTails{};
        std::array<unsigned,512> mAlternates{};
        std::uint64_t mFrame=0,mOrder=0;
        unsigned mSeed=0;
    public:
        explicit ProceduralRelease(unsigned seed=0):mSeed(seed){}
        void clear(){mHeld.fill({});mTails.fill({});mAlternates.fill(0);mFrame=mOrder=0;}
        void forget(unsigned key){for(auto& b:mHeld)if(b.data && b.key==key)b={};}
        void remember(unsigned key,int note,int group,float velocity,ProceduralSample* data,float gain)
        {
            forget(key);
            for(auto& b:mHeld)if(group>=0 && b.data && b.group==group)b={};
            if(!data || !data->hasReleases)return;
            auto* b=&mHeld[0];
            for(auto& candidate:mHeld)
            {
                if(!candidate.data){b=&candidate;break;}
                if(candidate.order<b->order)b=&candidate;
            }
            *b={data,key,note,group,velocity,gain,mFrame,++mOrder};
        }
        void retarget(unsigned oldKey,unsigned newKey,int note)
        {for(auto& b:mHeld)if(b.data && b.key==oldKey){b.key=newKey;b.note=note;return;}}
        void off(unsigned key,double level,double slide=0)
        {
            for(auto& b:mHeld)if(b.data && b.key==key)
            {
                const auto binding=b;b={};
                if(level<=0 || binding.data->status.load(std::memory_order_acquire)!=ProceduralSample::Ready)return;
                const auto& zones=binding.data->zones;
                const auto velocity=std::clamp(int(std::lround(binding.velocity*127)),1,127);
                for(unsigned i=0;i<zones.size();++i)
                {
                    const auto& z=zones[i];
                    if(!(z.flags&2) || binding.note<z.lowKey || binding.note>z.highKey
                        || velocity<z.lowVelocity || velocity>z.highVelocity)continue;
                    const auto alternate=(mAlternates[i]++ + mSeed)%z.alternates;
                    for(const auto& selected:zones)if((selected.flags&2) && selected.lowKey==z.lowKey
                        && selected.highKey==z.highKey && selected.lowVelocity==z.lowVelocity
                        && selected.highVelocity==z.highVelocity && selected.alternate==alternate)
                    {
                        auto* tail=&mTails[0];
                        for(auto& candidate:mTails)
                        {
                            if(!candidate.data){tail=&candidate;break;}
                            if(candidate.order<tail->order)tail=&candidate;
                        }
                        // The source's release map supplies held-time decay.
                        const double held=std::min(30.0,double(mFrame-binding.frame)/48000);
                        *tail={binding.data,selected.start,selected.length,0,binding.note-selected.root+slide,
                            level*binding.velocity*binding.gain*std::pow(10.0,-selected.releaseDecay*held/20),++mOrder};
                        return;
                    }
                    return;
                }
                return;
            }
        }
        double next(double pitch)
        {
            ++mFrame;double mixed=0;
            for(auto& t:mTails)if(t.data)
            {
                const auto at=static_cast<unsigned>(t.position);
                if(at+1>=t.length){t={};continue;}
                const auto& pcm=t.data->pcm;const double fraction=t.position-at;
                const double sample=pcm[t.start+at]*(1-fraction)+pcm[t.start+at+1]*fraction;
                mixed+=sample*t.gain*std::min(1.0,t.position/144);
                t.position+=std::exp2((t.pitch+pitch)/12);
            }
            return mixed;
        }
        unsigned voices()const
        {unsigned count=0;for(const auto& t:mTails)if(t.data)++count;return count;}
    };
}
#endif
