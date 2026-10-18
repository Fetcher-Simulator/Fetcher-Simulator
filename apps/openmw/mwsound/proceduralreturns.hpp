#ifndef OPENMW_MWSOUND_PROCEDURALRETURNS_H
#define OPENMW_MWSOUND_PROCEDURALRETURNS_H

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace MWSound
{
    // Shared mono send returns. All storage is allocated before rendering.
    class ProceduralReturns
    {
    public:
        struct Parameter { const char* name; float min,max,initial; };
        inline static constexpr std::array<Parameter,8> Parameters{{
            {"delayEnabled",0,1,1},{"delayTime",40,1500,250},{"delayFeedback",0,0.85f,0.35f},{"delayReturn",0,1,0.35f},
            {"reverbEnabled",0,1,1},{"reverbDecay",0.2f,8,1.8f},{"reverbDamping",200,16000,6000},{"reverbReturn",0,1,0.25f}
        }};
        using State=std::array<float,8>;
        static State defaults() {State s{};for(unsigned n=0;n<s.size();++n)s[n]=Parameters[n].initial;return s;}
        static bool valid(const State& s)
        {
            for(unsigned n=0;n<s.size();++n)
                if(!std::isfinite(s[n]) || s[n]<Parameters[n].min || s[n]>Parameters[n].max
                    || ((n==0 || n==4) && s[n]!=0 && s[n]!=1)) return false;
            return true;
        }
        void target(const State& state) {mTarget=state;}
        void stop() {mStopping=480;}
        void resume() {if(mStopping) clear();mMuted=false;}
        struct Output {float delay,reverb;};
        Output process(float delayInput,float reverbInput)
        {
            for(unsigned n=0;n<mState.size();++n)
            {
                mState[n]+=(mTarget[n]-mState[n])*0.0020811647;
                if(std::abs(mTarget[n]-mState[n])<1e-12)mState[n]=mTarget[n];
            }
            if(!(mCounter++%32))
            {
                mDamping=1-std::exp(-2*3.141592653589793*mState[6]/48000);
                for(unsigned n=0;n<8;++n)mFeedback[n]=std::pow(0.001,Lengths[n]/(48000*mState[5]));
            }
            if(mMuted) return {0,0};
            if(mStopping) delayInput=reverbInput=0;
            // Fractional read position glides when delay time changes.
            double read=mDelayAt-mState[1]*48;
            if(read<0) read+=DelaySize;
            const auto at=static_cast<unsigned>(read);const double fraction=read-at;
            const double delayed=mBuffers->delay[at]*(1-fraction)+mBuffers->delay[(at+1)%DelaySize]*fraction;
            mDelayLow+=(delayed-mDelayLow)*0.544;
            if(std::abs(mDelayLow)<1e-25)mDelayLow=0;
            mBuffers->delay[mDelayAt]=clean(delayInput*mState[0]+mDelayLow*mState[2]);
            mDelayAt=(mDelayAt+1)%DelaySize;
            double reverb=0;
            for(unsigned n=0;n<8;++n)
            {
                const double value=mBuffers->comb[n][mCombAt[n]];
                mCombLow[n]+=(value-mCombLow[n])*mDamping;
                if(std::abs(mCombLow[n])<1e-25)mCombLow[n]=0;
                mBuffers->comb[n][mCombAt[n]]=clean(reverbInput*mState[4]*0.25+mCombLow[n]*mFeedback[n]);
                mCombAt[n]=(mCombAt[n]+1)%Lengths[n];reverb+=value*0.125;
            }
            for(unsigned n=0;n<4;++n)
            {
                const double delayedAllpass=mBuffers->allpass[n][mAllpassAt[n]];
                const double value=delayedAllpass-reverb*0.5;
                mBuffers->allpass[n][mAllpassAt[n]]=clean(reverb+value*0.5);
                mAllpassAt[n]=(mAllpassAt[n]+1)%Diffusion[n];reverb=value;
            }
            const double fade=mStopping?mStopping/480.0:1;
            Output result{static_cast<float>(delayed*mState[0]*mState[3]*fade),static_cast<float>(reverb*mState[4]*mState[7]*fade)};
            if(mStopping && !--mStopping){clear();mMuted=true;}
            return result;
        }
    private:
        static float clean(double value) {return std::abs(value)<1e-25?0:static_cast<float>(value);}
        static constexpr unsigned DelaySize=72002;
        inline static constexpr std::array<unsigned,8> Lengths{{1493,1601,1747,1867,1999,2137,2341,2671}};
        inline static constexpr std::array<unsigned,4> Diffusion{{227,353,431,557}};
        struct Buffers
        {
            std::array<float,DelaySize> delay{};
            std::array<std::array<float,2671>,8> comb{};
            std::array<std::array<float,557>,4> allpass{};
        };
        void clear()
        {
            mBuffers->delay.fill(0);for(auto& line:mBuffers->comb)line.fill(0);for(auto& line:mBuffers->allpass)line.fill(0);
            mCombLow.fill(0);mDelayLow=0;mStopping=0;
        }
        std::unique_ptr<Buffers> mBuffers=std::make_unique<Buffers>();
        State mTarget=defaults();std::array<double,8> mState{{1,250,0.35,0.35,1,1.8,6000,0.25}};
        std::array<unsigned,8> mCombAt{};std::array<unsigned,4> mAllpassAt{};
        std::array<double,8> mCombLow{},mFeedback{};
        double mDelayLow=0,mDamping=0.5;
        unsigned mDelayAt=0,mCounter=0,mStopping=0;
        bool mMuted=false;
    };
}
#endif
