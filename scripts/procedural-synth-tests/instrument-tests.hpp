#include "../../apps/openmw/mwsound/proceduralinstrument.hpp"
void instrumentTests()
{
    using Sample=MWSound::ProceduralSample;
    const std::string hash(32,'a');
    auto data=std::make_shared<Sample>();data->hash=hash;
    for(double hz:{220.,330.,440.,660.})for(unsigned i=0;i<24000;++i)data->pcm.push_back(static_cast<float>(.5*std::sin(2*3.141592653589793*hz*i/48000)));
    data->zones={{60,60,1,63,60,0,1,0,24000},
        {60,60,64,127,60,0,2,24000,24000},{60,60,64,127,60,1,2,48000,24000},
        {72,72,1,127,72,0,1,72000,24000}};
    data->status.store(Sample::Ready);
    auto setup=[&](Synth& s) {s.set("attack",.002f);s.set("sustain",1);s.set("release",.01f);s.set("cutoff",18000);s.set("resonance",0);check(s.sample(data,60,false,hash),"bank accepted");render(s,4800);};
    auto crossings=[](const std::vector<float>& pcm) {unsigned n=0;for(unsigned i=4801;i<pcm.size();++i)if(pcm[i-1]<=0 && pcm[i]>0)++n;return n;};
    Synth s;setup(s);
    auto hit=[&](int note,float velocity) {s.allNotesOff();render(s,4800);s.noteOn(note,velocity);return render(s,14400);};
    check(std::abs(int(crossings(hit(60,.3f)))-44)<=2,"velocity chooses soft recording");
    check(std::abs(int(crossings(hit(60,.8f)))-66)<=2,"first hard alternate selected");
    check(std::abs(int(crossings(hit(60,.8f)))-88)<=2,"second hard alternate selected");
    check(std::abs(int(crossings(hit(60,.8f)))-66)<=2,"alternate group wraps deterministically");
    check(std::abs(int(crossings(hit(72,.8f)))-132)<=2,"zone root avoids transposing another zone twice");
    check(energy(hit(59,.8f))==0 && !s.voices(),"unmapped bank notes stay silent");
    s.noteOn(60,.3f);s.noteOn(72,.8f);render(s,2400);check(s.voices()==2,"bank chord keeps independent zones");
    render(s,24000);check(!s.voices(),"zone end retires voices without reading adjacent sample");
    auto a=std::make_unique<NativeStudio>(),b=std::make_unique<NativeStudio>();setupStudio(*a);setupStudio(*b);
    const auto score=scoreNote(0,4800,1,60,100)+scoreNote(6000,4800,1,60,100)+scoreNote(12000,4800,1,72,70);
    for(auto* studio:{a.get(),b.get()})check(studio->sample(1,data,60,false,hash) && studio->loadScore(score,24000,true) && studio->start(0,0),"bank native score starts");
    check(studioRender(*a,24000,480)==studioRender(*b,24000,137),"bank playback and alternates are independent of audio block size");
    // Exercise the exact production binary reader, including alternate closure.
    auto encode=[](std::vector<Sample::Zone> zones,unsigned frames) {
        std::string bytes("FSBANK1\0",8);
        auto number=[&](std::uint32_t v,unsigned n){for(unsigned i=0;i<n;++i)bytes.push_back(char(v>>(8*i)));};
        number(static_cast<std::uint32_t>(zones.size()),4);number(frames,4);number(48000,4);number(0,4);
        for(const auto& z:zones) {for(unsigned v:std::array<unsigned,8>{z.lowKey,z.highKey,z.lowVelocity,z.highVelocity,z.root,z.alternate,z.alternates,0})number(v,1);number(z.start,4);number(z.length,4);}
        bytes.append(frames*2,'\0');return bytes;
    };
    auto read=[&](const std::string& bytes) {Sample out;std::istringstream in(bytes);std::size_t reserved=0;MWSound::readInstrument(in,bytes.size(),out,[&](std::size_t n){reserved=n;});check(reserved==out.pcm.size()*4,"bank reserves PCM before allocating");return out.zones.size();};
    check(read(encode({{40,80,1,127,60,0,1,0,32}},32))==1,"production bank binary reads");
    auto rejects=[&](std::string bytes){try{read(bytes);return false;}catch(const std::exception&){return true;}};
    check(rejects(encode({{40,80,1,127,60,0,2,0,32}},32)),"missing alternate rejected");
    check(rejects(encode({{40,80,1,127,60,0,1,0,32},{60,90,1,127,60,0,1,0,32}},32)),"ambiguous zones rejected");
    check(rejects(encode({{40,80,1,127,60,0,1,20,32}},32)),"zone out of PCM bounds rejected");
    auto truncated=encode({{40,80,1,127,60,0,1,0,32}},32);truncated.pop_back();check(rejects(truncated),"truncated PCM rejected");
    {
        const auto bytes=encode({{40,80,1,127,60,0,1,0,32}},32);std::istringstream in(bytes);Sample out;
        bool rejected=false;try {MWSound::readInstrument(in,bytes.size(),out,[](std::size_t){throw std::runtime_error("budget");});}catch(const std::exception&){rejected=true;}
        check(rejected && out.pcm.empty(),"PCM budget rejection happens before bank allocation");
    }
    std::cout<<"PASS multisample velocity/key zones, alternate hits, roots, cursors, native timing and binary validation\n";
}
