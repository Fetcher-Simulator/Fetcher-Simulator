void parallelTests()
{
    using Effects=MWSound::ProceduralSynthEffects;
    constexpr double pi=3.14159265358979323846;
    {
        Effects legacy,disabled,clean;
        Effects::Settings a,b,c;a.cabinet=4;a.mode=2;a.drive=26;b=a;
        b.parallel.drive=48;b.parallel.noise=1;b.parallel.feedback=.85;b.parallel.lift=18;
        c.cabinet=4;c.parallel.mix=1;c.parallel.clean=1;c.parallel.dirty=0;
        Effects plain;Effects::Settings plainSettings;plainSettings.cabinet=4;
        double error=0;
        for(unsigned n=0;n<24000;++n)
        {
            const double input=.3*std::sin(2*pi*997*n/48000);
            check(legacy.process(input,a)==disabled.process(input,b),"disabled parallel controls retain exact legacy PCM");
            error=std::max(error,std::abs(clean.process(input,c)-plain.process(input,plainSettings)));
        }
        check(error<1e-12,"parallel clean and normal amp paths have identical FIR latency and polarity");
    }
    std::array<double,4> signatures{};
    for(int mode=0;mode<4;++mode)
    {
        Effects fx;Effects::Settings s;s.parallel.mix=1;s.parallel.clean=.3;s.parallel.dirty=.8;
        s.parallel.shape=mode;s.parallel.drive=36;s.parallel.noise=.2;
        for(unsigned n=0;n<48000;++n)
        {
            const double out=fx.process(.6*std::sin(2*pi*211*n/48000),s);
            check(std::isfinite(out) && std::abs(out)<8,"every parallel shaper has bounded finite output");
            if(n>12000)signatures[mode]+=out*out;
        }
    }
    for(unsigned a=0;a<4;++a)for(unsigned b=a+1;b<4;++b)
        check(std::abs(signatures[a]-signatures[b])>1,"soft, hard, folding and rectified layers are distinct");
    {
        Effects fx;Effects::Settings s;s.parallel.mix=1;s.parallel.clean=0;s.parallel.dirty=1;
        s.parallel.shape=1;s.parallel.drive=30;
        std::vector<float> processed(48000),naive(48000);
        const double gain=std::pow(10.0,30.0/20);
        for(unsigned n=0;n<52800;++n)
        {
            const double input=std::sin(2*pi*9000*n/48000),out=fx.process(input,s);
            if(n>=4800){processed[n-4800]=float(out);naive[n-4800]=float(std::clamp(input*.18*gain,-1.0,1.0)/(.18*std::sqrt(gain)));}
        }
        const double alias=magnitude(processed,3000)/magnitude(processed,9000);
        const double unfiltered=magnitude(naive,3000)/magnitude(naive,9000);
        check(alias<unfiltered*.2,"parallel oversampling suppresses the folded fifth harmonic by more than 14 dB");
        Effects toggled,continued;Effects::Settings p;p.cabinet=1;p.parallel.drive=24;p.parallel.shape=2;
        double edgeDifference=0;
        for(unsigned n=0;n<24000;++n)
        {
            if(n==4000)p.parallel.mix=1;if(n==16000)p.parallel.mix=0;
            const double input=.4*std::sin(2*pi*997*n/48000),out=toggled.process(input,p);
            auto reference=p;
            if(n==4000)reference.parallel.mix=0;if(n==16000)reference.parallel.mix=1;
            const double unchanged=continued.process(input,reference);
            if(n==4000 || n==16000)edgeDifference=std::max(edgeDifference,std::abs(out-unchanged));
        }
        check(edgeDifference<.01,"parallel enable/disable fades instead of jumping to the new waveform");
    }
    {
        Effects fx;Effects::Settings s;s.parallel.mix=1;s.parallel.noise=1;s.parallel.comb=1;
        s.parallel.depth=1;s.parallel.rate=12;s.parallel.feedback=.85;s.parallel.lift=18;
        for(unsigned n=0;n<24000;++n)check(fx.process(0,s)==0,"following noise and lift never start sound without a source");
        double tail=0;
        for(unsigned n=0;n<240000;++n)
        {
            if(n==24000)s.parallel.feedback=-.85;
            if(n==36000){s.parallel.delay=.5;s.parallel.shape=3;}
            const double input=n<48000?.5*std::sin(n*.04):0;
            const double out=fx.process(input,s);
            check(std::isfinite(out) && std::abs(out)<16,"maximum modulated comb feedback/lift remains stable");
            if(n>230000)tail=std::max(tail,std::abs(out));
        }
        check(tail<1e-12,"comb, following noise and lifted tails drain to silence");
        s.parallel.mix=0;
        for(unsigned n=0;n<24000;++n)fx.process(0,s);
        s.parallel.mix=1;
        for(unsigned n=0;n<12000;++n)check(fx.process(0,s)==0,"reenabling parallel never replays cleared history");
    }
    {
        const auto level=[&](double amplitude,double lift) {
            Effects fx;Effects::Settings s;s.parallel.mix=1;s.parallel.clean=1;s.parallel.dirty=0;s.parallel.lift=lift;
            double sum=0;
            for(unsigned n=0;n<48000;++n){const double out=fx.process(amplitude*std::sin(n*.1),s);if(n>24000)sum+=out*out;}
            return sum;
        };
        check(level(.01,18)>level(.01,0)*4,"upward compression lifts quiet material");
        check(std::abs(level(.9,18)/level(.9,0)-1)<.01,"upward compression leaves loud material unchanged");
        Effects staticComb,movingComb;Effects::Settings s;s.parallel.mix=1;s.parallel.comb=.8;
        double difference=0;
        for(unsigned n=0;n<48000;++n)
        {
            const double input=.3*std::sin(n*.1);const auto plain=staticComb.process(input,s);
            s.parallel.depth=.7;const auto moving=movingComb.process(input,s);s.parallel.depth=0;
            if(n>12000)difference+=std::abs(plain-moving);
        }
        check(difference>10,"comb modulation changes tone without stereo-dependent routing");
    }
    {
        using Studio=MWSound::ProceduralStudio;
        auto a=std::make_unique<Studio>(),b=std::make_unique<Studio>(),raw=std::make_unique<Studio>();
        for(auto* studio:{a.get(),b.get(),raw.get()})setupStudio(*studio);
        const auto notes=scoreNote(0,20000,1,48)+scoreNote(12000,20000,1,55);
        const auto controls=scoreNote(0,32768,1,198,128)+scoreNote(0,43690,1,203,0)
            +scoreNote(0,32768,1,209,0)+scoreNote(12000,65535,1,198,0)+scoreNote(12000,0,1,203,0);
        // Keep controls/notes ordered by frame as project compilation does.
        const auto score=controls.substr(0,33)+notes.substr(0,11)+controls.substr(33)+notes.substr(11);
        for(auto* studio:{a.get(),b.get()})check(studio->loadScore(score,36000,false) && studio->start(0,0),"parallel score controls load");
        raw->loadScore(notes,36000,false);raw->start(0,0);
        auto pcm=studioRender(*a,36000,480);
        check(pcm==studioRender(*b,36000,137),"parallel noise/modulation/control rendering is block invariant");
        check(pcm!=studioRender(*raw,36000,480) && a->strikes()==2,"parallel changes tone while retaining scheduled note strikes");
        check(!a->loadScore(scoreNote(0,1,1,203,128),36000,false),"waveshaper modes cannot ramp continuously");
        check(!a->loadScore(scoreNote(0,1,1,212,0),36000,false),"unknown parallel control rejected");
    }
    {
        std::vector<std::unique_ptr<Synth>> band;
        for(unsigned n=0;n<8;++n)
        {
            auto s=std::make_unique<Synth>();s->set("cabinet",4);s->set("parallelMix",1);s->set("parallelShape",2);
            s->set("parallelComb",.7f);s->set("parallelDepth",.4f);s->set("parallelNoise",.2f);s->set("parallelLift",12);
            for(int pitch=40;pitch<48;++pitch)s->noteOn(pitch,.6f);
            band.push_back(std::move(s));
        }
        const auto start=std::chrono::steady_clock::now();
        for(auto& s:band)render(*s,48000);
        std::cout<<"8 instruments x 8 voices with parallel layers, comb, lift and IR: "
            <<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<" seconds / second PCM (informational)\n";
    }
    std::cout<<"PASS parallel legacy bypass, aligned clean path, distinct shapers, gated noise, stable tails, lift, mono modulation and score automation\n";
}
