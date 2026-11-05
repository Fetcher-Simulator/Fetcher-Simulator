#include "../../apps/openmw/mwsound/proceduralsyntheffects.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>
#include "captured-amp-golden.hpp"

static std::atomic<unsigned> allocations{0};
static bool trackAllocations=false;
void* operator new(std::size_t n)
{
    if(trackAllocations)++allocations;
    if(void* p=std::malloc(n?n:1))return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete[](void* p,std::size_t) noexcept {std::free(p);}
static void check(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}

int main()
{
    try
    {
        MWSound::CapturedAmpModel model;
        std::uint32_t state=12345;std::size_t point=0;float maxError=0;
        trackAllocations=true;
        for(unsigned n=0;n<16384;++n)
        {
            state=state*1664525+1013904223;
            double input=((state>>16)/65535.0-.5)*.04+.06*std::sin(n*.071)+.02*std::sin(n*.193);
            if(n<64 || (n>=4096 && n<6144) || n>=12300)input=0;
            if(n>=8192 && n<12288)input*=3;
            const float y=model.infer(float(input));
            if(point<CaptureGolden.size() && CaptureGolden[point].first==n)
            {maxError=std::max(maxError,std::abs(y-CaptureGolden[point].second));++point;}
        }
        model.reset();
        for(unsigned n=0;n<10000;++n)check(std::abs(model.infer(0)-model.silentOutput())<2e-5,"prewarmed reset must remain silent");
        trackAllocations=false;
        check(point==CaptureGolden.size() && maxError<.0002f,"native capture differs from reference renderer");
        check(allocations==0,"model infer/reset allocated on audio thread");
        std::cout<<"PASS reference capture golden; max error "<<maxError<<", no infer/reset allocations\n";

        MWSound::ProceduralSynthEffects fx;
        MWSound::ProceduralSynthEffects::Settings settings;settings.mode=5;settings.drive=12;settings.tone=16000;
        trackAllocations=true;
        for(unsigned n=0;n<96000;++n)
        {
            const double input=n<24000?.2*std::sin(n*.071):n<48000?(n&1?1e6:-1e6):0;
            const auto value=fx.process(input,settings);
            check(std::isfinite(value) && std::abs(value)<100,"capture extreme input or tail is unsafe");
            if(n>95000)check(std::abs(value)<1e-10,"capture must drain into silence");
        }
        settings.mode=0;for(unsigned n=0;n<10000;++n)fx.process(0,settings);
        check(fx.process(0,settings)==0,"capture bypass must return to exact silence");
        settings.mode=5;for(unsigned n=0;n<2000;++n)check(std::abs(fx.process(0,settings))<1e-10,"silent reactivation must not emit a stale tail");
        trackAllocations=false;check(allocations==0,"capture effect process/reset allocated");
        std::cout<<"PASS native insert, extreme input, Stop tail, bypass/re-entry and allocation checks\n";
        MWSound::CapturedAmp broad,tight,moving;double broadEnergy=0,tightEnergy=0;
        trackAllocations=true;
        for(unsigned n=0;n<24000;++n)
        {
            const double source=.08*std::sin(n*.0081);
            const double a=broad.process(source,.45,0),b=tight.process(source,.45,1);
            const double c=moving.process(source,.45,double(n%4096)/4095);
            check(std::isfinite(c) && std::abs(c)<100,"capture tightness modulation is unsafe");
            broadEnergy+=a*a;tightEnergy+=b*b;
        }
        trackAllocations=false;check(allocations==0,"capture filter changes allocated");
        check(std::abs(broadEnergy-tightEnergy)>.001,"captured body/tightness must change the actual response");
        std::cout<<"PASS capture low-end range, live coefficient changes and allocation safety\n";

        std::array<std::unique_ptr<MWSound::CapturedAmp>,4> band;
        for(auto& amp:band)amp=std::make_unique<MWSound::CapturedAmp>();
        const auto start=std::chrono::steady_clock::now();double checksum=0;
        for(unsigned n=0;n<48000;++n)for(unsigned i=0;i<band.size();++i)
            checksum+=band[i]->process(.1*std::sin(n*(.071+i*.001)),.45);
        const double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        check(std::isfinite(checksum),"parallel captures failed");
        std::cout<<"4 independent captured amps: "<<seconds<<" seconds / second PCM (informational)\n";
    }
    catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
