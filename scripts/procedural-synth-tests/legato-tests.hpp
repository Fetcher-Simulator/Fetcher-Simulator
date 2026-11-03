void legatoTests()
{
    MWSound::ProceduralPitchSlide slide;slide.start(-12,.1);
    check(slide.offset()==-12,"slide starts at the previous sounding pitch");
    for(unsigned n=0;n<2400;++n)slide.next();check(std::abs(slide.offset()+6)<1e-9,"slide travels in semitone space");
    slide.start(slide.offset()+4,.05);check(std::abs(slide.offset()+2)<1e-9,"retarget retains the current pitch offset");
    for(unsigned n=0;n<2400;++n)slide.next();check(slide.offset()==0,"slide reaches the exact target in finite time");
    slide.start(12,0);check(slide.offset()==0,"zero time changes pitch immediately");
    using Sample=MWSound::ProceduralSample;
    const std::string hash(32,'a');auto bank=std::make_shared<Sample>();bank->hash=hash;
    for(unsigned n=0;n<144000;++n)bank->pcm.push_back(float(.3*std::sin(2*3.141592653589793*261.625565*n/48000)));
    bank->zones={{0,127,1,127,60,0,1,0,144000}};bank->status.store(Sample::Ready);
    const auto prepare=[&](int mode,int source=0) {
        auto s=std::make_unique<Synth>();s->set("attack",.002f);s->set("sustain",1);s->set("release",.01f);
        s->set("cutoff",18000);s->set("resonance",0);s->set("stringLegato",float(mode));s->set("stringSlideTime",.1f);
        if(source==1)s->set("instrumentType",1);
        if(source==2)check(s->sample(bank,60,false,hash),"legato bank setup");
        render(*s,4800);s->noteOn(60,.8f);render(*s,4800);return s;
    };
    const auto frequency=[](const std::vector<float>& pcm) {
        unsigned crossings=0;for(unsigned n=1;n<pcm.size();++n)if(pcm[n-1]<=0 && pcm[n]>0)++crossings;
        return crossings*48000.0/pcm.size();
    };
    for(int source:{0,1,2})
    {
        auto connected=prepare(1,source),reference=prepare(1,source);
        connected->noteOn(72,.2f);const auto first=render(*connected,1),unchanged=render(*reference,1);
        check(std::abs(first[0]-unchanged[0])<1e-7,"legato keeps source cursor, envelopes, picked velocity and first-frame pitch");
        render(*connected,9600);
        check(connected->voices()==1,"overlapping legato uses one sounding voice");
        if(source!=1)check(std::abs(frequency(render(*connected,9600))-523.251)<6,"legato settles at the new octave on oscillator and recorded sources");
        connected->noteOff(72);render(*connected,9600);
        check(connected->voices()==1,"releasing the newest key returns to an earlier held key");
        if(source!=1)check(std::abs(frequency(render(*connected,9600))-261.626)<6,"last-note fallback reaches the earlier held pitch");
        connected->noteOff(60);render(*connected,4800);check(connected->voices()==0,"last key release ends connected playback");
        auto oldOff=prepare(1,source);oldOff->noteOn(67,.8f);render(*oldOff,9600);oldOff->noteOff(60);render(*oldOff,4800);
        check(oldOff->voices()==1,"an older key-off never releases the newer connected note");
        oldOff->noteOff(67);render(*oldOff,4800);check(!oldOff->voices(),"released older keys cannot reappear as ghost fallback notes");
        auto picked=prepare(2,source),unpicked=prepare(1,source);
        picked->noteOn(67,.3f);unpicked->noteOn(67,.3f);
        check(render(*picked,4800)!=render(*unpicked,4800),"picked connection restarts excitation/source while legato retains it");
        picked->noteOff(67);render(*picked,9600);check(picked->voices()==1,"picked slides also return to held notes");
        picked->allNotesOff();render(*picked,4800);check(!picked->voices(),"panic clears connected voices");
        picked->noteOn(64,.8f);render(*picked,1000);picked->noteOff(64);render(*picked,4800);check(!picked->voices(),"panic clears the held-note stack before a fresh phrase");
    }
    {
        auto mono=prepare(1);
        mono->noteOff(60);render(*mono,4800);mono->noteOn(72,.8f);render(*mono,4800);
        check(std::abs(frequency(render(*mono,9600))-523.251)<6,"a separate phrase starts at its target rather than sliding from a released key");
        mono->noteOn(76,.8f);render(*mono,500);mono->set("stringLegato",0);render(*mono,500);mono->noteOn(79,.8f);render(*mono,4800);
        check(mono->voices()==2,"turning connection mode off restores polyphonic new attacks");
        mono->allNotesOff();render(*mono,4800);check(!mono->voices(),"mode switching preserves panic cleanup");
        auto a=prepare(1),b=prepare(1);
        for(auto* s:{a.get(),b.get()}){s->noteOn(72,.8f);render(*s,2400);}
        a->noteOn(64,.4f);
        check(std::abs(render(*a,1)[0]-render(*b,1)[0])<1e-7,"a mid-slide retarget begins at the currently sounding pitch");
        a->allNotesOff();render(*a,4800);
    }
    for(bool ordinary:{false,true})
    {
        auto source=std::make_shared<Sample>();source->hash=hash;source->pcm=bank->pcm;
        if(!ordinary)source->zones={{0,127,1,127,60,0,1,0,144000,1,1,0}};
        source->status.store(Sample::Ready);
        auto a=std::make_unique<Synth>(),b=std::make_unique<Synth>();
        for(auto* s:{a.get(),b.get()})check(s->sample(source,60,false,hash),"unsupported legato source fixture");
        b->set("stringLegato",1);b->set("stringSlideTime",.5f);
        for(int note:{60,67,72}){a->noteOn(note,.8f);b->noteOn(note,.8f);check(render(*a,1000)==render(*b,1000),"one-shots and ordinary voice/effect clips bypass mono connection exactly");}
    }
    {
        auto a=std::make_unique<Synth>(),b=std::make_unique<Synth>();b->set("stringLegato",2);
        for(auto* s:{a.get(),b.get()})s->set("drumType",2);
        for(int note:{36,38,42}){a->noteOn(note,.8f);b->noteOn(note,.8f);check(render(*a,1000)==render(*b,1000),"synthesized percussion bypasses connected playing");}
    }
    {
        auto a=std::make_unique<NativeStudio>(),b=std::make_unique<NativeStudio>();
        Synth::State state;for(unsigned i=0;i<state.size();++i)state[i]=Synth::Parameters[i].initial;
        state[9]=.002f;state[11]=1;state[12]=.01f;
        const auto score=scoreNote(0,32768,1,210,0)+scoreNote(0,6554,1,211,0)
            +scoreNote(0,30000,1,60)+scoreNote(6000,6000,1,67)+scoreNote(15000,6000,1,64);
        for(auto* studio:{a.get(),b.get()})check(studio->configure(1,state,1) && studio->loadScore(score,36000,false) && studio->start(0,0),"authored mono/slide controls load beside overlapping gates");
        check(studioRender(*a,24000,480)==studioRender(*b,24000,137),"connected automation and held-note fallback are block invariant");
        check(a->trackVoices(1)==1 && a->strikes()==3,"native score retains three note events and one connected voice");
        a->releaseTrack(1);studioRender(*a,4800);check(!a->trackVoices(1),"track release clears connected held keys");
        a->stop();studioRender(*a,4800);check(!a->trackVoices(1),"Studio Stop clears held-note priority");
        check(!a->loadScore(scoreNote(0,1,1,210,128),36000,false),"connection modes cannot ramp");
        check(!a->loadScore(scoreNote(0,1,1,214,0),36000,false),"unknown connected control rejected");
        check(a->start(0,18000.0/48000),"connected score supports seek");studioRender(*a,24000,137);
        check(!a->trackVoices(1),"seek does not leave unheard earlier gates in a held stack");
    }
    {
        auto studio=std::make_unique<NativeStudio>();
        Synth::State state;for(unsigned i=0;i<state.size();++i)state[i]=Synth::Parameters[i].initial;
        for(unsigned i=0;i<state.size();++i)if(std::string_view(Synth::Parameters[i].name)=="stringLegato")state[i]=1;
        check(studio->configure(1,state,1),"cold mono track config");
        studio->loadScore(scoreNote(0,6000,1,60)+scoreNote(0,6000,1,67),12000,false);studio->start(0,0);studioRender(*studio,1);
        check(studio->trackVoices(1)==1,"configured mono mode applies before the first cold simultaneous attacks");
    }
    {
        auto a=std::make_unique<NativeStudio>(),b=std::make_unique<NativeStudio>();
        for(auto* studio:{a.get(),b.get()})setupStudio(*studio);
        const auto loop=scoreNote(0,32768,1,210,0)+scoreNote(0,18000,1,60)+scoreNote(6000,4000,1,64);
        for(auto* studio:{a.get(),b.get()}){check(studio->loadScore(loop,12000,true),"overlapping mono loop loads");studio->start(0,0);}
        check(studioRender(*a,36000,480)==studioRender(*b,36000,137),"connected fallback across looped same-pitch gate counts is block invariant");
        a->stop();studioRender(*a,4800);check(!a->trackVoices(1),"loop Stop clears the mono key stack");
        const auto pedal=scoreNote(0,32768,1,210,0)+scoreNote(0,65535,1,187,0)+scoreNote(0,1000,1,60)+scoreNote(2000,1000,1,64);
        check(a->loadScore(pedal,12000,false) && a->start(0,0),"sustained connected score loads");studioRender(*a,5000);
        a->stop();studioRender(*a,4800);check(!a->trackVoices(1),"Stop clears connected keys before releasing sustain-held gates");
        a->noteOn(1,72,.8f);studioRender(*a,1000);a->noteOff(1,72);studioRender(*a,4800);
        check(!a->trackVoices(1),"fresh live playing after sustained Stop cannot resurrect earlier notes");
    }
    std::cout<<"PASS finite pitch slides, legato/picked connections, held-note fallback, source bypass, panic/Stop/seek and block-invariant scores\n";
}

void stringOwnershipTests()
{
    using Sample=MWSound::ProceduralSample;
    const std::string hash(32,'b');auto bank=std::make_shared<Sample>();bank->hash=hash;
    for(unsigned n=0;n<192000;++n)bank->pcm.push_back(float(.2*std::sin(2*3.141592653589793*261.625565*n/48000)));
    bank->zones={{0,127,1,127,60,0,1,0,192000}};bank->status.store(Sample::Ready);
    const auto prepare=[&](int source,int mode=1,int layout=1) {
        auto s=std::make_unique<Synth>();s->set("attack",.002f);s->set("sustain",1);s->set("release",.01f);
        s->set("cutoff",18000);s->set("resonance",0);s->set("stringLegato",float(mode));s->set("stringLayout",float(layout));s->set("stringSlideTime",.05f);
        if(source==1)s->set("instrumentType",1);
        if(source==2)check(s->sample(bank,60,false,hash),"per-string bank setup");
        render(*s,4800);return s;
    };
    for(int source:{0,1,2})for(int mode:{1,2})
    {
        auto s=prepare(source,mode);
        for(int note:{40,45,50,55,59,64})s->noteOn(note,.6f);
        render(*s,4800);check(s->voices()==6,"automatic guitar allocation retains six independent strings");
        s->noteOn(66,.8f);render(*s,4800);check(s->voices()==6,"saturated playable strings connect on the nearest string only");
        s->noteOff(66);render(*s,4800);check(s->voices()==6,"per-string release returns to its own earlier held note");
        s->noteOff(64);render(*s,4800);check(s->voices()==5,"returning high string releases independently of the other five");
        s->set("stringSelect",1);s->noteOn(42,.8f);render(*s,4800);
        check(s->voices()==5,"explicit low-string slides leave the other strings sounding");
        s->noteOff(40);render(*s,4800);check(s->voices()==5,"older low-string release does not cut the new note");
        s->set("stringSelect",2);s->noteOff(42);render(*s,4800);
        check(s->voices()==4,"key-off uses original ownership after the string selector changes");
        s->allNotesOff();render(*s,4800);check(!s->voices(),"panic clears every string and held-note return");
        s->noteOn(47,.5f);render(*s,1000);s->noteOff(47);render(*s,4800);check(!s->voices(),"fresh phrase cannot resurrect pre-panic strings");
        auto unplayable=prepare(source,mode);unplayable->noteOn(20,.7f);unplayable->noteOn(120,.7f);render(*unplayable,4800);
        check(unplayable->voices()==2,"out-of-range notes retain original pitches and ordinary polyphonic gates");
        unplayable->noteOff(20);unplayable->noteOff(120);render(*unplayable,4800);check(!unplayable->voices(),"fallback notes release normally");
        auto plain=prepare(source,0,0),disabled=prepare(source,0,4);
        for(int note:{40,45,50,55,59,64,67}){plain->noteOn(note,.7f);disabled->noteOn(note,.7f);check(render(*plain,500)==render(*disabled,500),"disabled connection bypasses string allocation audibly exactly");}
    }
    for(const auto& entry:std::array<std::pair<int,int>,6>{{{1,40},{2,38},{3,36},{4,35},{5,28},{6,23}}})
    {
        auto s=prepare(0,1,entry.first);s->set("stringSelect",1);
        s->noteOn(entry.second,.7f);render(*s,500);s->noteOn(entry.second+2,.7f);render(*s,4800);
        check(s->voices()==1,"each tuning connects its lowest open string through fret two");
        s->set("stringLayout",0);render(*s,4800);check(!s->voices(),"tuning changes retire old owned gestures and held keys");
        s->noteOn(72,.7f);render(*s,500);s->noteOff(72);render(*s,4800);check(!s->voices(),"tuning change cannot return to notes from the old layout");
    }
    {
        // Inspect pitch as well as voice counts: both independently connected
        // strings must reach their targets, while a drone string keeps its pitch.
        const auto power=[](const std::vector<float>& pcm,double frequency) {
            double re=0,im=0;for(unsigned n=0;n<pcm.size();++n)
            {const double phase=2*3.141592653589793*frequency*n/48000;re+=pcm[n]*std::cos(phase);im+=pcm[n]*std::sin(phase);}
            return (re*re+im*im)/(pcm.size()*pcm.size());
        };
        for(int source:{0,2})
        {
            auto s=prepare(source);s->set("stringSelect",1);s->noteOn(40,.6f);render(*s,1000);
            s->set("stringSelect",2);s->noteOn(45,.6f);render(*s,1000);
            s->set("stringSelect",3);s->noteOn(50,.6f);render(*s,1000);
            s->set("stringSelect",1);s->noteOn(43,.6f);render(*s,1000);
            s->set("stringSelect",2);s->noteOn(48,.6f);render(*s,4800);
            const auto pcm=render(*s,24000);
            for(int note:{43,48,50})check(power(pcm,440*std::exp2((note-69)/12.0))>1e-6,"two independent string targets and an unchanged drone are audible at their own pitches");
            for(int note:{43,48,50,40,45})s->noteOff(note);render(*s,4800);check(!s->voices(),"all independent pitch probes release cleanly");
        }
    }
    {
        auto s=prepare(0);s->set("stringSelect",7);s->noteOn(64,.7f);s->noteOn(67,.7f);render(*s,4800);
        check(s->voices()==2,"nonexistent selected strings fall back without discarding notes");
        s->allNotesOff();render(*s,4800);
        s->set("polyphony",1);s->set("stringSelect",1);s->noteOn(40,.7f);render(*s,500);
        s->set("stringSelect",2);s->noteOn(45,.7f);render(*s,500);
        s->set("stringSelect",1);s->noteOn(42,.7f);render(*s,500);s->noteOff(42);render(*s,4800);
        check(!s->voices(),"voice stealing invalidates old string returns rather than resurrecting stolen keys");
    }
    for(bool ordinary:{false,true})
    {
        auto source=std::make_shared<Sample>();source->hash=hash;source->pcm=bank->pcm;
        if(!ordinary)source->zones={{0,127,1,127,60,0,1,0,192000,1,1,0}};
        source->status.store(Sample::Ready);
        auto a=std::make_unique<Synth>(),b=std::make_unique<Synth>();
        for(auto* s:{a.get(),b.get()})s->sample(source,60,false,hash);
        b->set("stringLayout",1);b->set("stringSelect",1);b->set("stringLegato",2);
        for(int note:{40,43,47}){a->noteOn(note,.8f);b->noteOn(note,.8f);check(render(*a,500)==render(*b,500),"sample one-shots and ordinary clips bypass per-string mode exactly");}
    }
    {
        auto a=std::make_unique<NativeStudio>(),b=std::make_unique<NativeStudio>();
        Synth::State state;for(unsigned i=0;i<state.size();++i)state[i]=Synth::Parameters[i].initial;
        state[9]=.002f;state[11]=1;state[12]=.01f;
        const auto score=scoreNote(0,32768,1,210,0)+scoreNote(0,10923,1,212,0)+scoreNote(0,9362,1,213,0)
            +scoreNote(0,24000,1,40)+scoreNote(1000,18724,1,213,0)+scoreNote(1000,24000,1,45)
            +scoreNote(3000,9362,1,213,0)+scoreNote(3000,8000,1,43)+scoreNote(5000,18724,1,213,0)+scoreNote(5000,8000,1,47);
        for(auto* studio:{a.get(),b.get()})check(studio->configure(1,state,1) && studio->loadScore(score,36000,false) && studio->start(0,0),"per-string automation loads on native score clock");
        check(studioRender(*a,18000,480)==studioRender(*b,18000,137),"independent simultaneous string slides and returns are block invariant");
        check(a->trackVoices(1)==2 && a->strikes()==4,"independent score connections retain the two sustained strings");
        a->releaseTrack(1);studioRender(*a,4800);check(!a->trackVoices(1),"track release clears all per-string held notes");
        a->stop();studioRender(*a,4800);check(!a->trackVoices(1),"Stop clears all per-string state");
        check(!a->loadScore(scoreNote(0,1,1,212,128),36000,false) && !a->loadScore(scoreNote(0,1,1,213,128),36000,false),"tuning and explicit string automation cannot ramp");
        check(a->loadScore(score,36000,false) && a->start(0,20000.0/48000),"per-string score supports seeking");
        studioRender(*a,20000);check(!a->trackVoices(1),"seek cannot reconstruct unheard old string gates");
    }
    std::cout<<"PASS independent string allocation, tunings, owned returns, source compatibility, voice stealing and block-invariant native string automation\n";
}
