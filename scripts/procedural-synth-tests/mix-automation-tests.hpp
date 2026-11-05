void mixAutomationTests()
{
    {
        Mixer mixer;auto state=Mixer::defaults();mixer.target(state,.2f);
        mixer.automate(0,.75,0,true);
        check(std::abs(mixer.process(1)-.75f)<1e-7,"fader automation is absolute rather than multiplied by the saved fader");
        mixer.automate(1,1,0,true);mixer.automate(2,.5,0,true);mixer.process(1);
        check(mixer.delaySend()==1 && mixer.reverbSend()==.5f,"send chase sets independent post-fader sends");
        mixer.target(state,.2f,false);for(unsigned n=0;n<16000;++n)mixer.process(1);
        check(mixer.process(1)==0,"separate Mute/Solo gate wins over fader automation");
        mixer.target(state,.2f,true);for(unsigned n=0;n<16000;++n)mixer.process(1);
        check(std::abs(mixer.process(1)-.75f)<1e-6,"unmute restores the automated fader rather than the base fader");
        mixer.target(state,.4f);for(unsigned n=0;n<16000;++n)mixer.process(1);
        mixer.automate(0,1,0,false);
        check(std::abs(mixer.process(1)-.4f)<1e-6,"a changed live fader takes over its active curve");
        mixer.resetAutomation();mixer.automate(0,1,0,true);
        check(mixer.process(1)==1,"seek/loop chase releases manual takeover");
        mixer.resetAutomation();for(unsigned n=0;n<16000;++n)mixer.process(1);
        check(std::abs(mixer.process(1)-.4f)<1e-6 && mixer.delaySend()<1e-7 && mixer.reverbSend()<1e-7,"Stop restores the saved fader and send targets");
    }
    auto patch=Synth::State{};for(unsigned i=0;i<patch.size();++i)patch[i]=Synth::Parameters[i].initial;
    patch[9]=.002f;patch[11]=1;patch[12]=.01f;
    const auto score=scoreNote(0,47000,1,60)+scoreNote(0,0,1,214,128)+scoreNote(18000,65535,1,214,128)
        +scoreNote(36000,10000,1,214,0)+scoreNote(0,65535,1,215,128)+scoreNote(24000,0,1,215,0)
        +scoreNote(0,0,1,216,128)+scoreNote(24000,65535,1,216,0);
    // Controls and notes share the normal globally sorted transfer order.
    auto sorted=[](std::string bytes) {
        std::vector<std::string> records;for(unsigned i=0;i<bytes.size();i+=11)records.push_back(bytes.substr(i,11));
        auto frame=[](const std::string& s){unsigned n=0;for(unsigned i=0;i<4;++i)n|=static_cast<unsigned char>(s[i])<<(8*i);return n;};
        std::stable_sort(records.begin(),records.end(),[&](const auto& a,const auto& b){return frame(a)<frame(b);});
        bytes.clear();for(const auto& r:records)bytes+=r;return bytes;
    };
    {
        auto a=std::make_unique<NativeStudio>(),b=std::make_unique<NativeStudio>();
        for(auto* studio:{a.get(),b.get()})
        {
            check(studio->configure(1,patch,.31f),"fader test track configures");
            auto effects=MWSound::ProceduralReturns::defaults();effects[1]=80;
            check(studio->effects(effects,.7f) && studio->loadScore(sorted(score),48000,true) && studio->start(0,0),"level and both send curves load with shared returns");
        }
        const auto pcm=studioRender(*a,144000,480),irregular=studioRender(*b,144000,137);
        check(pcm==irregular && energy(pcm)>0 && a->strikes()==3,"fader/send ramps and return tails are block invariant across loops without extra strikes");
    }
    {
        auto muted=std::make_unique<NativeStudio>(),zero=std::make_unique<NativeStudio>(),full=std::make_unique<NativeStudio>();
        const auto level=scoreNote(0,20000,1,60)+scoreNote(0,65535,1,214,0);
        check(muted->configure(1,patch,.3f,false) && zero->configure(1,patch,0) && full->configure(1,patch,1),"mute and zero-level tracks configure independently");
        for(auto* studio:{muted.get(),zero.get(),full.get()})check(studio->loadScore(level,24000,false) && studio->start(0,0),"absolute level score starts");
        check(energy(studioRender(*muted,24000))==0,"score cannot unmute an inaudible track");
        check(studioRender(*zero,24000)==studioRender(*full,24000),"recorded fader can fade up from a saved zero without squared gain");
    }
    {
        auto a=std::make_unique<NativeStudio>();check(a->configure(1,patch,.6f),"chase test configures");
        const auto silent=scoreNote(0,0,1,214,0)+scoreNote(12000,11999,1,60);
        check(a->loadScore(silent,24000,false) && a->start(0,.2),"late listener seeks into muted fader curve");
        check(energy(studioRender(*a,6000))==0,"late chase applies fader before the first future note");
        const auto dry=scoreNote(0,22000,1,60);
        check(a->loadScore(dry,24000,false) && a->replace(0),"replacement without mix lanes queues");
        studioRender(*a,5000);
        check(energy(studioRender(*a,1000))>0,"replacement releases a removed fader curve to the saved base");
        a->stop();studioRender(*a,10000);a->noteOn(1,60,1);
        check(energy(studioRender(*a,12000))>0,"Stop clears fader override for manual notes");
        check(!a->loadScore(scoreNote(0,1,1,220,0),24000,false)
            && !a->loadScore(scoreNote(0,1,1,214,129),24000,false)
            && !a->loadScore(scoreNote(0,65536,1,215,128),24000,false)
            && !a->loadScore(scoreNote(0,1,1,216,0)+scoreNote(0,2,1,216,128),24000,false),"unknown, program, oversized and duplicate mix controls reject");
    }
    std::cout<<"PASS native absolute track faders, independent send ramps, mute gate, live takeover, chase/Stop, bounded parsing and block-invariant PCM\n";
}
