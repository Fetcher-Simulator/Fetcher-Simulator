#ifndef OPENMW_MWSOUND_PROCEDURALSTUDIO_H
#define OPENMW_MWSOUND_PROCEDURALSTUDIO_H

#include "proceduralsynth.hpp"
#include "proceduralmixer.hpp"
#include "proceduralreturns.hpp"
#include <vector>

namespace MWSound
{
    // One stream, one sample clock, independent instrument/effects state per lane.
    class ProceduralStudio final : public ProceduralAudio
    {
    public:
        ProceduralStudio()
        {
            for (auto& track : mTracks) track = nullptr;
            for (auto& lane : mNoteBits) for (auto& bits : lane) bits = 0;
            for (auto& state : mMixerTarget) state = ProceduralMixer::defaults();
            for (unsigned n=0;n<MaxTracks;++n) { mLanePeak[n]=0; mRms[n]=0; mReduction[n]=0; }
            keepAlive();
        }
        static constexpr unsigned MaxTracks = 16, MaxNotes = 60000, MaxActive = 4096;
        static constexpr std::uint32_t MaxFrames = 48000 * 1800;
        struct Note
        {
            std::uint32_t frame, duration;
            unsigned char track, pitch, velocity;
        };
        struct Score
        {
            std::vector<Note> notes;
            std::uint32_t length;
            bool loop;
        };
        bool configure(unsigned track, const ProceduralSynth::State& state, float level);
        bool hasTrack(unsigned track) const;
        bool mixer(unsigned track, const ProceduralMixer::State& state);
        float trackRms(unsigned n) const { return hasTrack(n) ? mRms[n-1].load() : 0; }
        float trackReduction(unsigned n) const { return hasTrack(n) ? mReduction[n-1].load() : 0; }
        bool effects(const ProceduralReturns::State& state, float master);
        float delayPeak() const {return mDelayPeak;}
        float reverbPeak() const {return mReverbPeak;}
        float masterRms() const { return mMasterRms; }
        float mixPeak() const { return mMixPeak; }
        bool set(unsigned track, std::string_view name, float value);
        bool noteOn(unsigned track, int note, float velocity);
        bool noteOff(unsigned track, int note);
        void releaseTrack(unsigned track);
        float trackPeak(unsigned track) const;
        unsigned trackVoices(unsigned track) const;
        bool loadScore(std::string_view bytes, std::uint32_t length, bool loop);
        bool start(double delay, double offset);
        bool replace(double delay);
        void stop();
        bool playing() const { return mPlaying; }
        double position() const { return static_cast<double>(mPosition.load()) / 48000; }
        unsigned fault() const { return mFault; }
        std::uint64_t strikes() const { return mStrikes; }
        std::vector<unsigned> activeNotes() const;
        void render(float* output, std::size_t frames) override;
        void close() override { mClosed = true; }
        bool closed() const override { return mClosed; }
        void keepAlive() override;
        bool expired() const override;
        float peak() const override { return mPeak; }
        unsigned voices() const override { return mVoiceCount; }
        unsigned units() const override { return 1 + mTrackCount.load(); }

    private:
        struct Off
        {
            std::uint64_t frame;
            unsigned track, pitch, generation;
            bool operator<(const Off& other) const { return frame > other.frame; }
        };
        // Slots publish once and live until the stream is destroyed. Render only
        // loads raw atomic pointers; it never locks a shared_ptr publication slot.
        std::array<std::unique_ptr<ProceduralSynth>, MaxTracks> mOwnedTracks{};
        std::array<std::atomic<ProceduralSynth*>, MaxTracks> mTracks{};
        std::array<std::array<unsigned, 128>, MaxTracks> mHeld{};
        std::array<std::array<std::atomic<std::uint64_t>, 2>, MaxTracks> mNoteBits{};
        std::array<unsigned, MaxTracks> mGeneration{};
        std::array<ProceduralMixer, MaxTracks> mMixer{};
        std::array<ProceduralMixer::State, MaxTracks> mMixerTarget{};
        std::array<float, MaxTracks> mLevelTarget{};
        std::array<bool, MaxTracks> mMixerDirty{};
        std::array<std::atomic<float>, MaxTracks> mLanePeak{}, mRms{}, mReduction{};
        std::array<double, MaxTracks> mPower{};
        std::atomic<float> mMasterRms{0}, mMixPeak{0};
        double mMasterPower=0;
        ProceduralReturns mReturns;
        ProceduralReturns::State mEffectsTarget=ProceduralReturns::defaults();
        float mMasterTarget=1,mMasterAudioTarget=1;double mMasterCurrent=1;
        bool mEffectsDirty=false,mMasterInitialized=false;
        std::atomic<float> mDelayPeak{0},mReverbPeak{0};
        std::array<Off, MaxActive> mOffs{};
        std::size_t mOffCount = 0, mIndex = 0;
        std::uint64_t mTime = 0, mCycle = 0, mDelay = 0;
        std::mutex mControl;
        struct Live { unsigned track; int pitch; float velocity; };
        std::array<Live, 256> mLive{};
        std::size_t mLiveCount = 0;
        bool enqueue(unsigned track, int pitch, float velocity);
        std::shared_ptr<const Score> mLoaded, mActive;
        std::shared_ptr<const Score> mReplacement;
        std::uint64_t mReplaceDelay = 0;
        bool mReplacePending = false;
        // Control-thread retention prevents the renderer freeing score vectors.
        std::vector<std::shared_ptr<const Score>> mRetained;
        bool mStartPending = false, mStopPending = false;
        std::uint64_t mPendingDelay = 0, mPendingOffset = 0;
        std::atomic<bool> mClosed{ false }, mPlaying{ false };
        std::atomic<float> mPeak{ 0 };
        std::atomic<unsigned> mVoiceCount{ 0 }, mTrackCount{ 0 }, mFault{ 0 };
        std::atomic<std::uint64_t> mPosition{ 0 }, mStrikes{ 0 };
        std::atomic<std::uint64_t> mRendered{ 0 };
        std::atomic<long long> mAlive{ 0 };
        void clearVoices();
        void trigger(const Note& note);
        void releaseDue();
    };
}
#endif
