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
        { "instrumentType", 0, 1, 0, true },
        { "stringDecay", 0.1f, 10, 3 },
        { "stringDamping", 0, 1, 0.35f },
        { "stringMute", 0, 1, 0 },
        { "stringPick", 0.05f, 0.95f, 0.23f },
        { "driveType", 0, 3, 0, true },
        { "drive", 0, 36, 12 },
        { "driveTone", 200, 16000, 12000 },
        { "fxMix", 0, 1, 1 },
        { "fxOutput", 0, 2, 1 },
        { "ampBass", -12, 12, 0 },
        { "ampMid", -12, 12, 0 },
        { "ampTreble", -12, 12, 0 },
        { "cabinet", 0, 6, 0, true },
        { "preHighpass", 0, 500, 0 },
        { "cabinetHighpass", 0, 500, 0 },
        { "cabinetLowpass", 0, 18000, 0 },
        { "stringArticulation", 0, 3, 0, true },
        { "pickAttack", 0, 1, 0 },
        { "pickDecay", .001f, .05f, .006f },
        { "stringResonance", 0, 1, 0 },
        { "pickPitch", -2, 2, 0 },
        { "pickVariation", 0, 1, 0 },
        { "pickDirection", 0, 2, 0, true },
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
        : mStrings(std::make_unique<Strings>())
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
            // Smoothed zero sustain can leave a subnormal value whose release
            // decrement underflows. Retire this inaudible tail so Stop and
            // preset changes cannot leave a silent voice allocated forever.
            if (value <= 1e-8f)
            {
                value = 0;
                stage = 0;
            }
        }
        return value;
    }
    bool ProceduralSynth::sample(std::shared_ptr<ProceduralSample> data, int root, bool oneShot, std::string_view hash)
    {
        if (root < 0 || root > 127 || (data && (hash.size() != 32
            || hash.find_first_not_of("0123456789abcdef") != hash.npos))) return false;
        std::lock_guard lock(mMutex);
        SampleSettings next; next.data = data.get(); next.root = root; next.oneShot = oneShot;
        if (data) std::copy(hash.begin(), hash.end(), next.hash.begin());
        mSamplePins.erase(std::remove_if(mSamplePins.begin(), mSamplePins.end(), [this, &next](const auto& p) {
            return p.get() != mSampleCurrent.data && p.get() != next.data;
        }), mSamplePins.end());
        if (next == mSampleTarget) return true;
        if (data && std::none_of(mSamplePins.begin(), mSamplePins.end(), [&](const auto& p) { return p == data; }))
            mSamplePins.push_back(std::move(data));
        mSampleTarget = next; return true;
    }
    bool ProceduralSynth::sampleReady() const
    {
        const auto* data = mSampleCurrent.data;
        return data && data->status.load(std::memory_order_acquire) == ProceduralSample::Ready
            && data->pcm.size() > 1 && data->hash == std::string_view(mSampleCurrent.hash.data(), 32);
    }
    void ProceduralSynth::syncSample()
    {
        if (mSampleCurrent == mSampleTarget) return;
        for (auto& voice : mVoices) voice = Voice{};
        mSampleCurrent = mSampleTarget;
        mSampleAlternates.fill(0);
    }
    const ProceduralSample::Zone* ProceduralSynth::sampleZone(int note,float value,bool advance)
    {
        if(!sampleReady())return nullptr;
        const auto& zones=mSampleCurrent.data->zones;
        const int velocity=std::clamp(static_cast<int>(std::lround(value*127)),1,127);
        for(std::size_t index=0;index<zones.size();++index)
        {
            const auto& z=zones[index];
            if(note<z.lowKey || note>z.highKey || velocity<z.lowVelocity || velocity>z.highVelocity)continue;
            if(!advance)return &z;
            const unsigned alternate=mSampleAlternates[index]++%z.alternates;
            for(const auto& candidate:zones)if(candidate.lowKey==z.lowKey && candidate.highKey==z.highKey
                && candidate.lowVelocity==z.lowVelocity && candidate.highVelocity==z.highVelocity
                && candidate.alternate==alternate)return &candidate;
            break;
        }
        return nullptr;
    }
    void ProceduralSynth::chokeSamples(unsigned group)
    {
        if(!group)return;
        for(auto& voice:mVoices)if(voice.note>=0 && voice.sampleGroup==group)
            {voice.amp.release();voice.filter.release();voice.drumChoke=true;}
    }
    void ProceduralSynth::apply(Command command)
    {
        if (mSampleCurrent.data && command.velocity >= 0 && !sampleReady()) return;
        const auto* zone=command.velocity>=0?sampleZone(command.note,command.velocity,true):nullptr;
        if(mSampleCurrent.data && command.velocity>=0 && !mSampleCurrent.data->zones.empty() && !zone)return;
        if(zone)chokeSamples(zone->offBy);
        const bool oneShot=mSampleCurrent.oneShot || (zone && (zone->flags&1));
        const auto sampleVoice=[&](Voice& voice) {
            if(!mSampleCurrent.data)return;
            voice.sampleStart=zone?zone->start:0;
            voice.sampleLength=zone?zone->length:static_cast<unsigned>(mSampleCurrent.data->pcm.size());
            voice.sampleRoot=zone?zone->root:mSampleCurrent.root;
            voice.sampleRootFrequency=440*std::exp2((voice.sampleRoot-69)/12.0);
            voice.sampleOneShot=oneShot;voice.sampleGroup=zone?zone->group:0;
        };
        const auto expression=[&](Voice& voice) {
            ProceduralStringExpression::Settings settings;
            const bool melodic=voice.drumType==0 && (!mSampleCurrent.data
                || (!voice.sampleOneShot && !mSampleCurrent.data->zones.empty()));
            if(melodic)
            {
                settings.articulation=unsigned(mCurrent[StringArticulation]);settings.attack=mCurrent[PickAttack];
                settings.attackDecay=mCurrent[PickDecay];settings.resonance=mCurrent[StringResonance];
                settings.pitch=mCurrent[PickPitch];settings.variation=mCurrent[PickVariation];settings.direction=unsigned(mCurrent[PickDirection]);
                settings.mute=mCurrent[StringMute];settings.decay=mCurrent[StringDecay];settings.damping=mCurrent[StringDamping];
            }
            voice.expression.trigger(settings,voice.velocity,voice.age);
        };
        for (auto& voice : mVoices)
            if (voice.note == command.note)
            {
                if (command.velocity < 0)
                {
                    // Drum hits are one-shots. MIDI gate length must not cut their tails.
                    if (voice.drumType == 0 && !voice.sampleOneShot)
                    {
                        voice.amp.release();
                        voice.filter.release();
                    }
                    return;
                }
                // Repeated drum hits overlap within the voice budget. Their note-off
                // events do not own a shared gate, unlike melodic notes.
                if (voice.drumType != 0 || (!mSampleCurrent.data && mCurrent[DrumType] != 0)
                    || (mSampleCurrent.data && (oneShot || voice.sampleOneShot)))
                    continue;
                if (mSampleCurrent.data) { voice = Voice{}; voice.samplePosition = 0; }
                sampleVoice(voice);
                // Melodic retriggers preserve phase and envelope amplitude.
                voice.note = command.note;
                voice.drumType = mSampleCurrent.data ? 0 : static_cast<int>(mCurrent[DrumType]);
                voice.velocity = command.velocity;
                voice.pitchEnvelope = 1;
                voice.amp.stage = voice.filter.stage = 1;
                voice.age = ++mAge;
                voice.drumChoke = false;
                voice.instrumentType = mSampleCurrent.data ? 0 : static_cast<int>(mCurrent[InstrumentType]);
                expression(voice);
                if (voice.instrumentType != 0)
                    pluck(voice);
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
        if (slot->drumType != 0 || mCurrent[DrumType] != 0 || mSampleCurrent.data)
        {
            const double carry = slot->drumLast;
            *slot = Voice{};
            slot->drumCarry = carry;
        }
        slot->drumType = mSampleCurrent.data ? 0 : static_cast<int>(mCurrent[DrumType]);
        sampleVoice(*slot);
        slot->note = command.note;
        slot->velocity = command.velocity;
        slot->pitchEnvelope = 1;
        slot->noise ^= static_cast<std::uint32_t>(mAge + 1) * 2654435761u;
        slot->amp.stage = slot->filter.stage = 1;
        slot->age = ++mAge;
        slot->drumChoke = false;
        slot->instrumentType = mSampleCurrent.data ? 0 : static_cast<int>(mCurrent[InstrumentType]);
        expression(*slot);
        if (slot->instrumentType != 0)
            pluck(*slot);
    }
    void ProceduralSynth::pluck(Voice& voice)
    {
        // Noise-excited, fractional-delay Karplus-Strong string. A comb in the
        // excitation controls pick position. Excitation uses per-voice state;
        // there is no shared random generator or audio-thread allocation.
        const double frequency = 440 * std::exp2((voice.note - 69 + mCurrent[2] + mCurrent[3] / 100) / 12);
        const auto period = static_cast<std::size_t>(std::clamp(std::round(SampleRate / frequency), 3.0, 8190.0));
        const double offset=voice.expression.pickOffset();
        const auto pick = std::max(std::size_t(1), static_cast<std::size_t>(offset==0
            ? period*mCurrent[StringPick] : period*std::clamp(mCurrent[StringPick]+offset,.05,.95)));
        const auto noise = [&](std::size_t n) {
            auto bits = static_cast<std::uint32_t>(n) + voice.noise;
            bits ^= bits >> 16; bits *= 0x7feb352du; bits ^= bits >> 15; bits *= 0x846ca68bu; bits ^= bits >> 16;
            return static_cast<double>(bits) / 2147483648.0 - 1;
        };
        auto& buffer = (*mStrings)[&voice - mVoices.data()];
        double mean = 0;
        for (std::size_t n = 0; n < period; ++n)
        {
            const double value = (noise(n) - noise((n + period - pick) % period)) * 0.7;
            buffer[StringCapacity - period + n] = static_cast<float>(value);
            mean += value / period;
        }
        for (std::size_t n = StringCapacity - period; n < StringCapacity; ++n)
            buffer[n] -= static_cast<float>(mean);
        // Initialize the entire history, including the part a pitch envelope or
        // LFO can reach before the first pass. A re-pluck must not read an old note.
        for (std::size_t n = StringCapacity - period; n-- > 0;)
            buffer[n] = buffer[n + period];
        voice.stringCarry = voice.stringLast;
        voice.stringPosition = 0; voice.stringPrevious = 0; voice.stringAge = 0;
    }
    void ProceduralSynth::resetAutomation()
    {
        mAutomated.fill(false); mProgrammed.fill(false); mAutomationSlope.fill(0);
    }
    void ProceduralSynth::automationDiscrete()
    {
        for (auto i : {std::size_t(25), std::size_t(28), std::size_t(41)})
            if (mAutomated[i] || mProgrammed[i])
                mCurrent[i] = static_cast<float>(mAutomated[i] ? mAutomation[i] : mProgram[i]);
        // Strike controls are latched per voice, including continuous pick curves.
        for(unsigned i=StringArticulation;i<ParameterCount;++i)
            if(i!=StringResonance && (mAutomated[i] || mProgrammed[i]))mCurrent[i]=static_cast<float>(mAutomated[i]?mAutomation[i]:mProgram[i]);
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
                syncSample();
                mCurrent[25] = target[25];
                mCurrent[DrumType] = target[DrumType];
                mCurrent[InstrumentType] = target[InstrumentType];
                for(unsigned i=StringArticulation;i<ParameterCount;++i)
                    if(i!=StringResonance && !mAutomated[i] && !mProgrammed[i])mCurrent[i]=target[i];
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
        const bool sampleAvailable = sampleReady();
        const bool hasDrumVoices = std::any_of(mVoices.begin(), mVoices.end(),
            [](const Voice& voice) { return voice.note >= 0 && voice.drumType != 0; });
        const bool hasTonalVoices = std::any_of(mVoices.begin(), mVoices.end(),
            [](const Voice& voice) { return voice.note >= 0 && voice.drumType == 0; });
        // Exact smoothing fixed points let settled patches avoid a 55-parameter
        // pass per sample. Modulation clocks and transitions remain unchanged.
        State effective = target;
        bool steady = true;
        for (std::size_t i = 0; i < ParameterCount; ++i)
        {
            if (mProgrammed[i]) effective[i] = mProgram[i];
            if (mAutomated[i]) effective[i] = static_cast<float>(mAutomation[i]);
            if (Parameters[i].discrete) effective[i] = std::round(effective[i]);
            if (i == 25 && (mAutomated[i] || mProgrammed[i])) effective[i] = std::min(8.f, effective[i]);
            const float next = Parameters[i].discrete ? effective[i] : ProceduralDsp::smooth(mCurrent[i], effective[i], 0.004f);
            steady = steady && next == mCurrent[i];
        }
        for (unsigned i = 0; i < ControlCount; ++i)
            if (mAutomated[i] && mAutomationSlope[i] != 0) steady = false;
        for (unsigned i = 0; i < 4; ++i)
        {
            const double value = mAutomated[ParameterCount+i] ? mAutomation[ParameterCount+i] : (i<2 ? 0.0 : 1.0);
            steady = steady && ProceduralDsp::smooth(mPerformance[i], value, 0.004) == mPerformance[i];
        }
        for (std::size_t w = 0; w < 5; ++w)
        {
            steady = steady && ProceduralDsp::smooth(mWave1[w], w == static_cast<std::size_t>(effective[0]) ? 1.f : 0.f, 0.004f) == mWave1[w];
            steady = steady && ProceduralDsp::smooth(mWave2[w], w == static_cast<std::size_t>(effective[5]) ? 1.f : 0.f, 0.004f) == mWave2[w];
        }
        const bool fixedFilter = steady && mCurrent[15] == 0 && !(effective[22] == 1 && mCurrent[21] != 0);
        const double fixedG = fixedFilter ? std::tan(Pi * mCurrent[13] / SampleRate) : 0;
        float peakValue = 0;
        for (std::size_t frame = 0; frame < frames; ++frame)
        {
            if (!steady)
            {
                effective = target;
                for (std::size_t i = 0; i < ParameterCount; ++i)
                {
                    if (mProgrammed[i]) effective[i] = mProgram[i];
                    if (mAutomated[i]) effective[i] = static_cast<float>(mAutomation[i]);
                    if (Parameters[i].discrete) effective[i] = std::round(effective[i]);
                    if (i == 25 && (mAutomated[i] || mProgrammed[i])) effective[i] = std::min(8.f, effective[i]);
                    mCurrent[i] = Parameters[i].discrete ? effective[i] : ProceduralDsp::smooth(mCurrent[i], effective[i], 0.004f);
                }
                for (unsigned i = 0; i < 4; ++i)
                {
                    const double value = mAutomated[ParameterCount+i] ? mAutomation[ParameterCount+i] : (i<2 ? 0.0 : 1.0);
                    mPerformance[i] = ProceduralDsp::smooth(mPerformance[i], value, 0.004);
                }
                for (unsigned i = 0; i < ControlCount; ++i) if (mAutomated[i]) mAutomation[i] += mAutomationSlope[i];
            }
            mModPhase += 6.0 / SampleRate; mModPhase -= std::floor(mModPhase);
            const double performancePitch = mPerformance[0] + (mPerformance[1] == 0 ? 0 : std::sin(2*Pi*mModPhase)*mPerformance[1]*0.5);
            if (!steady)
            {
                for (std::size_t w = 0; w < 5; ++w)
                {
                    mWave1[w] = ProceduralDsp::smooth(mWave1[w], w == static_cast<std::size_t>(effective[0]) ? 1.f : 0.f, 0.004f);
                    mWave2[w] = ProceduralDsp::smooth(mWave2[w], w == static_cast<std::size_t>(effective[5]) ? 1.f : 0.f, 0.004f);
                }
            }
            const double lfo = mCurrent[21] == 0 ? 0 : std::sin(2 * Pi * mLfoPhase) * mCurrent[21];
            mLfoPhase += mCurrent[20] / SampleRate;
            mLfoPhase -= std::floor(mLfoPhase);
            // Decay controls specify seconds to -60 dB. Shared coefficients avoid
            // per-voice exponentials, and all voice/filter storage is fixed-size.
            const double bodyDecay = hasDrumVoices ? std::exp(-6.907755 / (SampleRate * mCurrent[BodyDecay])) : 0;
            const double noiseDecay = hasDrumVoices ? std::exp(-6.907755 / (SampleRate * mCurrent[NoiseDecay])) : 0;
            const double drumPitchDecay = hasDrumVoices ? std::exp(-1.0 / (SampleRate * mCurrent[DrumPitchDecay])) : 0;
            const double noiseHigh = hasDrumVoices ? 1 - std::exp(-2 * Pi * mCurrent[NoiseHighpass] / SampleRate) : 0;
            const double noiseLow = hasDrumVoices ? 1 - std::exp(-2 * Pi * mCurrent[NoiseLowpass] / SampleRate) : 0;
            const double pitchDecay = hasTonalVoices ? std::exp(-1.0 / (SampleRate * mCurrent[27])) : 0;
            // Reuse the existing string controls for modeled damping of gated
            // bank notes. Zero is an exact bypass for existing sampled patches.
            const double sampleMute=sampleAvailable && !mSampleCurrent.data->zones.empty()?mCurrent[StringMute]:0;
            const double sampleLoss=sampleMute>0?std::exp(-6.907755*sampleMute
                /(SampleRate*mCurrent[StringDecay]*(1-.9*sampleMute))):1;
            double mixed = 0;
            for (std::size_t i = 0; i < mVoices.size(); ++i)
            {
                auto& v = mVoices[i];
                if (i >= static_cast<std::size_t>(effective[25]) && v.amp.stage != 4)
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
                    v.bodyEnvelope = ProceduralDsp::silence(v.bodyEnvelope * bodyDecay);
                    v.noiseEnvelope = ProceduralDsp::silence(v.noiseEnvelope * noiseDecay);
                    v.clickEnvelope = ProceduralDsp::silence(v.clickEnvelope * 0.97938218); // one millisecond transient
                    v.pitchEnvelope = ProceduralDsp::silence(v.pitchEnvelope * drumPitchDecay);
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
                const float amp = v.amp.next(mCurrent[9], mCurrent[10], mCurrent[11], v.drumChoke ? std::min(0.01f, mCurrent[12]) : mCurrent[12]);
                if (!v.amp.stage)
                {
                    v = Voice{};
                    continue;
                }
                const float env = v.filter.next(mCurrent[16], mCurrent[17], mCurrent[18], mCurrent[19]);
                const double pitchLfo = performancePitch + (effective[22] == 0 ? lfo * 2 : 0);
                const double pitchEnvelope = v.pitchEnvelope * mCurrent[26] + v.expression.pitch();
                v.pitchEnvelope = ProceduralDsp::silence(v.pitchEnvelope * pitchDecay);
                v.noise ^= v.noise << 13; v.noise ^= v.noise >> 17; v.noise ^= v.noise << 5;
                const double noise = static_cast<double>(v.noise) / 2147483648.0 - 1;
                const auto increment = [&](float tune, float fine) {
                    return std::min(
                        0.45, 440 * std::exp2((v.note - 69 + tune + fine / 100 + pitchLfo + pitchEnvelope) / 12) / SampleRate);
                };
                const double dt1 = mSampleCurrent.data ? 0 : increment(mCurrent[2], mCurrent[3]);
                const double dt2 = mSampleCurrent.data ? 0 : increment(mCurrent[7], mCurrent[8]);
                double sample = 0;
                double stringFrequency=0;
                if (mSampleCurrent.data)
                {
                    if (!sampleAvailable) { v = Voice{}; continue; }
                    const auto& pcm = mSampleCurrent.data->pcm;
                    const auto at = static_cast<std::size_t>(v.samplePosition);
                    if (at + 1 >= v.sampleLength) { v = Voice{}; continue; }
                    const double fraction = v.samplePosition - at;
                    const double step = std::exp2((v.note - v.sampleRoot + mCurrent[2]
                        + mCurrent[3] / 100 + pitchLfo + pitchEnvelope) / 12);
                    stringFrequency=v.sampleRootFrequency*step;
                    // Short edge fades protect arbitrary trims from hard steps.
                    const double fade = std::min({1.0, v.samplePosition / 96,
                        (v.sampleLength - 1 - v.samplePosition) / std::max(96.0, 96 * step)});
                    const auto first=v.sampleStart+at;
                    sample = (pcm[first] + (pcm[first + 1] - pcm[first]) * fraction) * fade;
                    if(!v.sampleOneShot) {
                        if(sampleMute>0 && v.expression.articulation()==0) {
                            const double frequency=std::clamp(v.sampleRootFrequency*step
                                *(8-5*mCurrent[StringDamping]),200.0,12000.0);
                            const double g=2*Pi*frequency/SampleRate;
                            v.sampleMuteLow+=(sample-v.sampleMuteLow)*g/(1+g);
                            sample+=(v.sampleMuteLow-sample)*sampleMute;
                            v.sampleMuteEnvelope=ProceduralDsp::silence(v.sampleMuteEnvelope*sampleLoss);
                        } else v.sampleMuteLow=sample;
                        sample*=v.sampleMuteEnvelope;
                        if(v.sampleMuteEnvelope<.00001) {v=Voice{};continue;}
                    }
                    v.samplePosition += step;
                }
                else if (v.instrumentType != 0)
                {
                    auto& buffer = (*mStrings)[i];
                    const double mute=v.expression.articulation()==0?mCurrent[StringMute]:0;
                    const double damping = 0.05 + 0.45 * std::clamp(
                        static_cast<double>(mCurrent[StringDamping]) + (1 - v.velocity) * 0.35 + mute * 0.4, 0.0, 1.0);
                    const double period = 1 / dt1;
                    const double delay = std::clamp(period - damping, 2.0, 8190.0);
                    double read = v.stringPosition + StringCapacity - delay;
                    if (read >= StringCapacity) read -= StringCapacity;
                    const auto index = static_cast<std::size_t>(read);
                    const double fraction = read - index;
                    const double delayed = buffer[index] * (1 - fraction) + buffer[(index + 1) % StringCapacity] * fraction;
                    const double decay = mCurrent[StringDecay] * (1 - 0.94 * mute);
                    const double loss = std::exp(-6.907755 * period / (SampleRate * decay));
                    const double filtered = (delayed * (1 - damping) + v.stringPrevious * damping) * loss;
                    v.stringPrevious = delayed;
                    buffer[v.stringPosition] = static_cast<float>(std::abs(filtered) < 1e-20 ? 0 : filtered);
                    v.stringPosition = (v.stringPosition + 1) % StringCapacity;
                    const double fade = std::min(1.0, v.stringAge / 0.002);
                    v.stringLast = filtered * fade + v.stringCarry * (1 - fade);
                    sample = v.stringLast * 2.5;
                    v.stringAge += 1.0 / SampleRate;
                }
                else
                {
                    sample = oscillator(v.phase1, dt1, mWave1, noise) * mCurrent[1];
                    if (mCurrent[4] > 0.0001f)
                        sample += oscillator(v.phase2, dt2, mWave2, noise) * mCurrent[6] * mCurrent[4];
                }
                const bool expressive=v.drumType==0 && (!mSampleCurrent.data || (!v.sampleOneShot && !mSampleCurrent.data->zones.empty()));
                if(expressive && (v.expression.enabled() || mCurrent[StringResonance]>0))
                {
                    if(!mSampleCurrent.data)stringFrequency=dt1*SampleRate;
                    sample=v.expression.process(sample,noise,stringFrequency,mCurrent[StringResonance]);
                    if(v.expression.finished()){v=Voice{};continue;}
                }
                v.phase1 = std::fmod(v.phase1 + dt1, 1);
                v.phase2 = std::fmod(v.phase2 + dt2, 1);
                const double cutoff = fixedFilter ? mCurrent[13] : std::clamp(
                    mCurrent[13] * std::exp2(env * mCurrent[15] + (effective[22] == 1 ? lfo * 4 : 0)), 20.0, 18000.0);
                const double g = fixedFilter ? fixedG : std::tan(Pi * cutoff / SampleRate);
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
                const double amplitude = mCurrent[AmpCurve] == 1 ? std::max(0.f, amp) : std::pow(std::max(0.f, amp), mCurrent[AmpCurve]);
                mixed += low * amplitude * v.velocity * (effective[22] == 2 ? 1 - mCurrent[21] * 0.5 + lfo * 0.5 : 1);
            }
            ProceduralSynthEffects::Settings fx;
            fx.mode = static_cast<int>(effective[DriveType]); fx.cabinet = static_cast<int>(effective[Cabinet]);
            fx.drive = mCurrent[Drive]; fx.tone = mCurrent[DriveTone]; fx.mix = mCurrent[FxMix]; fx.output = mCurrent[FxOutput];
            fx.bass = mCurrent[AmpBass]; fx.mid = mCurrent[AmpMid]; fx.treble = mCurrent[AmpTreble];
            fx.preHighpass=effective[PreHighpass]>0?mCurrent[PreHighpass]:0;
            fx.cabinetHighpass=effective[CabinetHighpass]>0?mCurrent[CabinetHighpass]:0;
            fx.cabinetLowpass=effective[CabinetLowpass]>0?mCurrent[CabinetLowpass]:0;
            // Preserve legacy multiplication order when the insert is bypassed.
            const double signal = mEffects.process(mixed, fx);
            output[frame] = static_cast<float>(std::tanh(signal * mCurrent[23] * mCurrent[24] * 0.18 * mPerformance[2] * mPerformance[3]));
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
