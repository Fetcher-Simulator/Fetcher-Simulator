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

namespace MWSound
{
    // No engine, Lua, OpenAL or file dependencies. render() has one audio-thread
    // owner.
    class ProceduralSynth
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
        State mTarget{}, mCurrent{};
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
