#include "../../apps/openmw/mwsound/proceduralsyntheffects.hpp"
#include <sstream>

double magnitude(const std::vector<float>& samples, double hz)
{
    double real=0,imaginary=0;
    for (std::size_t i=0;i<samples.size();++i)
    {
        const double phase=2*3.14159265358979323846*hz*i/48000;
        real+=samples[i]*std::cos(phase);imaginary+=samples[i]*std::sin(phase);
    }
    return std::hypot(real,imaginary)/samples.size();
}
void guitarTests()
{
    using Effects=MWSound::ProceduralSynthEffects;
    Effects bypass;
    Effects::Settings settings;
    for (int n=0;n<1000;++n)
    {
        const double input=std::sin(n*0.713)*1.3;
        check(bypass.process(input,settings)==input,"legacy effects bypass is bit-exact");
    }
    settings.mode=2;settings.drive=30;settings.tone=16000;
    Effects oversampled;
    std::vector<float> processed(48000), naive(48000);
    const double gain=std::pow(10.0,settings.drive/20);
    for (int n=0;n<52800;++n)
    {
        const double input=std::sin(2*3.14159265358979323846*9000*n/48000);
        const auto output=oversampled.process(input,settings);
        if (n>=4800)
        {
            processed[n-4800]=static_cast<float>(output);
            naive[n-4800]=static_cast<float>(std::clamp(input*0.18*gain,-1.0,1.0)/std::sqrt(gain)/0.18);
        }
    }
    const auto alias=magnitude(processed,3000)/magnitude(processed,9000);
    const auto unfiltered=magnitude(naive,3000)/magnitude(naive,9000);
    std::cout<<"9 kHz distortion folded 3 kHz / fundamental: "<<alias<<" (1x "<<unfiltered<<")\n";
    check(alias<unfiltered*0.2,"4x resampling suppresses the folded fifth harmonic by more than 14 dB");
    std::array<double,4> signatures{};
    for (int mode=0;mode<4;++mode)
    {
        Effects effect;settings.mode=mode;settings.cabinet=3;settings.drive=36;
        settings.bass=12;settings.mid=-12;settings.treble=12;
        for (int n=0;n<96000;++n)
        {
            const double sample=effect.process(n<48000?std::sin(n*0.071)*2:0,settings);
            check(std::isfinite(sample),"every effects mode stays finite at extreme settings");
            if (n>=12000 && n<24000) signatures[mode]+=sample*sample;
            if (n>95000) check(std::abs(sample)<1e-12,"effects tails reach silence");
        }
    }
    check(std::abs(signatures[1]-signatures[2])>0.01 && std::abs(signatures[2]-signatures[3])>0.01,"drive characters produce distinct responses");
    Synth open,muted;
    for (auto* s : {&open,&muted})
    {
        s->set("instrumentType",1);s->set("sustain",1);s->set("release",0.05f);
        s->set("cutoff",14000);s->set("stringDecay",4);render(*s,4800);
    }
    muted.set("stringMute",1);render(muted,4800);
    open.noteOn(52,0.8f);muted.noteOn(52,0.8f);
    check(energy(render(open,4800))>0.000001,"physical string produces a plucked tone");
    render(muted,4800);render(open,4800);render(muted,4800);
    check(energy(render(muted,4800))<energy(render(open,4800))*0.1,"palm mute shortens the string decay");
    for (int note : {40,69,84})
    {
        Synth string;string.set("instrumentType",1);string.set("sustain",1);string.set("cutoff",14000);
        render(string,4800);string.noteOn(note,0.9f);render(string,4800);
        const auto samples=render(string,4800);
        const double expected=440*std::exp2((note-69)/12.0);
        const int period=static_cast<int>(48000/expected);
        int best=period;double score=-1;
        for (int lag=static_cast<int>(period*0.9);lag<=static_cast<int>(period*1.1);++lag)
        {
            double sum=0,left=0,right=0;
            for (std::size_t n=lag;n<samples.size();++n)
            { sum+=samples[n]*samples[n-lag];left+=samples[n]*samples[n];right+=samples[n-lag]*samples[n-lag]; }
            const double correlation=sum/std::sqrt(std::max(1e-30,left*right));
            if (correlation>score) {score=correlation;best=lag;}
        }
        check(std::abs(48000.0/best/expected-1)<0.025,"string fundamental tracks MIDI across guitar registers");
        string.noteOn(note,0.5f);render(string,4800);check(string.voices()==1,"same-note string retriggers reuse the melodic gate");
        string.allNotesOff();render(string,48000);check(string.voices()==0,"string panic releases its voice");
    }
    Synth extreme;
    extreme.set("instrumentType",1);extreme.set("stringDamping",0);extreme.set("stringDecay",10);
    extreme.set("driveType",3);extreme.set("drive",36);extreme.set("cabinet",3);extreme.set("ampBass",12);
    for (int note : {0,12,40,60,90,110,127}) extreme.noteOn(note,1);
    render(extreme,48000);extreme.set("driveType",1);extreme.set("stringMute",1);render(extreme,4800);
    extreme.allNotesOff();render(extreme,48000);check(extreme.voices()==0,"extreme string chord with effects remains panic-safe");
    std::cout<<"PASS guitar pitch, pluck, mute, retrigger, effects bypass, alias suppression and tails\n";
    std::array<std::unique_ptr<Synth>,16> orchestra;
    for (auto& instrument : orchestra)
    {
        instrument=std::make_unique<Synth>();instrument->set("instrumentType",1);instrument->set("polyphony",8);
        instrument->set("driveType",2);instrument->set("drive",30);instrument->set("cabinet",3);
        for (int note=48;note<56;++note) instrument->noteOn(note,0.8f);
    }
    std::array<float,480> block{};
    const auto start=std::chrono::steady_clock::now();
    for (int n=0;n<100;++n)
        for (auto& instrument : orchestra) {instrument->keepAlive();instrument->render(block.data(),block.size());}
    std::cout<<"16 instruments x 8 string voices with 4x inserts: "
        <<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<" seconds for 1 second PCM\n";
}

void writeBankPreview(const std::string& inputPath,const std::string& outputPath,bool matched=false)
{
    std::ifstream input(inputPath);check(static_cast<bool>(input),"bank parameters open");
    std::vector<float> mix;
    std::unique_ptr<Synth> synth;
    std::string line,name,role;
    auto flush=[&] {
        if (!synth) return;
        render(*synth,4800);
        const int base=role=="Bass"?48:60;
        synth->noteOn(role=="Drums"?38:base,0.8f);
        if(role!="Drums" && role!="Bass") {synth->noteOn(base+4,0.65f);synth->noteOn(base+7,0.7f);}
        auto held=render(*synth,48000);
        check(energy(held)>1e-10,"every bank sound produces audible-range PCM");
        synth->allNotesOff();auto tail=render(*synth,36000);
        const double rawRms=std::sqrt(energy(held));
        if(matched)
        {
            float peak=0;
            for(float sample:held) {check(std::isfinite(sample),"finite audition PCM");peak=std::max(peak,std::abs(sample));}
            for(float sample:tail) {check(std::isfinite(sample),"finite audition tail");peak=std::max(peak,std::abs(sample));}
            const double gain=std::min(0.06/std::max(rawRms,1e-10),0.8/std::max(static_cast<double>(peak),1e-10));
            for(float& sample:held) sample=static_cast<float>(sample*gain);
            for(float& sample:tail) sample=static_cast<float>(sample*gain);
        }
        mix.insert(mix.end(),held.begin(),held.end());mix.insert(mix.end(),tail.begin(),tail.end());
        std::cout<<"BANK "<<name<<" rawRMS="<<rawRms<<" auditionRMS="<<std::sqrt(energy(held))<<'\n';
        // Additional velocity/register checks without adding them to the preview.
        for (int note : {40,76})
        {
            synth->noteOn(note,0.3f);check(energy(render(*synth,12000))>1e-12,"bank soft register audition");
            synth->allNotesOff();render(*synth,48000);
        }
    };
    while (std::getline(input,line))
    {
        if(!line.empty() && line.back()=='\r') line.pop_back();
        if(line.empty()) continue;
        if(line[0]=='@')
        {
            flush();synth=std::make_unique<Synth>();const auto tab=line.find('\t');
            name=line.substr(1,tab-1);role=line.substr(tab+1);
        }
        else
        {
            const auto equals=line.find('=');check(synth && equals!=std::string::npos,"valid bank parameter line");
            check(synth->set(line.substr(0,equals),std::stof(line.substr(equals+1))),"native bank parameter matches Lua schema");
        }
    }
    flush();check(!mix.empty(),"bank has sounds");
    float peak=0;for(float sample:mix) peak=std::max(peak,std::abs(sample));
    std::ofstream file(outputPath,std::ios::binary);check(static_cast<bool>(file),"bank WAV opens");
    auto number=[&](std::uint32_t value,int bytes) {for(int i=0;i<bytes;++i) file.put(static_cast<char>((value>>(8*i))&255));};
    file.write("RIFF",4);number(36+static_cast<std::uint32_t>(mix.size()*2),4);file.write("WAVEfmt ",8);
    number(16,4);number(1,2);number(1,2);number(48000,4);number(96000,4);number(2,2);number(16,2);
    file.write("data",4);number(static_cast<std::uint32_t>(mix.size()*2),4);
    for(float sample:mix) number(static_cast<std::uint16_t>(static_cast<std::int16_t>(sample/std::max(peak,0.8f)*0.8f*32767)),2);
    check(static_cast<bool>(file),"bank WAV completes");
}
