// Offline audition of real Lua-compiled scores and exported preset snapshots.
#include "../../apps/openmw/mwsound/proceduralstudio.hpp"
#include "../../apps/openmw/mwsound/proceduralinstrument.hpp"
#include "../../components/files/hash.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>

int main(int argc,char** argv)
{
    try
    {
        if(argc!=8) throw std::runtime_error("score-audition score.bin tracks.tsv assets output.wav seek seconds group");
        std::ifstream scoreFile(argv[1],std::ios::binary),config(argv[2]);
        if(!scoreFile || !config) throw std::runtime_error("Cannot open score/config");
        const std::string score{std::istreambuf_iterator<char>(scoreFile),{}};
        const double seek=std::stod(argv[5]),seconds=std::stod(argv[6]);
        if(score.empty() || score.size()%11 || !std::isfinite(seconds) || seconds<=0 || seconds>1800
            || !std::isfinite(seek) || seek<0 || seek>=1800) throw std::runtime_error("Invalid score or audition range");
        MWSound::ProceduralStudio studio;
        std::map<std::string,std::shared_ptr<MWSound::ProceduralSample>> banks;
        std::map<unsigned,bool> selected;
        unsigned lane=0,length=0;std::string line;
        while(std::getline(config,line))
        {
            if(line.empty()) continue;
            if(line[0]=='@')
            {
                float level;std::string group;std::istringstream fields(line.substr(1));fields>>lane>>level>>group;
                if(!fields) throw std::runtime_error("Malformed track header");
                selected[lane]=std::string(argv[7])=="all" || group==argv[7];
                MWSound::ProceduralSynth::State state;
                for(unsigned n=0;n<state.size();++n) state[n]=MWSound::ProceduralSynth::Parameters[n].initial;
                if(!studio.configure(lane,state,level)) throw std::runtime_error("Track configuration rejected");
                continue;
            }
            const auto at=line.find('=');const auto name=line.substr(0,at),value=line.substr(at+1);
            if(name=="length") length=std::stoul(value);
            else if(name=="sample")
            {
                const auto path=std::string(argv[3])+"/"+value;auto& data=banks[path];
                if(!data)
                {
                    std::ifstream input(path,std::ios::binary);if(!input) throw std::runtime_error("Cannot open bank: "+path);
                    auto hash=Files::getHash(path,input);std::ostringstream fingerprint;
                    fingerprint<<std::hex<<std::setfill('0')<<std::setw(16)<<hash[0]<<std::setw(16)<<hash[1];
                    input.seekg(0,std::ios::end);const auto size=input.tellg();input.seekg(0);
                    data=std::make_shared<MWSound::ProceduralSample>();data->hash=fingerprint.str();
                    MWSound::readInstrument(input,static_cast<std::uint64_t>(size),*data,[](std::size_t){});
                    data->status.store(MWSound::ProceduralSample::Ready);
                }
                if(!studio.sample(lane,data,60,false,data->hash)) throw std::runtime_error("Bank configuration rejected");
            }
            else if(!studio.set(lane,name,std::stof(value))) throw std::runtime_error("Invalid parameter: "+name);
        }
        std::string filtered;
        for(std::size_t at=0;at+11<=score.size();at+=11)
            if(selected[static_cast<unsigned char>(score[at+8])]) filtered+=score.substr(at,11);
        if(!studio.loadScore(filtered,length,false) || !studio.start(0,seek)) throw std::runtime_error("Native score rejected");
        const auto frames=static_cast<unsigned>(seconds*48000);
        if(!frames) throw std::runtime_error("Audition is shorter than one frame");
        std::vector<float> pcm(frames);double sum=0,peak=0;
        for(unsigned at=0;at<frames;at+=480)
        {
            studio.keepAlive();studio.render(pcm.data()+at,std::min(480u,frames-at));
        }
        if(studio.fault()) throw std::runtime_error("Native playback fault");
        std::ofstream wav(argv[4],std::ios::binary);
        auto word=[&](unsigned value,unsigned size){for(unsigned n=0;n<size;++n)wav.put(char(value>>(n*8)));};
        wav.write("RIFF",4);word(36+frames*2,4);wav.write("WAVEfmt ",8);word(16,4);word(1,2);word(1,2);
        word(48000,4);word(96000,4);word(2,2);word(16,2);wav.write("data",4);word(frames*2,4);
        for(float sample:pcm)
        {
            if(!std::isfinite(sample) || std::abs(sample)>1) throw std::runtime_error("Invalid PCM");
            peak=std::max(peak,double(std::abs(sample)));sum+=sample*sample;
            word(static_cast<unsigned>(static_cast<std::int16_t>(sample*32767)),2);
        }
        if(!wav) throw std::runtime_error("WAV write failed");
        std::cout<<"{\"peak\":"<<peak<<",\"rms\":"<<std::sqrt(sum/frames)<<",\"strikes\":"<<studio.strikes()<<"}\n";
    }
    catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
