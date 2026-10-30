#ifndef OPENMW_MWSOUND_CABINETCONVOLVER_H
#define OPENMW_MWSOUND_CABINETCONVOLVER_H

#include "cabinetirdata.hpp"
#include <array>
#include <complex>
#include <cmath>
#include <algorithm>
#include <memory>

namespace MWSound
{
    // Uniform partitioned overlap-save. One mono history per summed instrument,
    // 64 frames latency; immutable kernels are prepared before audio rendering.
    class CabinetConvolver
    {
    public:
        static constexpr unsigned Block=64, Size=128, Parts=32, Count=3, Fade=2048;
        using Complex=std::complex<float>;
        using Spectrum=std::array<Complex,Size>;
        static void fft(Spectrum& values,bool inverse)
        {
            static const auto roots=[] {
                std::array<Complex,Size/2> r{};
                for(unsigned n=0;n<r.size();++n) r[n]=std::polar(1.f,float(-2*3.141592653589793*n/Size));
                return r;
            }();
            for(unsigned i=1,j=0;i<Size;++i)
            {
                unsigned bit=Size>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;
                if(i<j)std::swap(values[i],values[j]);
            }
            for(unsigned length=2;length<=Size;length*=2)
                for(unsigned base=0;base<Size;base+=length)
                    for(unsigned n=0;n<length/2;++n)
                    {
                        const auto root=roots[n*Size/length];
                        const auto a=values[base+n],b=values[base+n+length/2]*(inverse?std::conj(root):root);
                        values[base+n]=a+b;values[base+n+length/2]=a-b;
                    }
            if(inverse)for(auto& value:values)value/=float(Size);
        }
        using Kernel=std::array<Spectrum,Parts>;
        static const std::array<Kernel,Count>& kernels()
        {
            static const auto data=[] {
                std::array<Kernel,Count> result{};
                for(unsigned ir=0;ir<Count;++ir)for(unsigned part=0;part<Parts;++part)
                {
                    for(unsigned n=0;n<Block;++n)result[ir][part][n]=CabinetIRData::Responses[ir][part*Block+n];
                    fft(result[ir][part],false);
                }
                return result;
            }();
            return data;
        }
        CabinetConvolver():mKernels(kernels()),mHistory(std::make_unique<std::array<Spectrum,Parts>>()) {}
        void clear()
        {
            for(auto& block:*mHistory)block.fill({});
            mInput.fill(0);mPrevious.fill(0);mOutput.fill(0);
            mAt=mHead=0;mFresh=true;mFade=Fade;
        }
        float process(float sample,unsigned selected)
        {
            selected=std::min(selected,Count-1);
            if(mFresh){mCurrent=mNext=selected;mFresh=false;}
            const auto output=mOutput[mAt];mInput[mAt]=sample;
            if(++mAt==Block)
            {
                mAt=0;mHead=(mHead+1)%Parts;auto& input=(*mHistory)[mHead];
                for(unsigned n=0;n<Block;++n){input[n]=mPrevious[n];input[n+Block]=mInput[n];}
                mPrevious=mInput;fft(input,false);
                if(mFade==Fade && selected!=mCurrent){mNext=selected;mFade=0;}
                const auto old=convolve(mCurrent);
                if(mFade<Fade)
                {
                    const auto next=convolve(mNext);
                    for(unsigned n=0;n<Block;++n)
                    {
                        const float mix=float(mFade+n)/Fade;
                        mOutput[n]=old[n]+(next[n]-old[n])*mix;
                    }
                    mFade+=Block;if(mFade==Fade)mCurrent=mNext;
                }
                else mOutput=old;
            }
            return output;
        }
    private:
        std::array<float,Block> convolve(unsigned ir) const
        {
            Spectrum sum{};
            for(unsigned part=0;part<Parts;++part)
            {
                const auto& signal=(*mHistory)[(mHead+Parts-part)%Parts];const auto& kernel=mKernels[ir][part];
                for(unsigned n=0;n<Size;++n)sum[n]+=signal[n]*kernel[n];
            }
            fft(sum,true);std::array<float,Block> result{};
            for(unsigned n=0;n<Block;++n)result[n]=std::abs(sum[n+Block].real())<1e-20f?0:sum[n+Block].real();
            return result;
        }
        const std::array<Kernel,Count>& mKernels;
        std::unique_ptr<std::array<Spectrum,Parts>> mHistory;
        std::array<float,Block> mInput{},mPrevious{},mOutput{};
        unsigned mAt=0,mHead=0,mCurrent=0,mNext=0,mFade=Fade;
        bool mFresh=true;
    };
}
#endif
