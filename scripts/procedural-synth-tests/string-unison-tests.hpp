void stringUnisonTests()
{
    const auto setup=[](Synth& synth) {
        synth.set("instrumentType",1);synth.set("stringLayout",1);synth.set("stringLegato",2);
        synth.set("attack",.002f);synth.set("sustain",1);synth.set("release",.005f);synth.set("stringDecay",10);
        synth.set("cutoff",18000);render(synth,4800);
    };
    Synth direct;setup(direct);
    direct.strike(64,.8f,{-1,1,-1});direct.strike(64,.7f,{-1,6,-1});render(direct,480);
    check(direct.voices()==2,"same pitch on low and high E has two independent string voices");
    direct.noteOff(64);render(direct,4800);check(!direct.voices(),"ordinary live key-up releases all same-pitch string keys");
    const auto expression=[](unsigned frame,int pitch,int selection) {
        // Absent articulation + five times the present selected-string digit.
        return scoreNote(frame,5u*unsigned(selection+1),1,pitch,130);
    };
    const auto score=expression(0,64,1)+scoreNote(0,2400,1,64,100)
        +expression(0,64,6)+scoreNote(0,9600,1,64,90);
    auto studio=std::make_unique<NativeStudio>();setupStudio(*studio);
    studio->set(1,"instrumentType",1);studio->set(1,"stringLayout",1);studio->set(1,"stringLegato",2);
    studio->set(1,"attack",.002f);studio->set(1,"sustain",1);studio->set(1,"release",.005f);studio->set(1,"stringDecay",10);
    check(studio->loadScore(score,12000,false) && studio->start(0,0),"same-pitch score with saved string identities starts");
    studioRender(*studio,1000);check(studio->voices()==2,"score launches both same-pitch strings");
    studioRender(*studio,3000);check(studio->voices()==1 && !studio->activeNotes().empty(),"first string gate releases alone while the other stays visible/held");
    studioRender(*studio,7000);check(!studio->voices() && studio->activeNotes().empty(),"last string gate clears the pitch indicator");
    auto a=std::make_unique<NativeStudio>(),b=std::make_unique<NativeStudio>();
    for(auto* s:{a.get(),b.get()})
    {
        setupStudio(*s);s->set(1,"instrumentType",1);s->set(1,"stringLayout",1);s->set(1,"stringLegato",2);
        check(s->loadScore(score,12000,true) && s->start(0,0),"unison loop starts");
    }
    check(studioRender(*a,48000,480)==studioRender(*b,48000,137),"unison gates and loops are independent of block size");
    a->stop();studioRender(*a,4800);check(!a->voices(),"Stop clears all unison ownership");
    a->strike(1,64,.8f,{-1,1,-1});a->strike(1,64,.8f,{-1,6,-1});studioRender(*a,480);
    check(a->voices()==2,"live Studio can hold explicit same-pitch strings");
    a->noteOff(1,64);studioRender(*a,48000);check(!a->voices(),"Studio live key-up releases every matching live string without stuck voices");
    // Pedal must retain/release only the ended string, even while another
    // same-pitch string still owns its gate.
    auto pedal=std::make_unique<NativeStudio>();setupStudio(*pedal);
    pedal->set(1,"instrumentType",1);pedal->set(1,"stringLayout",1);pedal->set(1,"stringLegato",2);pedal->set(1,"release",.005f);
    const auto held=scoreNote(0,65535,1,187,0)+score+scoreNote(5000,0,1,187,0);
    check(pedal->loadScore(held,12000,false) && pedal->start(0,0),"unison sustain test starts");
    studioRender(*pedal,4000);check(pedal->voices()==2,"sustain holds the first ended string");
    studioRender(*pedal,2000);check(pedal->voices()==1,"pedal-up releases ended string without stopping held unison");
    studioRender(*pedal,6000);check(!pedal->voices(),"pedal and final gate leave no stuck strings");
    std::cout<<"PASS same-pitch string voices, independent score gates, sustain, live release, note indicators, loops and Stop\n";
}
