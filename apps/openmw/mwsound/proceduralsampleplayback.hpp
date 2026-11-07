#ifndef OPENMW_MWSOUND_PROCEDURALSAMPLEPLAYBACK_H
#define OPENMW_MWSOUND_PROCEDURALSAMPLEPLAYBACK_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace MWSound
{
    // Non-destructive settings in playback-relative seconds. A reverse view
    // changes reads, never the immutable shared PCM or its file identity.
    struct ProceduralSamplePlayback
    {
        bool reverse=false;
        double loopStart=0,loopEnd=0,crossfade=0;
        double fadeIn=0,fadeOut=0;
        bool loop()const {return loopEnd>0;}
        bool edited()const {return reverse || loop() || fadeIn>0 || fadeOut>0;}
        bool operator==(const ProceduralSamplePlayback& other)const
        {return reverse==other.reverse && loopStart==other.loopStart && loopEnd==other.loopEnd && crossfade==other.crossfade && fadeIn==other.fadeIn && fadeOut==other.fadeOut;}
        bool valid(bool oneShot=false)const
        {
            return std::isfinite(fadeIn) && std::isfinite(fadeOut) && fadeIn>=0 && fadeOut>=0 && fadeIn+fadeOut<=30
                && std::isfinite(loopStart) && std::isfinite(loopEnd) && std::isfinite(crossfade)
                && (loopEnd==0 ? loopStart==0 && crossfade==0
                    : !oneShot && loopStart>=0 && loopEnd<=30 && loopEnd-loopStart>=.01-1e-9
                        && crossfade>=0 && crossfade<=.25 && crossfade<=(loopEnd-loopStart)/2+1e-9);
        }
        bool fits(std::size_t frames)const
        {return fadeIn*48000+fadeOut*48000<=frames+1e-6 && (!loop() || loopEnd*48000<=frames+1e-6);}

        double read(const std::vector<float>& pcm,unsigned start,unsigned length,
            double& position,double step,bool& wrapped)const
        {
            const double end=loop()?std::min(loopEnd*48000,double(length-1)):double(length-1);
            const double first=loopStart*48000,blend=crossfade*48000;
            if(loop() && position>=end)
            {
                const double period=end-first-blend;
                position=first+blend+std::fmod(position-end,period);wrapped=true;
            }
            const auto at=static_cast<std::size_t>(position);
            const auto sample=[&](double where) {
                const auto a=static_cast<unsigned>(where);const double f=where-a;
                return reverse ? pcm[start+length-1-a]*(1-f)+pcm[start+length-2-a]*f
                    : pcm[start+a]*(1-f)+pcm[start+a+1]*f;
            };
            if(at+1>=length)return 0;
            double result=sample(position);
            if(loop() && blend>0 && position>=end-blend)
            {
                const double distance=position-(end-blend),mix=distance/blend;
                result=result*(1-mix)+sample(first+distance)*mix;
            }
            const double fade=loop()? (wrapped?1:std::min(1.0,position/96))
                :std::min({1.0,position/96,(length-1-position)/std::max(96.0,96*step)});
            // A sustain loop uses its note-off envelope for ending; do not fade
            // every wrap. The source fade-in applies only to the initial pass.
            const double in=fadeIn>0 && !(loop() && wrapped)?std::min(1.0,position/(fadeIn*48000)):1;
            const double out=fadeOut>0 && !loop()?std::min(1.0,(length-1-position)/(fadeOut*48000)):1;
            return result*fade*in*out;
        }
    };
}
#endif
