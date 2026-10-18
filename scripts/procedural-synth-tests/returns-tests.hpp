#include "../../apps/openmw/mwsound/proceduralreturns.hpp"
using Returns=MWSound::ProceduralReturns;
void returnsTests()
{
    Returns delay;auto state=Returns::defaults();state[1]=40;state[2]=0;state[3]=1;
    delay.target(state);for(unsigned i=0;i<48000;++i)delay.process(0,0);
    for(unsigned i=0;i<10000;++i)
    {
        const auto wet=delay.process(i==0?1.f:0.f,0);
        check(std::abs(wet.delay-(i==1920?1.f:0.f))<0.00001,"delay impulse has exact 40 ms timing and zero extra echoes at zero feedback");
        check(wet.reverb==0,"delay never feeds reverb implicitly");
    }
    Returns room;double early=0,late=0;float peak=0;
    for(unsigned i=0;i<48000*6;++i)
    {
        const auto wet=room.process(0,i==0?1.f:0.f);
        check(std::isfinite(wet.reverb) && wet.delay==0,"reverb is finite and does not feed delay");
        if(i<1493)check(wet.reverb==0,"reverb has a causal initial reflection delay");
        if(i<48000)early+=wet.reverb*wet.reverb;
        if(i>48000*5)late+=wet.reverb*wet.reverb;
        peak=std::max(peak,std::abs(wet.reverb));
    }
    check(peak>0.001 && late<early*0.00001,"reverb produces a decaying diffuse tail");
    state=Returns::defaults();state[2]=0.85f;state[5]=8;state[6]=16000;state[3]=state[7]=1;
    Returns extreme;extreme.target(state);
    for(unsigned i=0;i<96000;++i)
    {
        auto wet=extreme.process(std::sin(i*0.17f),std::sin(i*0.23f));
        check(std::isfinite(wet.delay) && std::isfinite(wet.reverb) && std::abs(wet.delay)<20 && std::abs(wet.reverb)<20,"maximum feedback/decay remains bounded");
    }
    extreme.stop();for(unsigned i=0;i<480;++i)extreme.process(1,1);
    for(unsigned i=0;i<48000;++i){const auto wet=extreme.process(1,1);check(wet.delay==0 && wet.reverb==0,"Stop clears tails and suppresses stale sends");}
    extreme.resume();bool resumed=false;
    for(unsigned i=0;i<24000;++i){auto wet=extreme.process(i==0?1.f:0.f,0);resumed=resumed || wet.delay!=0;}
    check(resumed,"new performance resumes clean return buffers");
    auto invalid=Returns::defaults();invalid[2]=1;check(!Returns::valid(invalid),"unstable feedback rejected");
    NativeStudio a,b;setupStudio(a);setupStudio(b);auto strip=MWSound::ProceduralMixer::defaults();strip[12]=0.7f;strip[13]=0.6f;
    check(a.mixer(1,strip) && b.mixer(1,strip) && a.effects(state,0.7f) && b.effects(state,0.7f),"native shared sends and post-return master configured");
    const auto score=scoreNote(0,3000,1,60)+scoreNote(12000,3000,2,64);
    a.loadScore(score,24000,true);b.loadScore(score,24000,true);a.start(0,0);b.start(0,0);
    check(studioRender(a,96000,480)==studioRender(b,96000,137),"shared delay/reverb PCM is independent of callback boundaries");
    check(a.delayPeak()>0 && a.reverbPeak()>0,"native return meters are live");
    a.stop();studioRender(a,4800);check(energy(studioRender(a,48000))==0,"Studio Stop leaves no wet tail");
    check(!b.effects(state,2) && !b.effects(invalid,1),"invalid native return controls rejected");
    std::cout<<"PASS shared delay/reverb impulses, decay, stability, Stop/resume, metering and block-invariant PCM\n";
}
