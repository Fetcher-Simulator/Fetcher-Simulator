#include "../../apps/openmw/mwsound/proceduralsynth.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

using Synth = MWSound::ProceduralSynth;
void check(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}
std::vector<float> render(Synth& s, unsigned frames)
{
    std::vector<float> result(frames);
    for (unsigned offset = 0; offset < frames; offset += 480)
        s.render(result.data() + offset, std::min(480u, frames - offset));
    for (float f : result)
        check(std::isfinite(f) && std::abs(f) <= 1, "finite bounded PCM");
    return result;
}
double energy(const std::vector<float>& buffer)
{
    double sum = 0;
    for (float f : buffer)
        sum += f * f;
    return sum / buffer.size();
}
int main()
{
    try
    {
        Synth drum;
        check(drum.set("pitchAmount",36) && drum.set("pitchDecay",0.025f), "drum pitch envelope controls");
        check(!drum.set("pitchDecay",0) && !drum.set("wave1",5), "new controls bounded");
        drum.set("wave1",4); drum.set("sustain",0); drum.set("decay",0.035f); drum.noteOn(60,1);
        check(energy(render(drum,2400))>0.00001,"native noise percussion renders");
        render(drum,48000);check(energy(render(drum,4800))<0.000001,"percussion envelope decays");
        Synth kick;
        kick.set("pitchAmount",24);kick.set("pitchDecay",0.04f);kick.set("attack",0.002f);kick.set("sustain",1);
        kick.set("cutoff",18000);kick.noteOn(36,1);
        const auto first = render(kick,4800);
        const auto later = render(kick,4800);
        auto crossingsOf = [](const auto& samples) {
            unsigned count=0;
            for (std::size_t i=1;i<samples.size();++i)
                if (samples[i-1]<=0 && samples[i]>0) ++count;
            return count;
        };
        check(crossingsOf(first)>crossingsOf(later)+2 && crossingsOf(later)>=6 && crossingsOf(later)<=8,
            "kick pitch envelope falls toward its fundamental");
        Synth s;
        check(energy(render(s, 4800)) == 0, "idle silence");
        check(!s.set("cutoff", std::numeric_limits<float>::quiet_NaN()), "NaN rejected");
        check(!s.set("gain", std::numeric_limits<float>::infinity()), "infinity rejected");
        check(!s.set("unknown", 0) && !s.set("wave1", 1.5f) && !s.set("cutoff", 0), "invalid controls rejected");
        check(!s.noteOn(-1, 1) && !s.noteOn(128, 1) && !s.noteOn(60, -1) && !s.noteOn(60, 2), "invalid notes rejected");
        check(s.noteOn(69, 1), "note accepted");
        render(s, 48000);
        auto tone = render(s, 48000);
        unsigned crossings = 0;
        for (std::size_t i = 1; i < tone.size(); ++i)
            if (tone[i - 1] <= 0 && tone[i] > 0)
                ++crossings;
        check(crossings >= 439 && crossings <= 441, "A4 frequency");
        check(energy(tone) > 0.001 && s.voices() == 1, "held note sustains");
        s.set("wave1", 2);
        s.set("cutoff", 100);
        render(s, 4800);
        check(energy(render(s, 4800)) < energy(tone) * 0.1, "live low-pass changes held tone");
        s.noteOff(69);
        render(s, 48000);
        check(s.voices() == 0 && energy(render(s, 4800)) == 0, "release reaches silence");
        std::cout << "PASS validation, pitch, sustain, live filter, release\n";
        for (int waveform = 0; waveform < 5; ++waveform)
        {
            Synth chord;
            chord.set("wave1", static_cast<float>(waveform));
            chord.set("enabled2", 1);
            for (int n = 48; n < 64; ++n)
                chord.noteOn(n, 0.8f);
            check(energy(render(chord, 4800)) > 0.0001 && chord.voices() == 16, "16 voice chord");
            for (int n = 64; n < 96; ++n)
                chord.noteOn(n, 1);
            render(chord, 4800);
            check(chord.voices() == 16, "voice stealing bounded");
            chord.allNotesOff();
            render(chord, 48000);
            check(chord.voices() == 0, "panic releases stolen notes");
        }
        std::cout << "PASS four waveforms, two oscillators, 16 voices, stealing, panic\n";
        Synth bounded;
        for (int n = 0; n < 10000; ++n)
            bounded.noteOn(n % 128, 1);
        bounded.allNotesOff();
        render(bounded, 48000);
        check(bounded.voices() == 0, "queue overflow cannot stick notes");
        bounded.noteOn(60, 1);
        render(bounded, 480);
        bounded.close();
        check(energy(render(bounded, 4800)) == 0 && !bounded.noteOn(60, 1), "shutdown silence");
        Synth concurrent;
        std::thread producer([&] {
            for (int i = 0; i < 10000; ++i)
            {
                concurrent.set("cutoff", static_cast<float>(100 + i % 10000));
                concurrent.noteOn(i % 128, 0.8f);
                concurrent.noteOff(i % 128);
            }
        });
        render(concurrent, 96000);
        producer.join();
        concurrent.allNotesOff();
        render(concurrent, 48000);
        check(concurrent.voices() == 0, "concurrent controls finish silent");
        std::cout << "PASS queue overflow, shutdown, concurrent control/audio\n";
        Synth perf;
        perf.set("enabled2", 1);
        perf.set("wave1", 1);
        perf.set("wave2", 1);
        for (int n = 48; n < 64; ++n)
            perf.noteOn(n, 0.5f);
        const auto start = std::chrono::steady_clock::now();
        render(perf, 96000);
        const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout << "16-voice dual-triangle render: " << elapsed << " seconds for 2 seconds PCM\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
