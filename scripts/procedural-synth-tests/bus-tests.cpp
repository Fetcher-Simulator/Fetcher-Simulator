#include "../../apps/openmw/mwsound/proceduralstudio.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
using Studio=MWSound::ProceduralStudio;
using Mixer=MWSound::ProceduralMixer;
static void check(bool value,const char* msg){if(!value)throw std::runtime_error(msg);}
static void setup(Studio& s,float volume=1)
{
    MWSound::ProceduralSynth::State p;
    for(unsigned i=0;i<p.size();++i)p[i]=MWSound::ProceduralSynth::Parameters[i].initial;
    for(unsigned i=0;i<p.size();++i)
    {
        const auto n=std::string_view(MWSound::ProceduralSynth::Parameters[i].name);
        if(n=="sustain" || n=="gain")p[i]=1;
        if(n=="volume")p[i]=volume;
        if(n=="attack")p[i]=.002f;
        if(n=="cutoff")p[i]=18000;
    }
    for(unsigned n=1;n<=2;++n)check(s.configure(n,p,1) && s.noteOn(n,60,.8f),"configure bus voices");
}
static std::vector<float> render(Studio& s,unsigned count)
{
    std::vector<float> v(count);for(unsigned at=0;at<count;at+=480)s.render(v.data()+at,std::min(480u,count-at));return v;
}
int main()
{
    try
    {
        auto empty=Mixer::defaults(),compressed=empty;compressed[6]=1;compressed[7]=-36;compressed[8]=8;compressed[9]=.1f;
        auto legacy=std::make_unique<Studio>(),bypass=std::make_unique<Studio>();setup(*legacy);setup(*bypass);
        check(bypass->buses(empty,1,true,empty),"default buses valid");
        check(render(*legacy,24000)==render(*bypass,24000),"default buses must preserve legacy PCM");
        auto member=std::make_unique<Studio>();setup(*member);check(member->buses(compressed,1,true,empty),"member compressor");
        render(*member,24000);check(member->memberReduction()>6 && member->masterReduction()==0,"member compressor must see the summed tracks only");
        check(member->buses(compressed,1,false,empty),"mute entire member");auto muted=render(*member,24000);
        check(std::abs(muted.back())<1e-10,"member mute drains every routed track");
        auto invalid=empty;invalid[12]=.5f;check(!member->buses(invalid,1,true,empty),"bus sends are not recursive");

        auto group=std::make_shared<MWSound::ProceduralMaster>();
        auto loud=std::make_unique<Studio>(),quiet=std::make_unique<Studio>(),separate=std::make_unique<Studio>();
        setup(*loud);setup(*quiet,.003f);setup(*separate,.003f);
        check(loud->linkMaster(group,0) && quiet->linkMaster(group,1),"link master outputs");
        check(!separate->linkMaster(group,1),"duplicate spatial slot rejected");
        auto other=std::make_shared<MWSound::ProceduralMaster>();check(separate->linkMaster(other,0),"separate performance master");
        for(auto* s:{loud.get(),quiet.get(),separate.get()})check(s->buses(empty,1,true,compressed),"song compressor target");
        std::array<float,480> block{};
        for(unsigned n=0;n<100;++n)for(auto* s:{loud.get(),quiet.get(),separate.get()})s->render(block.data(),block.size());
        check(loud->masterReduction()>6 && quiet->masterReduction()>6,"loud output must compress other band members");
        check(std::abs(loud->masterReduction()-quiet->masterReduction())<1,"linked outputs share steady gain reduction");
        check(separate->masterReduction()<.1,"another performance must not feed this master");
        loud->close();for(unsigned n=0;n<200;++n)quiet->render(block.data(),block.size());
        check(quiet->masterReduction()<.1,"closed member must not leave a stale detector");
        MWSound::ProceduralMaster tokens;const auto old=tokens.claim(3);tokens.publish(3,old,.5f);tokens.release(3,old);
        const auto fresh=tokens.claim(3);tokens.publish(3,old,1);check(tokens.envelope()==0,"retired slot cannot publish into replacement");
        tokens.publish(3,fresh,.25f);tokens.release(3,old);check(tokens.envelope()==.25,"retired close cannot clear replacement");
        std::cout<<"PASS member sums/mute, legacy bypass, linked spatial master, separate song isolation, slot generations and close cleanup\n";
    }
    catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
