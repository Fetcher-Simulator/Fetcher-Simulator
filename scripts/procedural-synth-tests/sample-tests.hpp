void sampleTests()
{
    using Sample = MWSound::ProceduralSample;
    const std::string hash(32, 'a');
    auto data = std::make_shared<Sample>(); data->hash = hash;
    data->pcm.resize(48000);
    for (std::size_t i = 0; i < data->pcm.size(); ++i)
        data->pcm[i] = static_cast<float>(std::sin(i * 2 * 3.141592653589793 * 220 / 48000));
    const auto setup = [&](Synth& s, bool oneShot, std::string fingerprint) {
        s.set("attack", .002f); s.set("sustain", 1); s.set("release", .02f); s.set("resonance", 0);
        s.set("cutoff", 18000); return s.sample(data, 60, oneShot, fingerprint);
    };
    Synth loading;
    check(setup(loading, false, hash), "sample settings accepted");
    loading.noteOn(60, 1);
    check(energy(render(loading, 480)) == 0 && !loading.voices(), "loading sample never falls back to oscillator");
    data->status.store(Sample::Ready, std::memory_order_release);
    loading.noteOn(60, 1);
    check(energy(render(loading, 4800)) > .001, "ready publication plays next note");
    loading.noteOff(60); render(loading, 4800);
    check(!loading.voices(), "gated sample follows release");
    Synth oneShot;
    setup(oneShot, true, hash); oneShot.noteOn(60, 1); render(oneShot, 480);
    oneShot.noteOff(60); render(oneShot, 4800);
    check(oneShot.voices() == 1, "one-shot survives MIDI gate");
    oneShot.noteOn(60, .5f); render(oneShot, 480);
    check(oneShot.voices() == 2, "one-shot retriggers overlap");
    oneShot.allNotesOff(); render(oneShot, 2400);
    check(!oneShot.voices(), "panic stops one-shot tails");
    const auto crossings = [&](int pitch) {
        Synth s; setup(s, false, hash); s.noteOn(pitch, 1); const auto pcm = render(s, 24000);
        unsigned n = 0; for (std::size_t i = 4801; i < pcm.size(); ++i) if (pcm[i-1] <= 0 && pcm[i] > 0) ++n;
        return n;
    };
    check(std::abs(static_cast<int>(crossings(60)) - 88) <= 1
        && std::abs(static_cast<int>(crossings(72)) - 176) <= 1, "root pitch and octave sample transposition");
    Synth mismatch; setup(mismatch, false, std::string(32, 'b')); mismatch.noteOn(60, 1);
    check(energy(render(mismatch, 4800)) == 0 && !mismatch.voices(), "mismatched asset stays silent");
    Synth chord; setup(chord, false, hash);
    for (int pitch : {60, 64, 67}) chord.noteOn(pitch, .8f);
    render(chord, 4800); check(chord.voices() == 3, "sample chord uses independent cursors");
    setup(chord, false, std::string(32, 'b')); render(chord, 480);
    check(!chord.voices(), "changing asset identity clears old sample voices");
    setup(chord, false, hash); chord.noteOn(60, 1); render(chord, 96000);
    check(!chord.voices(), "gated voice retires at sample end without note-off");
    auto retired = std::make_shared<Sample>(); retired->hash = hash; retired->pcm = data->pcm;
    retired->status.store(Sample::Ready); std::weak_ptr<Sample> weak = retired;
    Synth pin; pin.sample(retired, 60, false, hash); retired.reset(); render(pin, 1);
    pin.sample(nullptr, 60, false, ""); render(pin, 1);
    check(!weak.expired(), "callback never frees retired PCM");
    pin.sample(nullptr, 60, false, "");
    check(weak.expired(), "next control call reclaims retired PCM");
    auto a = std::make_unique<NativeStudio>(), b = std::make_unique<NativeStudio>();
    setupStudio(*a); setupStudio(*b);
    a->sample(1, data, 60, true, hash); b->sample(1, data, 60, true, hash);
    const auto score = scoreNote(0, 100, 1, 60) + scoreNote(6000, 100, 1, 72);
    for (auto* studio : {a.get(), b.get()})
        check(studio->loadScore(score, 24000, true) && studio->start(0, 0), "sample native score accepted");
    const auto whole = studioRender(*a, 24000, 480), split = studioRender(*b, 24000, 137);
    check(whole == split && energy(whole) > .001, "sample score timing independent of decoder block size");
    a->stop(); studioRender(*a, 4800); check(!a->voices(), "Studio Stop releases sample one-shots");
    using Playback=MWSound::ProceduralSamplePlayback;
    Playback loop;loop.loopStart=.1;loop.loopEnd=.3;loop.crossfade=.02;
    auto sustained=std::make_unique<Synth>();setup(*sustained,false,hash);
    check(sustained->sample(data,60,false,hash,{},1,loop),"bounded raw loop settings accepted");
    sustained->noteOn(72,.8f);render(*sustained,144000);
    check(sustained->voices()==1 && energy(render(*sustained,4800))>.001,"short excerpt sustains for seconds at transposed pitch");
    sustained->noteOff(72);render(*sustained,9600);check(!sustained->voices(),"loop continues smoothly through the envelope then retires on note-off");
    sustained->noteOn(127,.8f);render(*sustained,48000);check(sustained->voices()==1,"large cursor increments remain inside the loop");
    sustained->allNotesOff();render(*sustained,4800);check(!sustained->voices(),"Stop clears a looping voice");
    Playback reverse;reverse.reverse=true;
    std::vector<float> ramp(4800);for(unsigned n=0;n<ramp.size();++n)ramp[n]=float(n)/ramp.size();
    double pos=600.25;bool wrapped=false;
    check(std::abs(reverse.read(ramp,0,unsigned(ramp.size()),pos,1,wrapped)-(ramp.size()-1-pos)/ramp.size())<1e-6,"reverse interpolation reads the source backwards at fractional positions");
    Playback seam;seam.loopStart=0;seam.loopEnd=.08;seam.crossfade=.01;
    pos=.08*48000-1;double before=seam.read(ramp,0,unsigned(ramp.size()),pos,1,wrapped);++pos;
    double after=seam.read(ramp,0,unsigned(ramp.size()),pos,1,wrapped);
    check(wrapped && std::abs(after-before)<.003,"linear crossfade joins mismatched loop endpoints without a hard step");
    for(unsigned n=0;n<10000;++n){pos+=23456.7;check(std::isfinite(seam.read(ramp,0,unsigned(ramp.size()),pos,23456.7,wrapped)) && pos<3840,"many wraps per frame stay finite and bounded");}
    Playback invalid=loop;invalid.loopEnd=2;
    check(sustained->sample(data,60,false,hash,{},1,invalid),"bounded metadata binds independently of per-listener decoded length");
    sustained->noteOn(60,1);check(energy(render(*sustained,4800))==0 && !sustained->voices(),"loop past decoded audio is silent without interrupting unrelated parts");
    invalid=loop;invalid.crossfade=.2;check(!invalid.valid(),"crossfade cannot consume more than half the loop");
    check(!sustained->sample(data,60,true,hash,{},1,loop),"one-shot cannot create a nonterminating loop");
    auto pending=std::make_shared<Sample>();pending->hash=hash;
    check(sustained->sample(pending,60,false,hash,{},1,loop),"loading loop source may bind before publication");
    pending->pcm.resize(20);pending->status.store(Sample::Ready,std::memory_order_release);
    sustained->noteOn(60,1);check(energy(render(*sustained,4800))==0 && !sustained->voices(),"late shorter decode never reads invalid loop points or falls back to oscillators");
    data->buildWaveform();check(data->waveformReady && data->waveMin.size()==256,"loader waveform has a fixed bounded size");
    for(unsigned n=0;n<256;++n)check(data->waveMin[n]<=data->waveMax[n] && data->waveMin[n]>=-1 && data->waveMax[n]<=1,"waveform bins retain bounded amplitude extrema");
    for(auto* studio:{a.get(),b.get()})
    {
        studio->stop();check(studio->sample(1,data,60,false,hash,{},1,loop),"native sequencer accepts looped sample reference");
        check(studio->loadScore(scoreNote(0,90000,1,60),96000,false) && studio->start(0,0),"long sample gate score accepted");
    }
    check(studioRender(*a,60000,480)==studioRender(*b,60000,137),"loop seams remain identical across callback chunk sizes");
    a->stop();studioRender(*a,480);check(!a->voices(),"sequencer Stop clears loop gate on the audio callback");
    Sample waveform;waveform.pcm={-.8f,-.4f,.4f,.8f};
    auto view=waveform.view(0,4.0/48000,false),reverseView=waveform.view(0,4.0/48000,true);
    check(view && reverseView && view->minimum[0]==-.8f && reverseView->minimum[0]==.8f && view->maximum[255]==.8f,"sub-bin waveform zoom remains bounded and reverses playback order");
    auto zoom=waveform.view(1.0/48000,3.0/48000,false);
    check(zoom && zoom->minimum[0]==-.4f && zoom->maximum[255]==.4f,"zoom recalculates extrema from source PCM rather than enlarging old peaks");
    auto cross=waveform.zeroCrossing(2.0/48000,false),reverseCross=waveform.zeroCrossing(2.0/48000,true);
    check(cross && reverseCross && std::abs(*cross-1.5/48000)<1e-10 && std::abs(*reverseCross-1.5/48000)<1e-10,"zero crossing interpolates in displayed playback direction");
    check(!waveform.view(-1,.1,false) && !waveform.view(0,1,false) && !waveform.view(.1,.1,false) && !waveform.zeroCrossing(-1,false),"waveform editor rejects invalid ranges");
    waveform.zones.push_back({});check(!waveform.view(0,4.0/48000,false) && !waveform.zeroCrossing(0,false),"multisample banks cannot be edited as arbitrary raw excerpts");
    waveform.zones.clear();waveform.pcm.assign(48000,.5f);check(!waveform.zeroCrossing(.5,false),"zero snap cannot jump beyond its five-millisecond neighborhood");
    std::cout << "PASS sample loading, identity, pitch, chords, one-shot/gated release, stop, end and control-thread reclamation\n";
    std::cout << "PASS reverse interpolation, bounded sustain loops/crossfades, transposition, late decode rejection, waveform and native scheduling\n";
}
