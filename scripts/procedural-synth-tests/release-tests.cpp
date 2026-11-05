#include "../../apps/openmw/mwsound/proceduralsynth.hpp"
#include "../../apps/openmw/mwsound/proceduralinstrument.hpp"
#include "../../apps/openmw/mwsound/proceduralstudio.hpp"
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>
using MWSound::ProceduralSample;
static void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
static std::shared_ptr<ProceduralSample> bank()
{
    auto data=std::make_shared<ProceduralSample>();data->hash=std::string(32,'b');data->hasReleases=true;
    data->pcm.resize(5000);for(unsigned i=0;i<100;++i)data->pcm[i]=.1f*std::sin(i*.07f);
    for(unsigned i=100;i<5000;++i)data->pcm[i]=.1f*std::sin(i*.11f);
    data->zones.push_back({0,127,1,127,60,0,1,0,100,0,0,0});
    data->zones.push_back({0,127,1,127,60,0,1,100,4900,2,0,0});
    data->status.store(ProceduralSample::Ready);return data;
}
static double energy(const std::vector<float>& x){double e=0;for(float n:x){check(std::isfinite(n),"Nonfinite release output");e+=n*n;}return e;}
static void configure(MWSound::ProceduralSynth& s,const std::shared_ptr<ProceduralSample>& data,float level=1)
{
    check(s.sample(data,60,false,data->hash),"Sample binding failed");
    s.set("attack",.002f);s.set("sustain",1);s.set("release",.005f);s.set("cutoff",18000);
    s.set("stringReleaseLevel",level);s.set("polyphony",1);
}
int main()
{
 try
 {
    auto data=bank();MWSound::ProceduralSynth s;configure(s,data);
    std::vector<float> out(9600);s.noteOn(60,.8f);s.render(out.data(),out.size());
    check(s.voices()==0,"Short body should end while key remains held");
    s.noteOff(60);out.resize(512);s.render(out.data(),out.size());
    check(s.voices()==1 && energy(out)>1e-5,"Physical release must sound after the main recording ended");
    s.allNotesOff();s.render(out.data(),out.size());check(energy(out)==0 && s.voices()==0,"Panic must clear release noise immediately");
    MWSound::ProceduralSynth a,b;configure(a,data);configure(b,data);
    out.resize(9600);a.noteOn(60,.8f);b.noteOn(60,.8f);a.render(out.data(),out.size());b.render(out.data(),out.size());
    a.noteOff(60);b.noteOff(60);b.noteOff(60);
    std::vector<float> x(512),y(512);a.render(x.data(),512);b.render(y.data(),512);check(x==y,"Duplicate off must not retrigger a physical ending");
    MWSound::ProceduralRelease tails;
    for(unsigned n=0;n<32;++n){tails.remember(n,60,-1,.8f,data.get(),1);tails.off(n,1);}
    check(tails.voices()==8,"Release tails must remain bounded without stealing held notes");
    tails.clear();tails.remember(60,60,0,.8f,data.get(),1);tails.retarget(60,62,62);tails.off(60,1);
    check(tails.voices()==0,"A returned shadow key must not release the sounding string");
    tails.off(62,1);check(tails.voices()==1,"Retargeted string has exactly one physical release owner");
    tails.clear();tails.remember(60,60,0,.8f,data.get(),1);tails.off(60,0);check(tails.voices()==0,"Release level zero is an exact bypass");
    MWSound::ProceduralStringExpression expression;
    MWSound::ProceduralStringExpression::Settings settings;expression.trigger(settings,.8,1);
    expression.touch(.2,0,true);check(!expression.enabled() && expression.process(.1,.3,440)==.1,"Neutral touch must preserve the legacy source");
    expression.touch(.2,1,true);double total=0;
    for(unsigned n=0;n<4000;++n)total+=std::abs(expression.process(.1,n&1?.3:-.3,440));
    check(std::isfinite(total) && total>0 && total<400,"Touch dynamics must remain finite and bounded");
    // Loader permits overlapping attack/release maps but validates alternates
    // independently; release+one-shot/choke combinations remain invalid.
    const auto encoded=[](unsigned flags,unsigned alternates) {
        std::string bytes="FSBANK2";bytes.push_back(0);
        auto number=[&](unsigned value,unsigned count){for(unsigned i=0;i<count;++i)bytes.push_back(char(value>>(8*i)));};
        number(2,4);number(4,4);number(48000,4);number(0,4);
        for(unsigned n=0;n<2;++n){for(unsigned value:{0u,127u,1u,127u,60u,0u,n?alternates:1u,n?flags:0u,0u,0u,0u,0u})number(value,1);number(n*2,4);number(2,4);}
        bytes.append(8,0);return bytes;
    };
    for(unsigned flags:{2u,3u,4u,6u})for(unsigned alternates:{1u,2u})
    {
        const auto bytes=encoded(flags,alternates);std::istringstream input(bytes);ProceduralSample parsed;bool accepted=true;
        try{MWSound::readInstrument(input,bytes.size(),parsed,[](std::size_t){});}catch(const std::runtime_error&){accepted=false;}
        check(accepted==((flags==2 || flags==4) && alternates==1),"Loader must distinguish trigger classes and reject malformed release groups");
        if(accepted)check(parsed.hasReleases==(flags==2) && parsed.hasHammer==(flags==4),"Loader must publish the correct trigger class");
    }
    auto studio=std::make_unique<MWSound::ProceduralStudio>();MWSound::ProceduralSynth::State state;
    for(unsigned i=0;i<state.size();++i)state[i]=MWSound::ProceduralSynth::Parameters[i].initial;
    check(studio->configure(1,state,1) && studio->sample(1,data,60,false,data->hash),"Studio release setup");
    studio->set(1,"stringReleaseLevel",1);
    const auto record=[](unsigned frame,unsigned duration,unsigned note,unsigned velocity=100) {
        std::string bytes;for(unsigned value:{frame,duration})for(unsigned n=0;n<4;++n)bytes.push_back(char(value>>(8*n)));
        bytes.push_back(1);bytes.push_back(char(note));bytes.push_back(char(velocity));return bytes;
    };
    const auto score=record(0,65535,187,0)+record(1,100,60)+record(10000,0,187,0);
    check(studio->loadScore(score,20000,false) && studio->start(0,0),"Pedal release score setup");
    out.resize(9600);studio->render(out.data(),out.size());
    check(studio->trackVoices(1)==0,"Pedal holds the physical release after the short body expires");
    out.resize(1024);studio->render(out.data(),out.size());
    check(studio->trackVoices(1)>0 && energy(out)>1e-6,"Pedal-up fires the recorded ending");
    studio->stop();studio->render(out.data(),out.size());check(studio->trackVoices(1)==0,"Studio Stop clears physical release tails");
    auto hammerData=bank();hammerData->hasHammer=true;
    hammerData->zones.push_back({0,127,1,127,60,0,1,100,4900,4,0,0});
    auto connected=std::make_unique<MWSound::ProceduralStudio>();connected->configure(1,state,1);connected->sample(1,hammerData,60,false,hammerData->hash);
    connected->set(1,"stringLegato",1);connected->noteOn(1,60,.8f);out.resize(32);connected->render(out.data(),out.size());
    connected->noteOn(1,62,.8f);out.resize(512);connected->render(out.data(),out.size());
    check(connected->trackVoices(1)>0 && energy(out)>1e-5,"Recorded hammer replaces an otherwise exhausted body without repicking");
    std::cout<<"PASS recorded release ownership after body end, duplicate gates, bounded tails, Stop, retarget, neutral/touch behavior and bank validation\n";
 }
 catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
