#include "../../apps/openmw/mwsound/proceduralstring.hpp"

void stringTests()
{
    using Expression=MWSound::ProceduralStringExpression;
    using Sample=MWSound::ProceduralSample;
    Expression bypass;Expression::Settings neutral;bypass.trigger(neutral,.8,1);
    for(unsigned n=0;n<1000;++n)
    {
        const double input=std::sin(n*.1)*.3;
        check(bypass.process(input,.2,440)==input && bypass.pitch()==0,"neutral string expression is an exact bypass");
    }
    Expression pitch;auto pitched=neutral;pitched.pitch=.5;pitch.trigger(pitched,.8,1);
    check(pitch.pitch()==.5,"pick pitch begins at its authored value");
    for(unsigned n=0;n<4800;++n)pitch.process(.2,.1,440);
    check(pitch.pitch()<.001,"pick pitch settles instead of detuning the held note");
    const std::string hash(32,'a');auto source=std::make_shared<Sample>();source->hash=hash;
    for(unsigned n=0;n<96000;++n)
    {
        const double phase=2*3.141592653589793*261.625565*n/48000;
        source->pcm.push_back(float(.3*std::sin(phase)+.12*std::sin(phase*2)+.08*std::sin(phase*3)));
    }
    source->zones={{0,127,1,127,60,0,1,0,96000}};source->status.store(Sample::Ready);
    auto prepare=[&](bool recorded,unsigned mode,bool expressive=false) {
        auto synth=std::make_unique<Synth>();synth->set("instrumentType",1);synth->set("attack",.002f);
        synth->set("sustain",1);synth->set("release",.01f);synth->set("cutoff",18000);synth->set("resonance",0);
        synth->set("stringArticulation",float(mode));synth->set("stringMute",.7f);synth->set("stringDecay",3);
        if(expressive){synth->set("pickAttack",.3f);synth->set("pickPitch",.2f);synth->set("stringResonance",.3f);synth->set("pickVariation",.4f);synth->set("pickDirection",2);}
        if(recorded)check(synth->sample(source,60,false,hash),"expression sample fixture");
        render(*synth,4800);synth->noteOn(60,.8f);return synth;
    };
    for(bool recorded:{false,true})
    {
        auto open=prepare(recorded,1),palm=prepare(recorded,2),dead=prepare(recorded,3);
        const auto a=render(*open,48000),b=render(*palm,48000),c=render(*dead,48000);
        const auto tail=[](const auto& pcm){return energy(std::vector<float>(pcm.end()-4800,pcm.end()));};
        check(tail(a)>1e-7 && tail(b)<tail(a)*.0001,"open sustain and palm-muted decay are distinct on both sources");
        check(energy(c)>0 && !dead->voices() && tail(c)==0,"dead notes produce an attack and finish without waiting for note-off");
        auto raw=prepare(recorded,1),shaped=prepare(recorded,1,true);
        check(render(*raw,12000)!=render(*shaped,12000),"pick/resonance changes are audible DSP on both sources");
        auto plain=prepare(recorded,0),live=prepare(recorded,0);render(*plain,4800);render(*live,4800);
        live->set("stringResonance",.6f);
        check(render(*plain,12000)!=render(*live,12000) && live->voices()==1,"resonance can change an already-held note without a new strike");
    }
    auto shot=std::make_shared<Sample>();shot->hash=hash;shot->pcm=source->pcm;shot->zones=source->zones;
    shot->zones[0].flags=1;shot->status.store(Sample::Ready);
    auto ordinary=std::make_shared<Sample>();ordinary->hash=hash;ordinary->pcm=source->pcm;ordinary->status.store(Sample::Ready);
    for(const auto& data:{shot,ordinary})
    {
        auto a=prepare(false,0),b=prepare(false,3,true);
        a->allNotesOff();b->allNotesOff();render(*a,4800);render(*b,4800);
        a->sample(data,60,false,hash);b->sample(data,60,false,hash);render(*a,4800);render(*b,4800);
        a->noteOn(60,.8f);b->noteOn(60,.8f);
        check(render(*a,12000)==render(*b,12000),"one-shots and ordinary voice/effect samples bypass modeled guitar expression");
    }
    const auto score=scoreNote(0,21845,1,191,0)+scoreNote(0,48000,1,60)
        +scoreNote(12000,65535,1,191,0)+scoreNote(12000,24000,1,67);
    auto a=std::make_unique<NativeStudio>(),b=std::make_unique<NativeStudio>();
    for(auto* studio:{a.get(),b.get()})
    {
        Synth::State state;for(unsigned n=0;n<state.size();++n)state[n]=Synth::Parameters[n].initial;
        for(unsigned n=0;n<state.size();++n)
        {
            const auto name=std::string_view(Synth::Parameters[n].name);
            if(name=="sustain")state[n]=1;
            else if(name=="pickAttack")state[n]=.2f;
            else if(name=="stringResonance")state[n]=.25f;
            else if(name=="pickPitch")state[n]=.15f;
            else if(name=="pickDirection")state[n]=2;
        }
        check(studio->configure(1,state,.5f) && studio->sample(1,source,60,false,hash),"native expressive track setup");
        check(studio->loadScore(score,48001,false) && studio->start(0,0),"discrete articulation controls load beside notes");
    }
    check(studioRender(*a,24000,480)==studioRender(*b,24000,137),"per-note expression is block invariant");
    check(a->trackVoices(1)==1 && a->strikes()==2,"new dead-note articulation does not choke an already-held open note");
    check(!a->loadScore(scoreNote(0,1,1,191,128),48000,false),"articulation cannot use a continuous ramp");
    check(!a->loadScore(scoreNote(0,1,1,210,0),48000,false),"unknown string control is rejected");
    {
        auto plain=std::make_unique<NativeStudio>(),zero=std::make_unique<NativeStudio>();setupStudio(*plain);setupStudio(*zero);
        plain->loadScore(scoreNote(0,24000,1,60),24001,false);
        zero->loadScore(scoreNote(0,32768,1,195,0)+scoreNote(0,24000,1,60),24001,false);
        plain->start(0,0);zero->start(0,0);
        check(studioRender(*plain,12000)==studioRender(*zero,12000),"quantized neutral pick pitch retains exact source bypass");
    }
    {
        auto plain=std::make_unique<NativeStudio>(),attack=std::make_unique<NativeStudio>();
        Synth::State state;for(unsigned n=0;n<state.size();++n)state[n]=Synth::Parameters[n].initial;
        plain->configure(1,state,.5f);
        for(unsigned n=0;n<state.size();++n)if(std::string_view(Synth::Parameters[n].name)=="pickAttack")state[n]=.8f;
        attack->configure(1,state,.5f);
        for(auto* studio:{plain.get(),attack.get()}){studio->loadScore(scoreNote(0,12000,1,60),12001,false);studio->start(0,0);}
        check(studioRender(*plain,480)!=studioRender(*attack,480),"configured strike controls apply to the first cold scheduled note");
    }
    std::cout<<"PASS per-note string expression, legacy bypass, source eligibility, modeled gestures, control latching and block invariance\n";
}
