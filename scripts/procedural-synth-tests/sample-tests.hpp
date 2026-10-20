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
    std::cout << "PASS sample loading, identity, pitch, chords, one-shot/gated release, stop, end and control-thread reclamation\n";
}
