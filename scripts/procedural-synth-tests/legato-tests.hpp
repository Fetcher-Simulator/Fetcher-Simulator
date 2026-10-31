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
        check(!a->loadScore(scoreNote(0,1,1,212,0),36000,false),"unknown connected control rejected");
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
