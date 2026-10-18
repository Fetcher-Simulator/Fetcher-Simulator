#include "../../apps/openmw/mwsound/proceduralmixer.hpp"
using Mixer=MWSound::ProceduralMixer;
double mixerTone(Mixer::State state,double hz,double amplitude=0.1)
{
    Mixer mixer;mixer.target(state,1);double power=0;
    for (unsigned i=0;i<96000;++i)
    {
        const auto value=mixer.process(static_cast<float>(amplitude*std::sin(2*3.141592653589793*hz*i/48000)));
        check(std::isfinite(value),"finite mixer sine");if(i>=48000) power+=value*value;
    }
    return std::sqrt(power/48000);
}
void mixerTests()
{
    auto state=Mixer::defaults();Mixer bypass;bypass.target(state,1);
    for (unsigned i=0;i<10000;++i)
    {
        const float input=static_cast<float>(std::sin(i*0.2));
        check(bypass.process(input)==input,"default strip is sample-exact bypass");
    }
    Mixer muted;muted.target(state,0);check(muted.process(0.7f)==0,"fresh muted channel never leaks a first sample");
    const auto reference=mixerTone(state,1000);
    state[0]=1;state[1]=200;
    check(mixerTone(state,50)<mixerTone(Mixer::defaults(),50)*0.08,"high-pass removes low bass");
    check(mixerTone(state,2000)>mixerTone(Mixer::defaults(),2000)*0.98,"high-pass preserves upper range");
    state=Mixer::defaults();state[0]=1;state[2]=12;
    check(mixerTone(state,40)>mixerTone(Mixer::defaults(),40)*3.5,"bass shelf boosts lows");
    state=Mixer::defaults();state[0]=1;state[3]=-12;
    check(mixerTone(state,1000)<reference*0.27,"mid bell cuts at its center");
    state=Mixer::defaults();state[0]=1;state[5]=12;
    check(mixerTone(state,16000)>mixerTone(Mixer::defaults(),16000)*3.7,"treble shelf boosts highs");
    state=Mixer::defaults();state[6]=1;state[7]=-20;state[8]=4;state[9]=0.1f;
    Mixer compressor;compressor.target(state,1);float output=0;
    for(unsigned i=0;i<48000;++i) output=compressor.process(1);
    check(std::abs(compressor.reduction()-15)<0.02 && std::abs(output-std::pow(10.f,-15.f/20))<0.001,"4:1 compression settles at analytical constant-signal gain");
    compressor.process(0);const float held=compressor.reduction();
    for(unsigned i=0;i<48000;++i) compressor.process(0);
    check(held>14 && compressor.reduction()<0.01,"release retains then recovers gain");
    state[11]=6;compressor.target(state,1);
    for(unsigned i=0;i<48000;++i) output=compressor.process(1);
    check(std::abs(output-std::pow(10.f,-9.f/20))<0.001,"makeup follows compression");
    state[6]=0;compressor.target(state,1);
    for(unsigned i=0;i<48000;++i) output=compressor.process(1);
    check(output==1 && compressor.reduction()<0.00001,"compressor bypass removes makeup and reduction");
    Mixer fast,slow;state=Mixer::defaults();state[6]=1;state[7]=-30;state[9]=0.1f;fast.target(state,1);
    state[9]=200;slow.target(state,1);
    for(unsigned i=0;i<24000;++i) { fast.process(0);slow.process(0); }
    for(unsigned i=0;i<480;++i) { fast.process(0.6f);slow.process(0.6f); }
    check(fast.reduction()>slow.reduction()+10,"attack controls transient gain reduction");
    state=Mixer::defaults();fast.target(state,0);
    for(unsigned i=0;i<48000;++i) output=fast.process(0.7f);
    check(output==0,"fader ramp reaches exact mute without denormal tails");
    // Extreme gains, sweeping frequencies, bypass changes, and all 16 strips.
    std::array<Mixer,16> strips{};const auto start=std::chrono::steady_clock::now();
    for(unsigned n=0;n<16;++n) { state=Mixer::defaults();state[0]=state[6]=1;state[2]=state[3]=state[5]=18;state[7]=-60;state[8]=20;state[11]=24;strips[n].target(state,0.8f); }
    for(unsigned i=0;i<48000;++i) for(auto& strip:strips)
        check(std::isfinite(strip.process(static_cast<float>(std::sin(i*0.231)))),"extreme 16-lane strip remains finite");
    const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    check(!Mixer::valid({}) && Mixer::valid(Mixer::defaults()),"invalid mixer state rejected");
    state=Mixer::defaults();state[0]=0.5f;check(!Mixer::valid(state),"fractional bypass rejected");
    std::cout<<"PASS mixer bypass, HPF/shelves/bell, compressor ratio/release/makeup and extremes; 16 strips: "<<elapsed<<" s / second PCM\n";
}
