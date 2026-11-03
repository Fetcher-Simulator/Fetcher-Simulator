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
        std::map<unsigned,std::shared_ptr<MWSound::ProceduralSample>> baseBanks;
        std::map<unsigned,MWSound::ProceduralSynth::AttackSamples> attackBanks;
        std::map<unsigned,bool> selected;
        std::map<unsigned,MWSound::ProceduralMixer::State> mixers;
        auto returns=MWSound::ProceduralReturns::defaults();float master=1;
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
                std::string gate;bool audible=true;
                if(fields>>gate) {if(gate!="0" && gate!="1")throw std::runtime_error("Invalid audibility gate");audible=gate=="1";}
                if(!studio.configure(lane,state,level,audible)) throw std::runtime_error("Track configuration rejected");
                mixers[lane]=MWSound::ProceduralMixer::defaults();
                continue;
            }
            const auto at=line.find('=');const auto name=line.substr(0,at),value=line.substr(at+1);
            if(name=="length") length=std::stoul(value);
            else if(name=="master") master=std::stof(value);
            else if(name.rfind("effects.",0)==0)
            {
                bool found=false;for(unsigned i=0;i<returns.size();++i)if(name.substr(8)==MWSound::ProceduralReturns::Parameters[i].name)
                    {returns[i]=std::stof(value);found=true;break;}
                if(!found)throw std::runtime_error("Unknown return setting: "+name);
            }
            else if(name.rfind("mix.",0)==0)
            {
                if(!lane)throw std::runtime_error("Mixer setting before track header");
                bool found=false;for(unsigned i=0;i<MWSound::ProceduralMixer::Parameters.size();++i)if(name.substr(4)==MWSound::ProceduralMixer::Parameters[i].name)
                    {mixers.at(lane)[i]=std::stof(value);found=true;break;}
                if(!found)throw std::runtime_error("Unknown channel setting: "+name);
            }
            else if(name=="sample" || name.rfind("sample.",0)==0)
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
                if(name=="sample")baseBanks[lane]=data;
                else
                {
                    const unsigned variant=name=="sample.open"?0:name=="sample.short"?1:name=="sample.dead"?2:3;
                    if(variant>=3)throw std::runtime_error("Unknown sample attack");
                    attackBanks[lane][variant]={data,60,false,data->hash};
                }
            }
            else if(!studio.set(lane,name,std::stof(value))) throw std::runtime_error("Invalid parameter: "+name);
        }
        for(const auto& [track,attacks]:attackBanks)if(!baseBanks.count(track))throw std::runtime_error("Attack banks require a base bank");
        for(const auto& [track,data]:baseBanks)
            if(!studio.sample(track,data,60,false,data->hash,attackBanks[track]))throw std::runtime_error("Bank configuration rejected");
        if(!studio.effects(returns,master))throw std::runtime_error("Invalid return/master settings");
        for(const auto& [track,state]:mixers)if(!studio.mixer(track,state))throw std::runtime_error("Invalid mixer settings");
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
