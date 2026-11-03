void attackSampleTests()
{
    using Sample=MWSound::ProceduralSample;
    const std::string hash(32,'a');
    const auto bank=[&](double hz) {
        auto data=std::make_shared<Sample>();data->hash=hash;data->pcm.resize(48000);
        for(unsigned n=0;n<data->pcm.size();++n)data->pcm[n]=float(.25*std::sin(n*2*3.141592653589793*hz/48000));
        data->zones={{60,72,1,127,60,0,1,0,48000}};data->status.store(Sample::Ready);return data;
    };
    auto base=bank(220),shortAttack=bank(330),dead=bank(440);
    {
        Synth normal,boosted;
        for(auto* s:{&normal,&boosted}){s->set("attack",.002f);s->set("sustain",1);s->set("volume",.1f);s->set("cutoff",18000);s->set("resonance",0);}
        check(normal.sample(base,60,false,hash) && boosted.sample(base,60,false,hash,{},2),"bounded recording input gain accepted");
        normal.noteOn(60,.8f);boosted.noteOn(60,.8f);
        const double ratio=energy(render(boosted,12000))/energy(render(normal,12000));
        check(ratio>3.95 && ratio<4.05,"unity-compatible input gain scales PCM before the amp");
        check(!normal.sample(base,60,false,hash,{},0) && !normal.sample(base,60,false,hash,{},9)
            && !normal.sample(base,60,false,hash,{},std::numeric_limits<float>::quiet_NaN()),"invalid source calibration gains reject");
    }
    Synth::AttackSamples variants{};variants[1]={shortAttack,60,false,hash};variants[2]={dead,60,false,hash};
    const auto setup=[&](Synth& synth) {
        synth.set("attack",.002f);synth.set("sustain",1);synth.set("release",.01f);synth.set("resonance",0);synth.set("cutoff",18000);
        check(synth.sample(base,60,false,hash,variants),"captured attack set accepted");render(synth,4800);
    };
    const auto crossings=[](const auto& pcm) {unsigned count=0;for(unsigned n=4801;n<pcm.size();++n)if(pcm[n-1]<=0 && pcm[n]>0)++count;return count;};
    Synth synth;setup(synth);
    for(auto entry:std::array<std::pair<int,int>,4>{{{0,44},{1,44},{2,66},{3,88}}})
    {
        synth.allNotesOff();render(synth,4800);synth.strike(60,.8f,{entry.first,-1,-1});
        check(std::abs(int(crossings(render(synth,14400)))-entry.second)<=2,"per-attack expression selects the actual recording");
    }
    synth.allNotesOff();render(synth,4800);synth.set("stringArticulation",2);synth.noteOn(60,.8f);
    check(std::abs(int(crossings(render(synth,14400)))-66)<=2,"patch articulation selects captured attack without a note override");
    synth.set("stringArticulation",1);synth.strike(64,.8f,{3,-1,-1});render(synth,480);
    check(synth.voices()==2,"different captured attacks coexist with independent immutable PCM cursors");
    synth.allNotesOff();render(synth,4800);check(!synth.voices(),"panic releases every captured attack");
    shortAttack->status.store(Sample::Loading);synth.strike(60,.8f,{2,-1,-1});
    check(energy(render(synth,4800))==0,"loading chosen recording never substitutes another sound");
    shortAttack->status.store(Sample::Ready);auto bad=variants;bad[1].hash=std::string(32,'b');
    synth.sample(base,60,false,hash,bad);render(synth,480);synth.strike(60,.8f,{2,-1,-1});
    check(energy(render(synth,4800))==0,"mismatched chosen attack stays silent");
    bad=variants;bad[1].oneShot=true;check(!synth.sample(base,60,false,hash,bad),"melodic attack variants cannot use one-shot semantics");
    check(!synth.sample(nullptr,60,false,"",variants),"variants require a base instrument");
    check(synth.sample(base,60,false,hash),"ordinary sample configuration clears captured mappings");render(synth,480);
    synth.strike(60,.8f,{2,-1,-1});check(energy(render(synth,4800))>0,"missing mapping retains existing modeled articulation");
    auto a=std::make_unique<NativeStudio>(),b=std::make_unique<NativeStudio>();setupStudio(*a);setupStudio(*b);
    // Companion value 3 = articulation 2; value 4 = articulation 3.
    const auto companion=[](unsigned frame,unsigned byte) {auto result=scoreNote(frame,byte,1,60,130);return result;};
    const auto score=companion(0,3)+scoreNote(0,4800,1,60,100)+companion(6000,4)+scoreNote(6000,4800,1,60,100);
    for(auto* studio:{a.get(),b.get()})check(studio->sample(1,base,60,false,hash,variants)
        && studio->loadScore(score,24000,true) && studio->start(0,0),"native score retains per-note sample choice");
    check(studioRender(*a,48000,480)==studioRender(*b,48000,137),"recorded attack playback and loops are block independent");
    a->stop();studioRender(*a,4800);check(!a->voices(),"Stop releases every selected bank");
    {
        Synth pinned;auto retired=bank(550);std::weak_ptr<Sample> weak=retired;
        Synth::AttackSamples owned{};owned[1]={retired,60,false,hash};pinned.sample(base,60,false,hash,owned);render(pinned,480);
        owned={};retired.reset();pinned.sample(base,60,false,hash);render(pinned,480);
        check(!weak.expired(),"retired variant PCM is never freed on the audio thread");
        pinned.sample(base,60,false,hash);check(weak.expired(),"later control call reclaims retired variant PCM");
    }
    std::cout<<"PASS recorded per-note attack selection, mixed voices, readiness/identity, legacy fallback, Stop/loop scheduling and PCM lifetime\n";
}
