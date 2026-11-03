#ifndef OPENMW_MWSOUND_PROCEDURALMIXER_H
#define OPENMW_MWSOUND_PROCEDURALMIXER_H

#include <algorithm>
#include <array>
#include <cmath>

namespace MWSound
{
    // A mono channel strip. State is copied by Studio's control mailbox; all
    // filter/detector history belongs exclusively to the audio thread.
    class ProceduralMixer
    {
    public:
        struct Parameter { const char* name; float min, max, initial; };
        inline static constexpr std::array<Parameter, 14> Parameters{{
            {"eqEnabled",0,1,0}, {"highpass",20,1000,20},
            {"bass",-18,18,0}, {"mid",-18,18,0}, {"midFrequency",200,6000,1000}, {"treble",-18,18,0},
            {"compEnabled",0,1,0}, {"threshold",-60,0,-18}, {"ratio",1,20,4},
            {"attack",0.1f,200,10}, {"release",10,2000,150}, {"makeup",0,24,0},
            {"delaySend",0,1,0}, {"reverbSend",0,1,0}
        }};
        using State = std::array<float, Parameters.size()>;
        static State defaults() { State s{}; for (unsigned i=0;i<s.size();++i) s[i]=Parameters[i].initial; return s; }
        static bool valid(const State& s)
        {
            for (unsigned i=0;i<s.size();++i)
                if (!std::isfinite(s[i]) || s[i]<Parameters[i].min || s[i]>Parameters[i].max
                    || ((i==0 || i==6) && s[i]!=0 && s[i]!=1)) return false;
            return true;
        }
        void target(const State& state, float level, bool audible = true)
        {
            // A live fader move takes over its running curve until Stop/seek/
            // loop/replacement chase. Other channel edits do not disturb it.
            for (unsigned i=0;i<AutomationCount;++i)
            {
                const double value=i==0 ? level : state[i+11];
                const double old=i==0 ? mLevelTarget : mTarget[i+11];
                if (mAutomated[i] && value!=old) { mOverride[i]=true; mAutomation[i]=value; mSlope[i]=0; }
            }
            mTarget=state; mLevelTarget=level; mAudibleTarget=audible ? 1 : 0;
            if (!mInitialized) { mLevel=level; mAudible=mAudibleTarget; mInitialized=true; }
        }
        static constexpr unsigned AutomationCount = 3; // absolute fader, delay send, reverb send
        void automate(unsigned code, double value, double slope, bool chase)
        {
            if (mOverride[code] && !chase) return;
            mAutomated[code]=true; mAutomation[code]=value; mSlope[code]=slope;
            if (chase) { if (code==0) mLevel=value; else mState[code+11]=value; }
        }
        void resetAutomation() { mAutomated.fill(false); mOverride.fill(false); }
        float process(float input)
        {
            // Continuous controls and bypass wet/dry ramps have a 10 ms time constant.
            constexpr double smoothing=0.0020811647;
            for (unsigned i=0;i<mState.size();++i)
            {
                const double target=i>=12 && mAutomated[i-11] ? mAutomation[i-11] : mTarget[i];
                mState[i]+=(target-mState[i])*smoothing;
                if (std::abs(target-mState[i])<1e-12) mState[i]=target;
            }
            const double level=mAutomated[0] ? mAutomation[0] : mLevelTarget;
            mLevel+=(level-mLevel)*smoothing;
            if (std::abs(level-mLevel)<1e-12) mLevel=level;
            mAudible+=(mAudibleTarget-mAudible)*smoothing;
            if (std::abs(mAudibleTarget-mAudible)<1e-12) mAudible=mAudibleTarget;
            for (unsigned i=0;i<AutomationCount;++i) if (mAutomated[i]) mAutomation[i]+=mSlope[i];
            if (!(mCounter++ % 32)) coefficients();
            double eq=input;
            for (auto& filter:mFilters) eq=filter.process(eq);
            double value=input+(eq-input)*mState[0];
            const double magnitude=std::abs(value);
            const double coefficient=magnitude>mEnvelope ? mAttack : mRelease;
            mEnvelope=coefficient*mEnvelope+(1-coefficient)*magnitude;
            if (mEnvelope<1e-25) mEnvelope=0;
            const double over=20*std::log10(std::max(1e-12,mEnvelope))-mState[7];
            // 6 dB soft knee, feed-forward peak detector; no look-ahead latency.
            const double slope=1-1/mState[8];
            const double reduction=over<=-3 ? 0 : (over>=3 ? slope*over : slope*(over+3)*(over+3)/12);
            mReduction=static_cast<float>(reduction*mState[6]);
            value*=std::pow(10.0, (mState[11]-reduction)*mState[6]/20)*mLevel*mAudible;
            return static_cast<float>(value);
        }
        float delaySend() const {return static_cast<float>(mState[12]);}
        float reverbSend() const {return static_cast<float>(mState[13]);}
        float reduction() const { return mReduction; }
    private:
        struct Biquad
        {
            double b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
            void set(double B0,double B1,double B2,double A0,double A1,double A2)
            { b0=B0/A0;b1=B1/A0;b2=B2/A0;a1=A1/A0;a2=A2/A0; }
            double process(double x)
            {
                const double y=b0*x+z1; z1=b1*x-a1*y+z2;z2=b2*x-a2*y;
                if (std::abs(z1)<1e-25) z1=0;
                if (std::abs(z2)<1e-25) z2=0;
                return y;
            }
        };
        void coefficients()
        {
            constexpr double pi=3.14159265358979323846;
            const double w=2*pi*mState[1]/48000,c=std::cos(w),a=std::sin(w)/std::sqrt(2.0);
            mFilters[0].set((1+c)/2,-(1+c),(1+c)/2,1+a,-2*c,1-a);
            for (unsigned band=0;band<3;++band)
            {
                const double frequency=band==0?120:(band==1?mState[4]:6000);
                const double gain=mState[band==0?2:(band==1?3:5)];
                const double A=std::pow(10.0,gain/40),s=std::sin(2*pi*frequency/48000),C=std::cos(2*pi*frequency/48000);
                auto& f=mFilters[band+1];
                if (band==1) { const double alpha=s/(2*0.7);f.set(1+alpha*A,-2*C,1-alpha*A,1+alpha/A,-2*C,1-alpha/A); }
                else
                {
                    const double beta=std::sqrt(2*A)*s;
                    if (band==0) f.set(A*((A+1)-(A-1)*C+beta),2*A*((A-1)-(A+1)*C),A*((A+1)-(A-1)*C-beta),
                        (A+1)+(A-1)*C+beta,-2*((A-1)+(A+1)*C),(A+1)+(A-1)*C-beta);
                    else f.set(A*((A+1)+(A-1)*C+beta),-2*A*((A-1)+(A+1)*C),A*((A+1)+(A-1)*C-beta),
                        (A+1)-(A-1)*C+beta,2*((A-1)-(A+1)*C),(A+1)-(A-1)*C-beta);
                }
            }
            mAttack=std::exp(-1/(48*mState[9]));mRelease=std::exp(-1/(48*mState[10]));
        }
        std::array<bool,AutomationCount> mAutomated{},mOverride{};
        std::array<double,AutomationCount> mAutomation{},mSlope{};
        double mAudible=1,mAudibleTarget=1;
        State mTarget=defaults();
        std::array<double,14> mState{{0,20,0,0,1000,0,0,-18,4,10,150,0,0,0}};
        std::array<Biquad,4> mFilters{};
        double mEnvelope=0,mAttack=0.99,mRelease=0.999,mLevel=1,mLevelTarget=1;
        unsigned mCounter=0;
        float mReduction=0;
        bool mInitialized=false;
    };
}
#endif
