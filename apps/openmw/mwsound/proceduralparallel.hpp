#ifndef OPENMW_MWSOUND_PROCEDURALPARALLEL_H
#define OPENMW_MWSOUND_PROCEDURALPARALLEL_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include "proceduraldsp.hpp"

namespace MWSound
{
    // Per-instrument, mono parallel insert. The caller supplies the existing
    // 4x FIR kernel; both this path and its baseline have 16 frames of latency.
    // All histories are fixed-size and no global random generator is used.
    class ProceduralParallel
    {
    public:
        struct Settings
        {
            double mix=0,clean=.35,dirty=.75,noise=0,drive=18;
            int shape=0;
            double comb=0,delay=3.5,feedback=.35,rate=.5,depth=0,lift=0;
        };
        bool active() const { return mRunning; }
        void clear()
        {
            mUp.fill(0);mDown.fill(0);mComb->fill(0);mWeights={1,0,0,0};
            mUpAt=mDownAt=mCombAt=mFrames=0;mNoise=0x715a21u;mRunning=false;
            mBlend=mInputEnvelope=mNoiseHigh=mNoiseLow=mCombLow=mPhase=mLiftEnvelope=0;
            mGain=mLiftGain=mLiftTarget=1;
        }
        double process(double input,double baseline,const Settings& p,const std::array<double,65>& fir)
        {
            mBlend=ProceduralDsp::smooth(mBlend,p.mix,.004);
            if(mBlend<1e-6 && p.mix==0)
            {
                if(mRunning)clear();
                return baseline;
            }
            mRunning=true;
            if(!(mFrames++&15))mGain=std::pow(10.0,p.drive/20);
            for(unsigned n=0;n<4;++n)mWeights[n]=ProceduralDsp::smooth(mWeights[n],p.shape==int(n)?1.0:0.0,.004);
            mInputEnvelope=ProceduralDsp::smooth(mInputEnvelope,std::abs(input),std::abs(input)>mInputEnvelope?.01036:.000595);
            const double gate=std::min(1.0,mInputEnvelope/.001);
            const double norm=std::max(1.0,p.clean+p.dirty+p.noise);
            mUp[mUpAt]=input;
            double wet=0;
            for(unsigned phase=0;phase<4;++phase)
            {
                double up=0;
                for(unsigned tap=phase;tap<fir.size();tap+=4)up+=4*fir[tap]*mUp[(mUpAt+mUp.size()-tap/4)%mUp.size()];
                const double driven=up*.18*mGain;
                const double wrap=driven-4*std::floor((driven+2)/4);
                const double fold=wrap>1?2-wrap:wrap< -1?-2-wrap:wrap;
                const double dirty=(mWeights[0]*std::tanh(driven)+mWeights[1]*std::clamp(driven,-1.0,1.0)
                    +mWeights[2]*fold+mWeights[3]*std::abs(std::tanh(driven)))/(.18*std::sqrt(mGain));
                mNoise^=mNoise<<13;mNoise^=mNoise>>17;mNoise^=mNoise<<5;
                const double random=double(mNoise)/2147483648.0-1;
                mNoiseHigh=ProceduralDsp::smooth(mNoiseHigh,random,.02265); // 700 Hz at 192 kHz
                mNoiseLow=ProceduralDsp::smooth(mNoiseLow,random-mNoiseHigh,.17828); // 6 kHz
                mDown[mDownAt]=(up*p.clean+dirty*p.dirty+mNoiseLow*mInputEnvelope*gate*p.noise)/norm;
                if(phase==0)for(unsigned tap=0;tap<fir.size();++tap)wet+=fir[tap]*mDown[(mDownAt+mDown.size()-tap)%mDown.size()];
                mDownAt=(mDownAt+1)%mDown.size();
            }
            mUpAt=(mUpAt+1)%mUp.size();
            // The comb is feed-forward at zero feedback; bounded feedback is
            // damped and normalized so it does not generate a free-running layer.
            mPhase+=p.rate/48000;mPhase-=std::floor(mPhase);
            const double modulation=p.depth>0?std::sin(2*Pi*mPhase)*p.depth*.45:0;
            const double delay=std::clamp(p.delay*48*(1+modulation),2.0,2046.0);
            auto& comb=*mComb;
            double read=mCombAt+comb.size()-delay;if(read>=comb.size())read-=comb.size();
            const auto at=static_cast<unsigned>(read);const double fraction=read-at;
            const double delayed=comb[at]*(1-fraction)+comb[(at+1)%comb.size()]*fraction;
            mCombLow=ProceduralDsp::smooth(mCombLow,delayed,.544062);
            const double feedback=std::clamp(p.feedback,-.85,.85);
            comb[mCombAt]=ProceduralDsp::silence(wet+mCombLow*feedback);
            mCombAt=(mCombAt+1)%comb.size();
            wet=wet*(1-p.comb*.5)+delayed*(1-std::abs(feedback))*p.comb*.5;
            mLiftEnvelope=ProceduralDsp::smooth(mLiftEnvelope,std::abs(wet),std::abs(wet)>mLiftEnvelope?.01036:.000347);
            if(!(mFrames&15))
            {
                const double ceiling=std::pow(10.0,p.lift/20);
                const double boost=std::clamp(std::sqrt((.0316227766/.18)/std::max(mLiftEnvelope,1e-8)),1.0,ceiling);
                mLiftTarget=1+(boost-1)*std::clamp((mLiftEnvelope-1e-5)/.001,0.0,1.0);
            }
            mLiftGain=ProceduralDsp::smooth(mLiftGain,mLiftTarget,.001);
            return baseline+(wet*mLiftGain-baseline)*mBlend;
        }
    private:
        static constexpr double Pi=3.14159265358979323846;
        std::array<double,17> mUp{};
        std::array<double,65> mDown{};
        // Allocate once with the instrument on the control thread; clear and
        // render reuse it. Keep large histories off Windows' small thread stack.
        std::unique_ptr<std::array<double,2048>> mComb=std::make_unique<std::array<double,2048>>();
        std::array<double,4> mWeights{1,0,0,0};
        unsigned mUpAt=0,mDownAt=0,mCombAt=0,mFrames=0;
        std::uint32_t mNoise=0x715a21u;
        bool mRunning=false;
        double mBlend=0,mGain=1,mInputEnvelope=0,mNoiseHigh=0,mNoiseLow=0,mCombLow=0,mPhase=0;
        double mLiftEnvelope=0,mLiftGain=1,mLiftTarget=1;
    };
}
#endif
