#ifndef OPENMW_MWSOUND_PROCEDURALSYNTH_H
#define OPENMW_MWSOUND_PROCEDURALSYNTH_H

#include <algorithm>
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
#include "proceduralstring.hpp"

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
        static constexpr std::size_t ParameterCount = 81;
        // Wire slots 55-59 retain their original MIDI-performance meaning.
        static constexpr unsigned decodeControl(unsigned wire)
        { return wire<55?wire:wire<60?ParameterCount+wire-55:wire-5; }
        static const std::array<Parameter, ParameterCount> Parameters;
        using State = std::array<float, ParameterCount>;
        struct AttackSample
        {
            std::shared_ptr<ProceduralSample> data;
            int root = 60;
            bool oneShot = false;
            std::string hash;
            float gain = 1;
        };
        using AttackSamples = std::array<AttackSample, 3>; // open, short/palm, dead/ghost

        ProceduralSynth();
        bool set(std::string_view name, float value);
        bool sample(std::shared_ptr<ProceduralSample> data, int root, bool oneShot, std::string_view hash,
            const AttackSamples& attacks = {}, float gain = 1);
        bool noteOn(int note, float velocity);
        struct Strike
        {
            int articulation = -1, selection = -1, direction = -1;
            bool valid() const { return articulation>=-1 && articulation<=3 && selection>=-1 && selection<=7 && direction>=-1 && direction<=2; }
            static Strike fromByte(unsigned value)
            { return {int(value%5)-1,int(value/5%9)-1,int(value/45)-1}; }
        };
        bool strike(int note, float velocity, Strike expression);
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
            DriveType, Drive, DriveTone, FxMix, FxOutput, AmpBass, AmpMid, AmpTreble, Cabinet,
            PreHighpass, CabinetHighpass, CabinetLowpass,
            StringArticulation, PickAttack, PickDecay, StringResonance, PickPitch, PickVariation, PickDirection,
            ParallelMix, ParallelClean, ParallelDirty, ParallelNoise, ParallelDrive, ParallelShape,
            ParallelComb, ParallelDelay, ParallelFeedback, ParallelRate, ParallelDepth, ParallelLift,
            StringLegato, StringSlideTime, StringLayout, StringSelect
        };
        struct Command
        {
            int note;
            float velocity;
            Strike expression;
            int identity = -1; // internal note/string key; -1 releases all matching live keys
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
            float sampleGain = 1;
            bool sampleOneShot = false;
            ProceduralSample* sampleData = nullptr;
            bool capturedAttack = false;
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
            ProceduralStringExpression expression;
            ProceduralPitchSlide slide;
            int connection = -1;
            unsigned key = 0;
            unsigned long long age = 0;
        };
        std::mutex mMutex;
        struct SampleSettings
        {
            ProceduralSample* data = nullptr;
            int root = 60;
            bool oneShot = false;
            float gain = 1;
            std::array<char, 32> hash{};
            bool operator==(const SampleSettings& other) const
            {
                return data == other.data && root == other.root && oneShot == other.oneShot && gain == other.gain && hash == other.hash;
            }
        };
        SampleSettings mSampleTarget, mSampleCurrent;
        std::array<SampleSettings,3> mAttackTarget{}, mAttackCurrent{};
        std::array<std::array<unsigned,512>,4> mSampleAlternates{}; // independent audio-thread zone-group counters
        // Reclaimed on control calls only. The current audio pointer remains
        // pinned until a later call observes that the callback has switched.
        std::vector<std::shared_ptr<ProceduralSample>> mSamplePins;
        const SampleSettings& sampleSettings(unsigned variant) const { return variant ? mAttackCurrent[variant-1] : mSampleCurrent; }
        bool sampleReady(unsigned variant = 0) const;
        void syncSample(); // mMutex held, audio thread only
        const ProceduralSample::Zone* sampleZone(int note, float velocity, bool advance, unsigned variant = 0);
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
        struct ConnectionKey {float velocity=0;std::uint64_t order=0;int group=-1;Strike expression;};
        using ConnectionKeys=std::array<ConnectionKey,1024>;
        std::unique_ptr<ConnectionKeys> mConnectionKeys=std::make_unique<ConnectionKeys>();
        std::uint64_t mConnectionSerial=0;
        std::array<int,7> mConnectionVoices{{-1,-1,-1,-1,-1,-1,-1}};
        int mConnectionMode=0,mConnectionLayout=0;
        bool mConnectionEligible=false;
        bool connectionReady() const;
        void resetConnections();
        void reconcileConnections();
        int connectionGroup(int note, int selection = -1) const;
        unsigned keyIndex(int note,int selection = -1) const
        {
            const int selected=selection>0?selection:mConnectionLayout ? (selection==0?0:int(mCurrent[StringSelect])) : 0;
            return unsigned(note+128*std::max(0,selected));
        }
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
        bool enqueue(int note, float velocity, Strike expression);
        void apply(Command command);
        void pluck(Voice& voice);
    };
}
#endif
