#ifndef OPENMW_MWSOUND_PROCEDURALSYNTH_H
#define OPENMW_MWSOUND_PROCEDURALSYNTH_H

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <memory>
#include <string_view>
#include "proceduralsyntheffects.hpp"
#include "proceduralaudio.hpp"
#include "proceduralsample.hpp"

namespace MWSound
{
    // No engine, Lua, OpenAL or file dependencies. render() has one audio-thread
    // owner.
    class ProceduralSynth : public ProceduralAudio
    {
    public:
        struct Parameter
        {
            const char* name;
            float min, max, initial;
            bool discrete = false;
        };
        static constexpr unsigned SampleRate = 48000;
        static constexpr std::size_t ParameterCount = 55;
        static const std::array<Parameter, ParameterCount> Parameters;
        using State = std::array<float, ParameterCount>;

        ProceduralSynth();
        bool set(std::string_view name, float value);
        bool sample(std::shared_ptr<ProceduralSample> data, int root, bool oneShot, std::string_view hash);
        bool noteOn(int note, float velocity);
        bool noteOff(int note);
        void allNotesOff();
        void close() { mClosed = true; }
        bool closed() const { return mClosed; }
        void keepAlive();
        bool expired() const;
        float peak() const { return mPeak; }
        unsigned voices() const { return mVoiceCount; }
        void render(float* output, std::size_t frames);

    private:
        friend class ProceduralStudio;
        enum DrumParameter : std::size_t
        {
            DrumType = 28, BodyFrequency, BodyDecay, BodyLevel, NoiseDecay, NoiseLevel,
            NoiseHighpass, NoiseLowpass, DrumPitchAmount, DrumPitchDecay, DrumClick, DrumDrive, AmpCurve,
            InstrumentType, StringDecay, StringDamping, StringMute, StringPick,
            DriveType, Drive, DriveTone, FxMix, FxOutput, AmpBass, AmpMid, AmpTreble, Cabinet
        };
        struct Command
        {
            int note;
            float velocity;
        };
        struct Envelope
        {
            int stage = 0;
            float value = 0, releaseStart = 0;
            void release()
            {
                releaseStart = value;
                stage = 4;
            }
            float next(float attack, float decay, float sustain, float release);
        };
        struct Voice
        {
            int note = -1;
            float velocity = 0;
            double phase1 = 0, phase2 = 0;
            double samplePosition = 0;
            unsigned sampleStart = 0, sampleLength = 0;
            int sampleRoot = 60;
            double sampleRootFrequency = 440, sampleMuteEnvelope = 1, sampleMuteLow = 0;
            bool sampleOneShot = false;
            unsigned char sampleGroup = 0;
            double filter1 = 0, filter2 = 0;
            double pitchEnvelope = 1;
            std::uint32_t noise = 0x9e3779b9u;
            Envelope amp, filter;
            int drumType = 0;
            bool drumChoke = false;
            double drumAge = 0, bodyEnvelope = 1, noiseEnvelope = 1, clickEnvelope = 1;
            double drumLast = 0, drumCarry = 0;
            double noiseHighState = 0, noiseLowState = 0;
            double metalHighState = 0, metalLowState = 0;
            std::array<double, 6> metalPhases{};
            int instrumentType = 0;
            std::size_t stringPosition = 0;
            double stringPrevious = 0, stringAge = 0, stringLast = 0, stringCarry = 0;
            unsigned long long age = 0;
        };
        std::mutex mMutex;
        struct SampleSettings
        {
            ProceduralSample* data = nullptr;
            int root = 60;
            bool oneShot = false;
            std::array<char, 32> hash{};
            bool operator==(const SampleSettings& other) const
            {
                return data == other.data && root == other.root && oneShot == other.oneShot && hash == other.hash;
            }
        };
        SampleSettings mSampleTarget, mSampleCurrent;
        std::array<unsigned,512> mSampleAlternates{}; // audio-thread zone-group counters
        // Reclaimed on control calls only. The current audio pointer remains
        // pinned until a later call observes that the callback has switched.
        std::vector<std::shared_ptr<ProceduralSample>> mSamplePins;
        bool sampleReady() const;
        void syncSample(); // mMutex held, audio thread only
        const ProceduralSample::Zone* sampleZone(int note, float velocity, bool advance);
        void chokeSamples(unsigned group);
        State mTarget{}, mCurrent{};
        // Score-owned overrides are audio-thread state, separate from the editable patch.
        static constexpr unsigned ControlCount = ParameterCount + 5;
        std::array<bool, ControlCount> mAutomated{};
        std::array<double, ControlCount> mAutomation{}, mAutomationSlope{};
        std::array<bool, ParameterCount> mProgrammed{};
        State mProgram{};
        std::array<double, 4> mPerformance{0, 0, 1, 1};
        double mModPhase = 0;
        void resetAutomation();
        void automationDiscrete();
        std::array<Command, 256> mCommands{};
        std::size_t mCommandCount = 0;
        std::array<Voice, 32> mVoices{};
        static constexpr std::size_t StringCapacity = 8192;
        using Strings = std::array<std::array<float, StringCapacity>, 32>;
        // Allocated once on the control thread; never allocated in render().
        std::unique_ptr<Strings> mStrings;
        ProceduralSynthEffects mEffects;
        std::array<float, 5> mWave1{}, mWave2{};
        unsigned long long mAge = 0;
        double mLfoPhase = 0;
        std::atomic<bool> mPanic{ false }, mClosed{ false };
        std::atomic<float> mPeak{ 0 };
        std::atomic<unsigned> mVoiceCount{ 0 };
        std::atomic<long long> mAlive{ 0 };
        bool enqueue(int note, float velocity);
        void apply(Command command);
        void pluck(Voice& voice);
    };
}
#endif
