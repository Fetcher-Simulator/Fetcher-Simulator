#include "../../apps/openmw/mwsound/proceduraldsp.hpp"

void soundChangeSoakTests()
{
    // Verify convergence without relying on a host's flush-to-zero mode or
    // unstable wall-clock thresholds in CTest.
    float control=1;
    double weight=1;
    for (unsigned i=0;i<48000;++i)
    {
        control=MWSound::ProceduralDsp::smooth(control,0.f,0.004f);
        weight=MWSound::ProceduralDsp::smooth(weight,0.0,0.004);
        check(std::fpclassify(control)!=FP_SUBNORMAL && std::fpclassify(weight)!=FP_SUBNORMAL,
            "patch crossfades never enter subnormal arithmetic");
    }
    check(control==0 && weight==0,"float controls and double crossfades settle to exact zero");
    check(MWSound::ProceduralDsp::silence(-1e-24)==0 && MWSound::ProceduralDsp::silence(1e-8)==1e-8,
        "silence floor clears both polarities and preserves quiet audible signals");

    NativeStudio studio;
    Synth::State state;for(unsigned i=0;i<state.size();++i) state[i]=Synth::Parameters[i].initial;
    state[9]=0.002f;state[11]=1;state[12]=0.02f;
    auto configure=[&] {for(unsigned lane=1;lane<=16;++lane) check(studio.configure(lane,state,0.8f),"soak track configuration");};
    auto pump=[&](unsigned frames) {
        std::array<float,480> block{};
        for(unsigned at=0;at<frames;at+=480)
        {
            studio.keepAlive();studio.render(block.data(),std::min(480u,frames-at));
            for(float value:block) check(std::isfinite(value) && std::abs(value)<=1,"sound-change soak PCM stays bounded");
        }
    };
    auto measure=[&] {
        const auto begin=std::chrono::steady_clock::now();pump(48000);
        return std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
    };
    configure();pump(48000);const double fresh=measure();
    for(unsigned patch=0;patch<12;++patch)
    {
        state[0]=float(patch%5);state[5]=float((patch+2)%5);
        state[4]=float(patch%2);state[21]=patch%2?0.1f:0;
        state[46]=float(patch%4);state[47]=22;state[54]=float((patch+1)%4);
        configure();
        for(unsigned lane=1;lane<=16;++lane) check(studio.noteOn(lane,48+int(lane),0.5f),"soak notes start");
        pump(12000);studio.stop();pump(12000);
    }
    for(unsigned i=0;i<state.size();++i) state[i]=Synth::Parameters[i].initial;
    configure();studio.stop();pump(48000*35);
    check(studio.units()==17 && studio.voices()==0,"repeated sound changes retain a bounded rack and release every voice");
    check(studio.mixPeak()==0 && studio.trackPeak(1)==0 && studio.trackPeak(16)==0,
        "long-idle meter tails reach zero instead of sticking at subnormal values");
    const double cycled=measure();
    std::cout<<"PASS 16-track sound-change soak: "<<fresh<<" fresh / "<<cycled
        <<" cycled seconds per second PCM (timings informational)\n";
}
