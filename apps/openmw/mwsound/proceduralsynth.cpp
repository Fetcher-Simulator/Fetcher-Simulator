#include "proceduralsynth.hpp"

#include <algorithm>
#include <cmath>

namespace MWSound
{
    const std::array<ProceduralSynth::Parameter, ProceduralSynth::ParameterCount> ProceduralSynth::Parameters{ {
        { "wave1", 0, 4, 0, true },
        { "volume1", 0, 1, 0.8f },
        { "tune1", -24, 24, 0 },
        { "fine1", -100, 100, 0 },
        { "enabled2", 0, 1, 0, true },
        { "wave2", 0, 4, 2, true },
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
        { "pitchAmount", -48, 48, 0 },
        { "pitchDecay", 0.005f, 2, 0.08f },
        { "drumType", 0, 4, 0, true },
        { "bodyFrequency", 30, 1000, 180 },
        { "bodyDecay", 0.01f, 2, 0.14f },
        { "bodyLevel", 0, 1, 0.55f },
        { "noiseDecay", 0.005f, 3, 0.22f },
        { "noiseLevel", 0, 1, 0.75f },
        { "noiseHighpass", 20, 16000, 1500 },
        { "noiseLowpass", 200, 18000, 11000 },
        { "drumPitchAmount", 0, 48, 8 },
        { "drumPitchDecay", 0.005f, 0.5f, 0.025f },
        { "drumClick", 0, 1, 0.12f },
        { "drumDrive", 1, 8, 1.3f },
        { "ampCurve", 0.25f, 4, 1 },
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
        double oscillator(double phase, double dt, const std::array<float, 5>& weights, double noise)
        {
            // PolyBLEP removes the saw/square discontinuities. The triangle uses a
            // Nyquist-limited additive series (odd harmonics with 1/n^2 falloff).
            double triangle = 0;
            for (int h = 1; weights[1] > 0.00001f && h <= 15 && h * dt < 0.5; h += 2)
                triangle += ((h % 4 == 1) ? 1 : -1) * std::sin(2 * Pi * phase * h) / (h * h);
            const double saw = 2 * phase - 1 - blep(phase, dt);
            const double square = (phase < 0.5 ? 1 : -1) + blep(phase, dt) - blep(std::fmod(phase + 0.5, 1), dt);
            return weights[0] * (weights[0] > 0.00001f ? std::sin(2 * Pi * phase) : 0)
                + weights[1] * triangle * (8 / (Pi * Pi)) + weights[2] * saw + weights[3] * square + weights[4] * noise;
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
                    // Drum hits are one-shots. MIDI gate length must not cut their tails.
                    if (voice.drumType == 0)
                    {
                        voice.amp.release();
                        voice.filter.release();
                    }
                    return;
                }
                // Repeated drum hits overlap within the voice budget. Their note-off
                // events do not own a shared gate, unlike melodic notes.
                if (voice.drumType != 0 || mCurrent[DrumType] != 0)
                    continue;
                // Melodic retriggers preserve phase and envelope amplitude.
                voice.note = command.note;
                voice.drumType = static_cast<int>(mCurrent[DrumType]);
                voice.velocity = command.velocity;
                voice.pitchEnvelope = 1;
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
        if (slot->drumType != 0 || mCurrent[DrumType] != 0)
        {
            const double carry = slot->drumLast;
            *slot = Voice{};
            slot->drumCarry = carry;
        }
        slot->drumType = static_cast<int>(mCurrent[DrumType]);
        slot->note = command.note;
        slot->velocity = command.velocity;
        slot->pitchEnvelope = 1;
        slot->noise ^= static_cast<std::uint32_t>(mAge + 1) * 2654435761u;
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
                mCurrent[DrumType] = target[DrumType];
                if (mPanic.exchange(false))
                    for (auto& v : mVoices)
                    {
                        v.amp.release();
                        v.filter.release();
                        v.drumChoke = true;
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
            for (std::size_t w = 0; w < 5; ++w)
            {
                mWave1[w] += ((w == static_cast<std::size_t>(target[0]) ? 1.f : 0.f) - mWave1[w]) * 0.004f;
                mWave2[w] += ((w == static_cast<std::size_t>(target[5]) ? 1.f : 0.f) - mWave2[w]) * 0.004f;
            }
            const double lfo = std::sin(2 * Pi * mLfoPhase) * mCurrent[21];
            mLfoPhase += mCurrent[20] / SampleRate;
            mLfoPhase -= std::floor(mLfoPhase);
            // Decay controls specify seconds to -60 dB. Shared coefficients avoid
            // per-voice exponentials, and all voice/filter storage is fixed-size.
            const double bodyDecay = std::exp(-6.907755 / (SampleRate * mCurrent[BodyDecay]));
            const double noiseDecay = std::exp(-6.907755 / (SampleRate * mCurrent[NoiseDecay]));
            const double drumPitchDecay = std::exp(-1.0 / (SampleRate * mCurrent[DrumPitchDecay]));
            const double noiseHigh = 1 - std::exp(-2 * Pi * mCurrent[NoiseHighpass] / SampleRate);
            const double noiseLow = 1 - std::exp(-2 * Pi * mCurrent[NoiseLowpass] / SampleRate);
            double mixed = 0;
            for (std::size_t i = 0; i < mVoices.size(); ++i)
            {
                auto& v = mVoices[i];
                if (i >= static_cast<std::size_t>(target[25]) && v.amp.stage != 4)
                {
                    v.amp.release();
                    v.filter.release();
                    v.drumChoke = true;
                }
                if (v.note < 0)
                    continue;
                if (v.drumType != 0)
                {
                    v.noise ^= v.noise << 13; v.noise ^= v.noise >> 17; v.noise ^= v.noise << 5;
                    const double noise = static_cast<double>(v.noise) / 2147483648.0 - 1;
                    v.noiseHighState += noiseHigh * (noise - v.noiseHighState);
                    const double high = noise - v.noiseHighState;
                    v.noiseLowState += noiseLow * (high - v.noiseLowState);
                    const double frequency = mCurrent[BodyFrequency]
                        * std::exp2(v.pitchEnvelope * mCurrent[DrumPitchAmount] / 12);
                    const double increment = std::min(0.45, frequency / SampleRate);
                    double body;
                    if (v.drumType >= 3)
                    {
                        // Six inharmonic, band-limited square oscillators make the
                        // metallic hat layer. Noise has its own filter and decay.
                        constexpr std::array<double, 6> ratios{ 1, 1.342, 1.689, 2.071, 2.683, 3.217 };
                        constexpr std::array<float, 5> square{ 0, 0, 0, 1, 0 };
                        body = 0;
                        for (std::size_t n = 0; n < ratios.size(); ++n)
                        {
                            const double dt = std::min(0.45, increment * ratios[n]);
                            body += oscillator(v.metalPhases[n], dt, square, 0) / ratios.size();
                            v.metalPhases[n] = std::fmod(v.metalPhases[n] + dt, 1);
                        }
                        // Remove the square fundamentals from the hat's metallic
                        // layer using the same color controls as its noise layer.
                        v.metalHighState += noiseHigh * (body - v.metalHighState);
                        v.metalLowState += noiseLow * (body - v.metalHighState - v.metalLowState);
                        body = v.metalLowState;
                    }
                    else
                    {
                        body = std::sin(2 * Pi * v.phase1);
                        if (v.drumType == 2)
                            body = body * 0.75 + std::sin(2 * Pi * v.phase2) * 0.25;
                    }
                    v.phase1 = std::fmod(v.phase1 + increment, 1);
                    v.phase2 = std::fmod(v.phase2 + std::min(0.45, increment * 1.78), 1);
                    const double attack = std::min(1.0, v.drumAge / 0.0015);
                    double sample = body * v.bodyEnvelope * mCurrent[BodyLevel]
                        + v.noiseLowState * v.noiseEnvelope * mCurrent[NoiseLevel]
                        + noise * v.clickEnvelope * mCurrent[DrumClick];
                    sample = std::tanh(sample * mCurrent[DrumDrive]) / std::sqrt(mCurrent[DrumDrive]);
                    v.drumLast = sample * attack * v.velocity + v.drumCarry * (1 - attack);
                    mixed += v.drumLast;
                    v.bodyEnvelope *= bodyDecay;
                    v.noiseEnvelope *= noiseDecay;
                    v.clickEnvelope *= 0.97938218; // one millisecond transient
                    v.pitchEnvelope *= drumPitchDecay;
                    v.drumAge += 1.0 / SampleRate;
                    if (v.drumChoke)
                    {
                        v.bodyEnvelope *= 0.97;
                        v.noiseEnvelope *= 0.97;
                        v.clickEnvelope *= 0.97;
                    }
                    const double remaining = v.bodyEnvelope * mCurrent[BodyLevel]
                        + v.noiseEnvelope * mCurrent[NoiseLevel] + v.clickEnvelope * mCurrent[DrumClick];
                    if (remaining < 0.00001)
                        v = Voice{};
                    continue;
                }
                const float amp = v.amp.next(mCurrent[9], mCurrent[10], mCurrent[11], mCurrent[12]);
                if (!v.amp.stage)
                {
                    v = Voice{};
                    continue;
                }
                const float env = v.filter.next(mCurrent[16], mCurrent[17], mCurrent[18], mCurrent[19]);
                const double pitchLfo = target[22] == 0 ? lfo * 2 : 0;
                const double pitchEnvelope = v.pitchEnvelope * mCurrent[26];
                v.pitchEnvelope *= std::exp(-1.0 / (SampleRate * mCurrent[27]));
                v.noise ^= v.noise << 13; v.noise ^= v.noise >> 17; v.noise ^= v.noise << 5;
                const double noise = static_cast<double>(v.noise) / 2147483648.0 - 1;
                const auto increment = [&](float tune, float fine) {
                    return std::min(
                        0.45, 440 * std::exp2((v.note - 69 + tune + fine / 100 + pitchLfo + pitchEnvelope) / 12) / SampleRate);
                };
                const double dt1 = increment(mCurrent[2], mCurrent[3]);
                const double dt2 = increment(mCurrent[7], mCurrent[8]);
                double sample = oscillator(v.phase1, dt1, mWave1, noise) * mCurrent[1];
                if (mCurrent[4] > 0.0001f)
                    sample += oscillator(v.phase2, dt2, mWave2, noise) * mCurrent[6] * mCurrent[4];
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
                mixed += low * std::pow(std::max(0.f, amp), mCurrent[AmpCurve]) * v.velocity * (target[22] == 2 ? 1 - mCurrent[21] * 0.5 + lfo * 0.5 : 1);
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
