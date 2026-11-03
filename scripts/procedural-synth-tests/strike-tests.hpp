void strikeExpressionTests()
{
    const auto prepare=[](Synth& synth) {
        synth.set("attack",.002f);synth.set("sustain",1);synth.set("release",.01f);
        synth.set("instrumentType",1);synth.set("stringLayout",1);synth.set("stringLegato",2);render(synth,4800);
    };
    for(int articulation=0;articulation<=3;++articulation)for(int direction=0;direction<=2;++direction)
    {
        auto a=std::make_unique<Synth>(),b=std::make_unique<Synth>();prepare(*a);prepare(*b);
        b->set("stringArticulation",float(articulation));b->set("pickDirection",float(direction));b->set("stringSelect",1);
        check(a->strike(43,.7f,{articulation,1,direction}) && b->noteOn(43,.7f),"bounded attack expression queues");
        check(render(*a,4800)==render(*b,4800),"per-attack articulation/direction matches the same latched patch controls exactly");
    }
    {
        auto a=std::make_unique<Synth>();prepare(*a);
        check(!a->strike(40,.7f,{4,0,0}) && !a->strike(40,.7f,{0,8,0}) && !a->strike(40,.7f,{0,0,3})
            && !a->strike(128,.7f,{}) && !a->strike(40,-.1f,{}),"invalid attack expression cannot enter native queue");
        a->strike(45,.8f,{1,1,0});a->strike(47,.8f,{1,2,1});render(*a,4800);
        check(a->voices()==2,"same-frame attack snapshots retain separate strings instead of applying the last controller to both");
        a->noteOff(45);a->noteOff(47);render(*a,4800);check(!a->voices(),"expressive gates release normally");
        a->strike(40,.8f,{1,1,0});render(*a,500);a->strike(43,.8f,{3,1,1});render(*a,100);
        a->noteOff(43);render(*a,4800);check(a->voices()==1,"picked held-note return restores the original open articulation rather than the new dead attack");
        a->allNotesOff();render(*a,4800);check(!a->voices(),"panic clears expressive held returns");
    }
    {
        auto a=std::make_unique<Synth>(),b=std::make_unique<Synth>();prepare(*a);prepare(*b);
        a->noteOn(43,.8f);b->strike(43,.8f,{});check(render(*a,4800)==render(*b,4800),"empty expression exactly preserves legacy noteOn");
        a=std::make_unique<Synth>();b=std::make_unique<Synth>();setupDrum(*a,2);setupDrum(*b,2);
        a->noteOn(38,.8f);b->strike(38,.8f,{3,7,1});check(render(*a,4800)==render(*b,4800),"percussion ignores guitar attack overrides");
    }
    {
        auto studio=std::make_unique<NativeStudio>();Synth::State state;
        for(unsigned i=0;i<state.size();++i)state[i]=Synth::Parameters[i].initial;
        for(unsigned i=0;i<state.size();++i)
        {
            if(std::string_view(Synth::Parameters[i].name)=="stringLayout")state[i]=1;
            if(std::string_view(Synth::Parameters[i].name)=="stringLegato")state[i]=2;
        }
        state[12]=.01f;check(studio->configure(1,state,1),"expressive Studio track setup");
        studio->strike(1,45,.8f,{1,1,0});studio->strike(1,47,.8f,{1,2,1});studioRender(*studio,4800);
        check(studio->trackVoices(1)==2,"Studio live queue also preserves each attack's controller snapshot");
        check(!studio->strike(1,47,.8f,{0,8,0}) && !studio->strike(33,47,.8f,{}),"Studio rejects malformed expressive events");
        studio->releaseTrack(1);studioRender(*studio,4800);check(!studio->trackVoices(1),"track release clears expressive Studio notes");
        studio=std::make_unique<NativeStudio>();check(studio->configure(1,state,1),"cold saved per-string patch config");
        check(studio->loadScore(scoreNote(0,6000,1,40)+scoreNote(0,6000,1,45)+scoreNote(0,6000,1,50),12000,false)
            && studio->start(0,0),"cold saved per-string score starts without any tuning automation");
        studioRender(*studio,1);check(studio->trackVoices(1)==3,"saved per-string tuning applies before the very first cold score attacks");
    }
    std::cout<<"PASS per-attack expression snapshots, exact patch equivalence, same-frame string selection, held articulation return and native queue validation\n";
}
