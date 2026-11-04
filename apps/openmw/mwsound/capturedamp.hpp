#ifndef OPENMW_MWSOUND_CAPTUREDAMP_H
#define OPENMW_MWSOUND_CAPTUREDAMP_H

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <memory>
#include "capturedampdata.hpp"

namespace MWSound
{
    // Fixed 48-kHz, eight-channel causal WaveNet capture. The weight ordering,
    // residual/head equations and causal tap direction follow NAM Core's MIT
    // A2 implementation (Steven Atkinson). See the bundled license/provenance.
    // No model parser, plugin host, file access or allocations in infer/reset.
    class CapturedAmpModel
    {
        static constexpr unsigned ringSize(unsigned layer)
        {
            unsigned size=1;
            while(size <= (CapturedAmpData::Kernels[layer]-1)*CapturedAmpData::Dilations[layer]) size*=2;
            return size;
        }
        static constexpr unsigned HistoryFrames=9368;
        using Frame=std::array<float,8>;
        struct State
        {
            std::array<Frame,HistoryFrames> history{};
            std::array<Frame,16> head{};
            std::array<unsigned,23> position{};
            unsigned headPosition=0;
        };
        std::unique_ptr<State> mState=std::make_unique<State>();
        std::array<Frame,23> mSilence{};
        Frame mSilentHead{};
        float mSilentOutput=0;
    public:
        CapturedAmpModel()
        {
            // A causal feed-forward stack has a constant zero-input solution.
            // Calculate and cache it off the audio thread instead of running a
            // long prewarm when the player changes sounds during playback.
            Frame value{},sum{};
            unsigned frames=0;
            for(unsigned layer=0;layer<23;++layer)
            {
                frames+=ringSize(layer);
                mSilence[layer]=value;
                step(value,sum,0,layer,[&](unsigned){return value.data();});
            }
            // Changing the fixed architecture must also update this bound.
            if(frames>HistoryFrames) std::terminate();
            mSilentHead=sum;
            const float* head=CapturedAmpData::Weights.data()+CapturedAmpData::HeadOffset;
            float output=head[128];
            for(unsigned k=0;k<16;++k)for(unsigned i=0;i<8;++i)output+=head[k*8+i]*sum[i];
            mSilentOutput=output*head[129];
            reset();
        }
        void reset()
        {
            unsigned base=0;
            for(unsigned layer=0;layer<23;++layer)
            {
                const auto size=ringSize(layer);
                std::fill_n(mState->history.begin()+base,size,mSilence[layer]);
                mState->position[layer]=0;base+=size;
            }
            mState->head.fill(mSilentHead);mState->headPosition=0;
        }
        float silentOutput() const { return mSilentOutput; }
        float infer(float input)
        {
            const auto& weights=CapturedAmpData::Weights;
            Frame value{},sum{};
            for(unsigned i=0;i<8;++i)value[i]=weights[i]*input;
            unsigned base=0;
            for(unsigned layer=0;layer<23;++layer)
            {
                const auto size=ringSize(layer),mask=size-1,at=mState->position[layer];
                mState->history[base+at]=value;
                step(value,sum,input,layer,[&](unsigned back){return mState->history[base+((at-back)&mask)].data();});
                mState->position[layer]=(at+1)&mask;base+=size;
            }
            const auto at=mState->headPosition;
            mState->head[at]=sum;
            const float* head=weights.data()+CapturedAmpData::HeadOffset;
            float output=head[128];
            for(unsigned k=0;k<16;++k)
            {
                const auto& frame=mState->head[(at-(15-k))&15];
                for(unsigned i=0;i<8;++i)output+=head[k*8+i]*frame[i];
            }
            mState->headPosition=(at+1)&15;
            return output*head[129];
        }
    private:
        template<class History>
        static void step(Frame& value,Frame& sum,float input,unsigned layer,History history)
        {
            const auto kernel=CapturedAmpData::Kernels[layer];
            const float* w=CapturedAmpData::Weights.data()+CapturedAmpData::Offsets[layer];
            const float* bias=w+kernel*64;
            Frame z{};
            for(unsigned i=0;i<8;++i)z[i]=bias[i];
            for(unsigned k=0;k<kernel;++k)
            {
                const auto* x=history((kernel-1-k)*CapturedAmpData::Dilations[layer]);
                for(unsigned j=0;j<8;++j)
                    for(unsigned i=0;i<8;++i)z[i]+=w[k*64+j*8+i]*x[j];
            }
            for(unsigned i=0;i<8;++i)
            {
                z[i]+=bias[8+i]*input;
                if(z[i]<0)z[i]*=.01f;
                sum[i]+=z[i];
            }
            for(unsigned i=0;i<8;++i)value[i]+=bias[80+i];
            for(unsigned j=0;j<8;++j)
                for(unsigned i=0;i<8;++i)value[i]+=bias[16+j*8+i]*z[j];
        }
    };

    class CapturedAmp
    {
        struct Biquad
        {
            std::array<double,5> c{};double z1=0,z2=0;
            double process(double x)
            {
                const double y=c[0]*x+z1;
                z1=c[1]*x-c[3]*y+z2;z2=c[2]*x-c[4]*y;
                if(std::abs(z1)<1e-20)z1=0;
                if(std::abs(z2)<1e-20)z2=0;
                return y;
            }
            void reset(double input=0)
            {z1=-c[0]*input;z2=c[2]*input;}
        };
        static Biquad cut(double hz,bool high)
        {
            const double w=6.2831853071795864769*hz/48000,co=std::cos(w),a=std::sin(w)/std::sqrt(2.0),d=1+a;
            const double b=(1+(high?co:-co))/2;
            return {{{b/d,(high?-2:2)*b/d,b/d,-2*co/d,(1-a)/d}}};
        }
        static Biquad mid()
        {
            const double w=6.2831853071795864769*800/48000,a=std::sin(w)/(2*.7),A=std::pow(10.0,3.75/40),d=1+a/A;
            return {{{(1+a*A)/d,-2*std::cos(w)/d,(1-a*A)/d,-2*std::cos(w)/d,(1-a/A)/d}}};
        }
        CapturedAmpModel mModel;
        Biquad mInputLow=cut(7500,false),mInputHigh=cut(181,true),mMid=mid(),mBoostLow=cut(4975,false),mDc=cut(30,true);
        unsigned mQuiet=24000,mDelayAt=0;std::array<double,17> mDelay{};
    public:
        CapturedAmp(){reset();}
        void reset()
        {
            mModel.reset();mInputLow.reset();mInputHigh.reset();mMid.reset();mBoostLow.reset();
            mDc.reset(mModel.silentOutput());
            mQuiet=24000;mDelayAt=0;mDelay.fill(0);
        }
        double process(double input,double trim)
        {
            // Preserve the capture/HP tail, then sleep silent instruments. Wake
            // from the cached silence state without loading/prewarming a model.
            if(std::abs(input)<1e-12)
            {
                if(mQuiet>=24000)return 0;
                if(++mQuiet==24000){reset();return 0;}
            }
            else mQuiet=0;
            double x=mInputLow.process(std::clamp(input,-16.0,16.0));
            x=mMid.process(mInputHigh.process(x));
            x=mBoostLow.process(std::tanh(2.55*x)/2.55)*trim;
            const double y=mDc.process(mModel.infer(float(std::clamp(x,-4.0,4.0))));
            mDelay[mDelayAt]=std::isfinite(y)?std::clamp(y,-8.0,8.0)/.18:0;
            const double result=mDelay[(mDelayAt+1)%mDelay.size()];mDelayAt=(mDelayAt+1)%mDelay.size();
            return result;
        }
    };
}
#endif
