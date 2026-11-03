void noteExpressionTests()
{
    const auto configure=[](NativeStudio& s) {
        Synth::State state;for(unsigned i=0;i<state.size();++i)state[i]=Synth::Parameters[i].initial;
        for(unsigned i=0;i<state.size();++i)
        {
            if(std::string_view(Synth::Parameters[i].name)=="stringLayout")state[i]=1;
            if(std::string_view(Synth::Parameters[i].name)=="stringLegato")state[i]=2;
        }
        state[9]=.002f;state[11]=1;state[12]=.01f;
        check(s.configure(1,state,1) && s.configure(2,state,1),"expression score tracks configured");
    };
    for(unsigned code=0;code<180;++code)
    {
        const auto e=Synth::Strike::fromByte(code);
        check(e.valid() && unsigned(e.articulation+1+5*(e.selection+1)+45*(e.direction+1))==code,"all 180 mixed-radix expression values decode without collisions");
    }
    const auto paired=[](unsigned at,int note,unsigned code,unsigned gate=12000){return scoreNote(at,code,1,note,130)+scoreNote(at,gate,1,note);};
    const auto score=paired(0,45,57)+paired(0,47,109);
    {
        auto a=std::make_unique<NativeStudio>(),b=std::make_unique<NativeStudio>();configure(*a);configure(*b);
        check(a->loadScore(score,24000,false) && a->start(0,0),"same-frame mixed-articulation score loads");
        b->strike(1,45,100/127.f,{1,1,0});b->strike(1,47,100/127.f,{3,2,1});
        check(studioRender(*a,480)==studioRender(*b,480),"recorded per-note attacks reproduce their live expression snapshots exactly");
        check(a->strikes()==2 && a->trackVoices(1)==2,"companions do not create attacks or merge independent strings");
        a->stop();studioRender(*a,4800);check(!a->trackVoices(1),"Stop releases expressive score notes");
        for(const auto& bad:std::vector<std::string>{scoreNote(0,57,1,45,130),
            scoreNote(0,57,1,45,130)+scoreNote(0,12000,1,47),
            scoreNote(0,57,1,45,130)+scoreNote(1,12000,1,45),
            scoreNote(0,57,1,45,130)+scoreNote(0,12000,2,45),
            scoreNote(0,57,1,45,130)+scoreNote(0,100,1,141,0)+scoreNote(0,12000,1,45),
            scoreNote(0,57,1,45,130)+paired(0,45,57),paired(0,45,180),paired(0,45,0),scoreNote(0,57,1,141,130)})
            check(!a->loadScore(bad,24000,false),"orphaned, mismatched, duplicate and malformed expression records reject");
    }
    {
        using Sample=MWSound::ProceduralSample;auto bank=std::make_shared<Sample>();bank->hash=std::string(32,'c');
        for(unsigned n=0;n<96000;++n)bank->pcm.push_back(float(.2*std::sin(2*3.141592653589793*261.625565*n/48000)));
        bank->zones={{0,127,1,127,60,0,1,0,96000}};bank->status.store(Sample::Ready);
        auto a=std::make_unique<NativeStudio>(),b=std::make_unique<NativeStudio>();configure(*a);configure(*b);
        for(auto* s:{a.get(),b.get()})check(s->sample(1,bank,60,false,bank->hash),"expressive recorded bank configured");
        check(a->loadScore(score,24000,false) && a->start(0,0),"recorded-source expressive score starts");
        b->strike(1,45,100/127.f,{1,1,0});b->strike(1,47,100/127.f,{3,2,1});
        check(studioRender(*a,480)==studioRender(*b,480),"gated recorded banks also reproduce live mixed-articulation attacks exactly");
    }
    {
        auto a=std::make_unique<NativeStudio>(),b=std::make_unique<NativeStudio>();configure(*a);configure(*b);
        const auto loop=paired(0,45,57,18000)+paired(6000,48,109,6000);
        for(auto* s:{a.get(),b.get()})check(s->loadScore(loop,24000,true) && s->start(0,0),"expressive loop loads");
        check(studioRender(*a,72000,480)==studioRender(*b,72000,137),"per-note expression and cross-loop gates are block invariant");
        a->stop();studioRender(*a,4800);check(!a->trackVoices(1),"looped expression Stop clears held ownership");
        check(a->loadScore(paired(8000,45,57),24000,false) && a->start(0,6000.0/48000),"seeking before a future expressive attack succeeds");
        studioRender(*a,3000);check(a->strikes()==7 && a->trackVoices(1)==1,"seek retains the future note's attached expression without replaying a state marker");
    }
    {
        auto s=std::make_unique<NativeStudio>();configure(*s);std::string burst;
        for(unsigned n=0;n<512;++n)burst+=paired(0,int(n%128),1,64);
        check(s->loadScore(burst,48000,false) && s->start(0,0),"512-note expressive burst remains within the existing note-rate budget");
        studioRender(*s,1000);check(s->strikes()==512,"metadata is excluded from rate limiting and audible strike counts");
    }
    std::cout<<"PASS saved per-note/live PCM equivalence, expression record validation, exact attack counts, loop/seek/Stop and block-invariant playback\n";
}
