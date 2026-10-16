#include "proceduralsynth.hpp"

#include <algorithm>
#include <cmath>

namespace MWSound
{
    const std::array<ProceduralSynth::Parameter, ProceduralSynth::ParameterCount> ProceduralSynth::Parameters{ {
        { "wave1", 0, 3, 0, true },
        { "volume1", 0, 1, 0.8f },
        { "tune1", -24, 24, 0 },
        { "fine1", -100, 100, 0 },
        { "enabled2", 0, 1, 0, true },
        { "wave2", 0, 3, 2, true },
        { "volume2", 0, 1, 0.4f },
        { "tune2", -24, 24, 0 },
        { "fine2", -100, 100, 7 },
        { "attack", 0.002f, 5, 0.01f },
        { "decay", 0.002f, 5, 0.2f },
        { "sustain", 0, 1, 0.7f },
        { "release", 0.005f, 8, 0.3f },
        { "cutoff", 20, 18000, 8000 },
        { "resonance", 0, 0.95f, 0.1f },
        { "filterAmount", -8, 8, 0 },
        { "filterAttack", 0.002f, 5, 0.01f },
        { "filterDecay", 0.002f, 5, 0.3f },
        { "filterSustain", 0, 1, 0 },
        { "filterRelease", 0.005f, 8, 0.3f },
        { "lfoRate", 0.05f, 20, 4 },
        { "lfoDepth", 0, 1, 0 },
        { "lfoTarget", 0, 2, 0, true },
        { "volume", 0, 1, 0.7f },
        { "gain", 0, 2, 1 },
        { "polyphony", 1, 32, 16, true },
    } };

    namespace
    {
        constexpr double Pi = 3.14159265358979323846;
        long long ticks()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count();
        }
        double blep(double t, double dt)
        {
            if (t < dt)
            {
                t /= dt;
                return t + t - t * t - 1;
            }
            if (t > 1 - dt)
            {
                t = (t - 1) / dt;
                return t * t + t + t + 1;
            }
            return 0;
        }
        double oscillator(double phase, double dt, const std::array<float, 4>& weights)
        {
            // PolyBLEP removes the saw/square discontinuities. The triangle uses a
            // Nyquist-limited additive series (odd harmonics with 1/n^2 falloff).
            double triangle = 0;
            for (int h = 1; weights[1] > 0.00001f && h <= 15 && h * dt < 0.5; h += 2)
                triangle += ((h % 4 == 1) ? 1 : -1) * std::sin(2 * Pi * phase * h) / (h * h);
            const double saw = 2 * phase - 1 - blep(phase, dt);
            const double square = (phase < 0.5 ? 1 : -1) + blep(phase, dt) - blep(std::fmod(phase + 0.5, 1), dt);
            return weights[0] * (weights[0] > 0.00001f ? std::sin(2 * Pi * phase) : 0)
                + weights[1] * triangle * (8 / (Pi * Pi)) + weights[2] * saw + weights[3] * square;
        }
    }

    ProceduralSynth::ProceduralSynth()
    {
        for (std::size_t i = 0; i < ParameterCount; ++i)
            mTarget[i] = mCurrent[i] = Parameters[i].initial;
        mWave1[0] = mWave2[2] = 1;
        keepAlive();
    }
    void ProceduralSynth::keepAlive()
    {
        mAlive = ticks();
    }
    bool ProceduralSynth::expired() const
    {
        return ticks() - mAlive.load() > 10000;
    }

    bool ProceduralSynth::set(std::string_view name, float value)
    {
        if (!std::isfinite(value) || closed())
            return false;
        for (std::size_t i = 0; i < ParameterCount; ++i)
            if (name == Parameters[i].name)
            {
                const auto& p = Parameters[i];
                if (value < p.min || value > p.max || (p.discrete && std::floor(value) != value))
                    return false;
                std::lock_guard<std::mutex> lock(mMutex);
                mTarget[i] = value;
                return true;
            }
        return false;
    }
    bool ProceduralSynth::enqueue(int note, float velocity)
    {
        if (closed() || note < 0 || note > 127 || !std::isfinite(velocity) || velocity > 1)
            return false;
        std::lock_guard<std::mutex> lock(mMutex);
        if (mCommandCount == mCommands.size())
        {
            // Drop the complete pending batch and release all voices on overflow.
            mCommandCount = 0;
            mPanic = true;
            return false;
        }
        mCommands[mCommandCount++] = { note, velocity };
        return true;
    }
    bool ProceduralSynth::noteOn(int note, float velocity)
    {
        return velocity >= 0 && enqueue(note, velocity);
    }
    bool ProceduralSynth::noteOff(int note)
    {
        return enqueue(note, -1);
    }
    void ProceduralSynth::allNotesOff()
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mCommandCount = 0;
        mPanic = true;
    }
    float ProceduralSynth::Envelope::next(float attack, float decay, float sustain, float releaseTime)
    {
        constexpr float dt = 1.f / SampleRate;
        if (stage == 1)
        {
            value += dt / attack;
            if (value >= 1)
            {
                value = 1;
                stage = 2;
            }
        }
        else if (stage == 2)
        {
            value -= dt * (1 - sustain) / decay;
            if (value <= sustain)
            {
                value = sustain;
                stage = 3;
            }
        }
        else if (stage == 3)
            value += (sustain - value) * 0.004f;
        else if (stage == 4)
        {
            value -= dt * releaseStart / releaseTime;
            if (value <= 0)
            {
                value = 0;
                stage = 0;
            }
        }
        return value;
    }
    void ProceduralSynth::apply(Command command)
    {
        for (auto& voice : mVoices)
            if (voice.note == command.note)
            {
                if (command.velocity < 0)
                {
                    voice.amp.release();
                    voice.filter.release();
                    return;
                }
                // Retrigger this pitch without resetting its phase or envelope amplitude.
                voice.velocity = command.velocity;
                voice.amp.stage = voice.filter.stage = 1;
                voice.age = ++mAge;
                return;
            }
        if (command.velocity < 0)
            return;
        const auto limit = static_cast<std::size_t>(mCurrent[25]);
        auto end = mVoices.begin() + limit;
        auto slot = std::min_element(mVoices.begin(), end, [](const Voice& a, const Voice& b) {
            if ((a.note < 0) != (b.note < 0))
                return a.note < 0;
            return a.age < b.age;
        });
        // Retain the stolen voice's envelope/phase/filter state to avoid a hard
        // output step.
        slot->note = command.note;
        slot->velocity = command.velocity;
        slot->amp.stage = slot->filter.stage = 1;
        slot->age = ++mAge;
    }
    void ProceduralSynth::render(float* output, std::size_t frames)
    {
        if (closed() || expired())
        {
            std::fill_n(output, frames, 0.f);
            mVoiceCount = 0;
            mPeak = 0;
            return;
        }
        State target = mCurrent;
        {
            // Control writers never block the audio thread. All storage is fixed-size.
            std::unique_lock<std::mutex> lock(mMutex, std::try_to_lock);
            if (lock.owns_lock())
            {
                target = mTarget;
                mCurrent[25] = target[25];
                if (mPanic.exchange(false))
                    for (auto& v : mVoices)
                    {
                        v.amp.release();
                        v.filter.release();
                    }
                for (std::size_t i = 0; i < mCommandCount; ++i)
                    apply(mCommands[i]);
                mCommandCount = 0;
            }
        }
        float peakValue = 0;
        for (std::size_t frame = 0; frame < frames; ++frame)
        {
            for (std::size_t i = 0; i < ParameterCount; ++i)
                mCurrent[i] += (target[i] - mCurrent[i]) * 0.004f;
            for (std::size_t w = 0; w < 4; ++w)
            {
                mWave1[w] += ((w == static_cast<std::size_t>(target[0]) ? 1.f : 0.f) - mWave1[w]) * 0.004f;
                mWave2[w] += ((w == static_cast<std::size_t>(target[5]) ? 1.f : 0.f) - mWave2[w]) * 0.004f;
            }
            const double lfo = std::sin(2 * Pi * mLfoPhase) * mCurrent[21];
            mLfoPhase += mCurrent[20] / SampleRate;
            mLfoPhase -= std::floor(mLfoPhase);
            double mixed = 0;
            for (std::size_t i = 0; i < mVoices.size(); ++i)
            {
                auto& v = mVoices[i];
                if (i >= static_cast<std::size_t>(target[25]) && v.amp.stage != 4)
                {
                    v.amp.release();
                    v.filter.release();
                }
                if (v.note < 0)
                    continue;
                const float amp = v.amp.next(mCurrent[9], mCurrent[10], mCurrent[11], mCurrent[12]);
                if (!v.amp.stage)
                {
                    v = Voice{};
                    continue;
                }
                const float env = v.filter.next(mCurrent[16], mCurrent[17], mCurrent[18], mCurrent[19]);
                const double pitchLfo = target[22] == 0 ? lfo * 2 : 0;
                const auto increment = [&](float tune, float fine) {
                    return std::min(
                        0.45, 440 * std::exp2((v.note - 69 + tune + fine / 100 + pitchLfo) / 12) / SampleRate);
                };
                const double dt1 = increment(mCurrent[2], mCurrent[3]);
                const double dt2 = increment(mCurrent[7], mCurrent[8]);
                double sample = oscillator(v.phase1, dt1, mWave1) * mCurrent[1];
                if (mCurrent[4] > 0.0001f)
                    sample += oscillator(v.phase2, dt2, mWave2) * mCurrent[6] * mCurrent[4];
                v.phase1 = std::fmod(v.phase1 + dt1, 1);
                v.phase2 = std::fmod(v.phase2 + dt2, 1);
                const double cutoff = std::clamp(
                    mCurrent[13] * std::exp2(env * mCurrent[15] + (target[22] == 1 ? lfo * 4 : 0)), 20.0, 18000.0);
                const double g = std::tan(Pi * cutoff / SampleRate);
                const double k = 2 - 1.9 * mCurrent[14];
                const double a = 1 / (1 + g * (g + k));
                const double band = a * (v.filter1 + g * (sample - v.filter2));
                const double low = v.filter2 + g * band;
                v.filter1 = 2 * band - v.filter1;
                v.filter2 = 2 * low - v.filter2;
                if (std::abs(v.filter1) < 1e-20)
                    v.filter1 = 0;
                if (std::abs(v.filter2) < 1e-20)
                    v.filter2 = 0;
                mixed += low * amp * v.velocity * (target[22] == 2 ? 1 - mCurrent[21] * 0.5 + lfo * 0.5 : 1);
            }
            output[frame] = static_cast<float>(std::tanh(mixed * mCurrent[23] * mCurrent[24] * 0.18));
            peakValue = std::max(peakValue, std::abs(output[frame]));
        }
        unsigned count = 0;
        for (const auto& v : mVoices)
            if (v.note >= 0)
                ++count;
        mVoiceCount = count;
        mPeak = peakValue;
    }
}
