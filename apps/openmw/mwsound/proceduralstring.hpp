#ifndef OPENMW_MWSOUND_PROCEDURALSTRING_H
#define OPENMW_MWSOUND_PROCEDURALSTRING_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace MWSound
{
    // A finite slide in semitone space. Retargeting starts at the currently
    // sounding pitch; the duration is latched at the gesture, not at each block.
    class ProceduralPitchSlide
    {
    public:
        void start(double offset,double seconds)
        {
            mFrames=offset!=0?static_cast<unsigned>(std::round(seconds*48000)):0;
            mOffset=mFrames?offset:0;mStep=mFrames?-offset/mFrames:0;
        }
        double offset() const {return mOffset;}
        void next() {if(mFrames && !--mFrames)mOffset=0;else if(mFrames)mOffset+=mStep;}
    private:
        double mOffset=0,mStep=0;
        unsigned mFrames=0;
    };
    // Per-strike expression shared by generated strings and gated melodic banks.
    // No allocation, randomness from a global generator, or timing changes.
    class ProceduralStringExpression
    {
    public:
        struct Settings
        {
            unsigned articulation=0,direction=0;
            double attack=0,attackDecay=.006,resonance=0,pitch=0,variation=0;
            double mute=0,decay=3,damping=.35;
        };
        void trigger(const Settings& settings,double velocity,std::uint64_t ordinal)
        {
            *this=ProceduralStringExpression{};mSettings=settings;mVelocity=velocity;
            mEnabled=settings.articulation || settings.attack>0 || settings.resonance>0
                || settings.pitch!=0 || settings.variation>0 || settings.direction;
            if(!mEnabled)return;
            mUp=settings.direction==1 || (settings.direction==2 && (ordinal&1));
            auto bits=std::uint32_t(ordinal)*747796405u+2891336453u;
            bits^=bits>>16;const double variation=double(bits)/2147483648.0-1;
            mGain=1+settings.variation*.035*variation;
            mAttack=settings.attack*(.3+.7*velocity)*(1+settings.variation*.1*variation);
            mAttackLoss=std::exp(-1/(48000*settings.attackDecay));
            if(settings.articulation==2)
            {
                mMute=std::max(.65,settings.mute);
                const double duration=std::clamp(settings.decay*(1-.9*mMute),.02,.35);
                mMuteLoss=std::exp(-6.907755*mMute/(48000*duration));
            }
            else if(settings.articulation==3){mMute=.98;mMuteLoss=std::exp(-6.907755/(48000*.03));}
        }
        bool enabled() const {return mEnabled;}
        unsigned articulation() const {return mSettings.articulation;}
        double pitch() const {return mEnabled?mSettings.pitch*mPitchEnvelope:0;}
        double pickOffset() const
        {return mEnabled?(mUp?1:-1)*(.03*mSettings.direction+.06*mSettings.variation):0;}
        bool finished() const {return mMute>0 && mMuteEnvelope<1e-5;}
        double process(double source,double noise,double frequency,double resonance=-1)
        {
            resonance=resonance<0?mSettings.resonance:resonance;
            if(!mEnabled && resonance<=0)return source;
            source*=mGain;
            if(resonance>0)
            {
                if(!(mAge&31))
                    for(unsigned n=0;n<2;++n)
                    {
                        const double f=std::clamp(frequency*(n?3.02:2.01)*(1+.003*mPitchEnvelope),40.0,12000.0);
                        const double radius=std::exp(-3.141592653589793*f/(48000*(2+.7*mVelocity)));
                        auto& r=mModes[n];r.gain=1-radius;r.a=2*radius*std::cos(2*3.141592653589793*f/48000);r.b=-radius*radius;
                    }
                double body=0;
                for(unsigned n=0;n<2;++n)
                {
                    auto& r=mModes[n];const double value=r.gain*(source-r.x2)+r.a*r.y1+r.b*r.y2;
                    r.x2=r.x1;r.x1=source;r.y2=r.y1;r.y1=std::abs(value)<1e-20?0:value;
                    body+=value*(n?.2:.3);
                }
                source+=body*resonance;
            }
            if(mMute>0)
            {
                if(!(mAge&31))
                {
                    const double f=std::clamp(frequency*(2.5+3*(1-mSettings.damping)),180.0,10000.0);
                    mPalmCoefficient=1-std::exp(-2*3.141592653589793*f/48000);
                }
                mPalmLow+=(source-mPalmLow)*mPalmCoefficient;
                source+=(mPalmLow-source)*mMute;
            }
            if(mAttack>0)
            {
                mPickHigh+=(noise-mPickHigh)*.07;
                mPickLow+=(noise-mPickHigh-mPickLow)*.6;
                source+=mPickLow*mAttack*mAttackEnvelope*.12*(mUp?-1:1);
            }
            const double result=source*mMuteEnvelope;
            mMuteEnvelope*=mMuteLoss;mAttackEnvelope*=mAttackLoss;mPitchEnvelope*=.998265395;
            ++mAge;return result;
        }
    private:
        struct Mode {double x1=0,x2=0,y1=0,y2=0,gain=0,a=0,b=0;};
        Settings mSettings;
        std::array<Mode,2> mModes{};
        std::uint64_t mAge=0;
        bool mEnabled=false,mUp=false;
        double mGain=1,mAttack=0,mVelocity=0,mMute=0,mMuteLoss=1,mMuteEnvelope=1;
        double mAttackLoss=1,mAttackEnvelope=1,mPitchEnvelope=1,mPickHigh=0,mPickLow=0,mPalmLow=0,mPalmCoefficient=0;
    };
}
#endif
