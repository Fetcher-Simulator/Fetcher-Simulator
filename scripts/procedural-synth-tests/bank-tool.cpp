#include "../../apps/openmw/mwsound/proceduralinstrument.hpp"
#include "../../apps/openmw/mwsound/proceduralsynth.hpp"
#include "../../components/files/hash.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <cmath>
int main(int argc,char** argv)
{
    try
    {
        if(argc<2)throw std::runtime_error("Provide a Fetcher bank path and optional output WAV");
        std::ifstream input(argv[1],std::ios::binary);if(!input)throw std::runtime_error("Cannot open bank");
        const auto hash=Files::getHash(argv[1],input);std::ostringstream fingerprint;
        fingerprint<<std::hex<<std::setfill('0')<<std::setw(16)<<hash[0]<<std::setw(16)<<hash[1];
        input.seekg(0,std::ios::end);const auto bytes=input.tellg();input.seekg(0);
        auto data=std::make_shared<MWSound::ProceduralSample>();data->hash=fingerprint.str();
        MWSound::readInstrument(input,static_cast<std::uint64_t>(bytes),*data,[](std::size_t){});
        data->status.store(MWSound::ProceduralSample::Ready);
        std::cout<<"{\"hash\":\""<<data->hash<<"\",\"zones\":"<<data->zones.size()<<",\"pcmBytes\":"<<data->pcm.size()*4<<"}\n";
        if(argc<3)return 0;
        MWSound::ProceduralSynth synth;synth.set("attack",.002f);synth.set("sustain",1);synth.set("release",.12f);synth.set("cutoff",16000);synth.set("polyphony",8);
        const std::string mode=argc>3?argv[3]:"clean";
        const bool metal=mode=="metal";
        synth.set("volume",metal?.5f:.7f);synth.set("driveType",metal?2:0);synth.set("drive",metal?29:0);synth.set("cabinet",metal?3:mode=="clean"?1:0);
        if(mode=="bass" || mode=="drums" || mode=="acoustic") {
            const bool nylon=std::string(argv[1]).find("nylon")!=std::string::npos;
            synth.set("cutoff",18000);synth.set("volume",nylon?.9f:.8f);synth.set("gain",mode=="acoustic"?2:1.25f);
            synth.set("fxOutput",mode=="acoustic"?(nylon?2:1.5f):1);
        }
        synth.sample(data,60,false,data->hash);
        std::vector<float> pcm;
        auto render=[&](unsigned frames){const auto at=pcm.size();pcm.resize(at+frames);for(unsigned n=0;n<frames;n+=480)synth.render(pcm.data()+at+n,std::min(480u,frames-n));};
        render(4800);
        const std::vector<int> notes=mode=="bass"?std::vector<int>{28,35,40,43,47,52}
            :mode=="drums"?std::vector<int>{36,38,37,43,47,50,46,42,44,49,51,53,52,57,59}
            :std::vector<int>{40,48,55,60,67,72};
        for(int note:notes)for(float velocity:{.4f,.85f}) {synth.noteOn(note,velocity);render(24000);synth.noteOff(note);render(12000);}
        std::ofstream out(argv[2],std::ios::binary);
        auto number=[&](std::uint32_t n,unsigned count){for(unsigned i=0;i<count;++i)out.put(char(n>>(8*i)));};
        out.write("RIFF",4);number(36+pcm.size()*2,4);out.write("WAVEfmt ",8);number(16,4);number(1,2);number(1,2);number(48000,4);number(96000,4);number(2,2);number(16,2);out.write("data",4);number(pcm.size()*2,4);
        for(float value:pcm) {if(!std::isfinite(value))throw std::runtime_error("Nonfinite sample preview");number(static_cast<std::uint16_t>(static_cast<std::int16_t>(std::clamp(value,-1.f,1.f)*32767)),2);}
        if(!out)throw std::runtime_error("Cannot write preview");
    }
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
