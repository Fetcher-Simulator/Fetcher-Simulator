#include "../../apps/openmw/mwsound/proceduralsynth.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <fstream>
#include <string>
#include <cstdint>
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
void setupDrum(Synth& s, int type)
{
    s.set("drumType", static_cast<float>(type));
    s.set("bodyFrequency", type == 1 ? 55.f : type == 2 ? 190.f : 420.f);
    s.set("bodyDecay", type == 1 ? 0.45f : type == 2 ? 0.12f : type == 3 ? 0.04f : 0.35f);
    s.set("noiseDecay", type == 1 ? 0.025f : type == 2 ? 0.24f : type == 3 ? 0.065f : 0.65f);
    s.set("bodyLevel", type == 1 ? 0.95f : type == 2 ? 0.55f : 0.18f);
    s.set("noiseLevel", type == 1 ? 0.02f : type == 2 ? 0.85f : 0.7f);
    s.set("noiseHighpass", type <= 2 ? 1100.f : 6500.f);
    s.set("noiseLowpass", type <= 2 ? 12000.f : 16000.f);
    s.set("drumPitchAmount", type == 1 ? 30.f : type == 2 ? 7.f : 0.f);
    s.set("drumPitchDecay", type == 1 ? 0.022f : 0.018f);
    s.set("drumClick", type == 1 ? 0.12f : type == 2 ? 0.2f : 0.02f);
    s.set("drumDrive", type == 1 ? 1.6f : type == 2 ? 1.8f : 1.f);
    render(s, 4800); // settle control smoothing before the first hit
}
void writePreview(const std::string& path)
{
    Synth kick, snare, hat, open;
    setupDrum(kick,1);setupDrum(snare,2);setupDrum(hat,3);setupDrum(open,4);
    std::vector<float> mix(48000*8), a(1200), b(1200), c(1200), d(1200);
    for (unsigned pos=0;pos<mix.size();pos+=1200)
    {
        if (pos==0) kick.noteOn(36,1);
        if (pos==36000) snare.noteOn(38,1);
        if (pos==72000) hat.noteOn(42,1);
        if (pos==108000) open.noteOn(46,1);
        if (pos>=144000 && (pos-144000)%6000==0)
        {
            const auto step=(pos-144000)/6000;
            if (step%8==0) kick.noteOn(36,1);
            if (step%8==4) snare.noteOn(38,0.9f);
            if (step%2==0) { open.allNotesOff();hat.noteOn(42,step%4==0?0.8f:0.5f); }
            if (step%16==15) open.noteOn(46,0.7f);
        }
        kick.render(a.data(),1200);snare.render(b.data(),1200);hat.render(c.data(),1200);open.render(d.data(),1200);
        for (unsigned n=0;n<1200;++n) mix[pos+n]=a[n]+b[n]+c[n]+d[n];
    }
    float peak=0;for (float value:mix) peak=std::max(peak,std::abs(value));
    std::ofstream file(path,std::ios::binary);
    check(static_cast<bool>(file),"preview output opens");
    auto number=[&](std::uint32_t value,int bytes) { for(int i=0;i<bytes;++i) file.put(static_cast<char>((value>>(8*i))&255)); };
    file.write("RIFF",4);number(36+static_cast<std::uint32_t>(mix.size()*2),4);file.write("WAVEfmt ",8);
    number(16,4);number(1,2);number(1,2);number(48000,4);number(96000,4);number(2,2);number(16,2);
    file.write("data",4);number(static_cast<std::uint32_t>(mix.size()*2),4);
    for(float value:mix) number(static_cast<std::uint16_t>(static_cast<std::int16_t>(value/peak*0.8f*32767)),2);
    check(static_cast<bool>(file),"preview output completes");
}
#include "guitar-tests.hpp"
#include "studio-tests.hpp"

int main(int argc, char** argv)
{
    try
    {
        guitarTests();
        studioTests();
        Synth snareLow, snareHigh;
        setupDrum(snareLow,2);setupDrum(snareHigh,2);
        snareLow.noteOn(38,1);snareHigh.noteOn(84,1);
        const auto lowHit=render(snareLow,2400), highHit=render(snareHigh,2400);
        check(lowHit==highHit,"fixed drum tuning is independent of incoming MIDI note");
        snareLow.noteOff(38);snareHigh.noteOff(84);
        check(energy(render(snareLow,2400))>0.0000001,"early MIDI note-off cannot cut a drum tail");
        snareLow.allNotesOff();render(snareLow,2400);
        check(snareLow.voices()==0,"panic chokes one-shot drums");
        render(snareHigh,48000);check(snareHigh.voices()==0,"one-shot drum frees its voice without note-off");
        Synth rolls;setupDrum(rolls,2);rolls.set("polyphony",4);
        rolls.noteOn(38,1);render(rolls,1200);rolls.noteOn(38,1);render(rolls,1200);
        check(rolls.voices()==2,"repeated same-pitch drum hits retain overlapping tails");
        for(int n=0;n<20;++n) { rolls.noteOn(38,1);render(rolls,480); }
        check(rolls.voices()==4,"drum rolls obey the configured voice budget");
        rolls.allNotesOff();render(rolls,4800);check(rolls.voices()==0,"panic releases all overlapping drum hits");
        Synth shortNoise, longNoise;
        setupDrum(shortNoise,2);setupDrum(longNoise,2);
        for(auto* drum : {&shortNoise,&longNoise}) { drum->set("bodyLevel",0);drum->set("drumClick",0); }
        shortNoise.set("noiseDecay",0.08f);longNoise.set("noiseDecay",0.6f);
        render(shortNoise,4800);render(longNoise,4800);
        shortNoise.noteOn(38,1);longNoise.noteOn(38,1);
        render(shortNoise,7200);render(longNoise,7200);
        check(energy(render(longNoise,2400))>energy(render(shortNoise,2400))*100,"noise decay is independently controllable");
        for (int type=1;type<=4;++type)
        {
            Synth voice;setupDrum(voice,type);voice.noteOn(60,1);
            const auto hit=render(voice,4800);check(energy(hit)>0.000001,"each native drum type produces audio");
            float jump=0;for(std::size_t n=1;n<hit.size();++n) jump=std::max(jump,std::abs(hit[n]-hit[n-1]));
            check(jump<0.6f,"drum transient is bounded");
            render(voice,96000);check(voice.voices()==0,"all drum types decay to silence");
        }
        Synth extremes;setupDrum(extremes,4);
        extremes.set("bodyFrequency",1000);extremes.set("drumPitchAmount",48);extremes.set("drumDrive",8);
        extremes.set("noiseHighpass",16000);extremes.set("noiseLowpass",200);
        extremes.noteOn(127,1);render(extremes,4800);extremes.allNotesOff();render(extremes,4800);
        check(extremes.voices()==0,"extreme drum controls stay finite and panic-safe");
        std::cout << "PASS native drums, fixed tuning, one-shot tails, independent decays and choke\n";
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
        if (argc==3 && std::string(argv[1])=="--preview") writePreview(argv[2]);
        if (argc==4 && std::string(argv[1])=="--bank-preview") writeBankPreview(argv[2],argv[3]);
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
