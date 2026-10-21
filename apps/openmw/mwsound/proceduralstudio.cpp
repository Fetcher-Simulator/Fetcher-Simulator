#include "proceduralstudio.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace MWSound
{
    namespace
    {
        long long ticks()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        }
    }
    void ProceduralStudio::keepAlive() { mAlive = ticks(); }
    bool ProceduralStudio::expired() const { return ticks() - mAlive.load() > 10000; }
    bool ProceduralStudio::hasTrack(unsigned n) const
    {
        return n >= 1 && n <= MaxTracks && std::atomic_load(&mTracks[n - 1]) != nullptr;
    }
    bool ProceduralStudio::configure(unsigned n, const ProceduralSynth::State& state, float level)
    {
        if (closed() || n < 1 || n > MaxTracks || !std::isfinite(level) || level < 0 || level > 1) return false;
        for (std::size_t i = 0; i < state.size(); ++i)
        {
            const auto& p = ProceduralSynth::Parameters[i];
            if (!std::isfinite(state[i]) || state[i] < p.min || state[i] > p.max
                || (p.discrete && std::floor(state[i]) != state[i])) return false;
        }
        std::lock_guard<std::mutex> lock(mControl);
        auto track = std::atomic_load(&mTracks[n - 1]);
        const bool fresh = !track;
        if (!track)
        {
            mOwnedTracks[n - 1] = std::make_unique<ProceduralSynth>();
            track = mOwnedTracks[n - 1].get();
        }
        for (std::size_t i = 0; i < state.size(); ++i)
            track->set(ProceduralSynth::Parameters[i].name, i == 25 ? std::min(8.f, state[i]) : state[i]);
        mLevelTarget[n-1]=level; mMixerDirty[n-1]=true;
        if (fresh) { std::atomic_store(&mTracks[n - 1], track); ++mTrackCount; }
        keepAlive(); return true;
    }
    bool ProceduralStudio::mixer(unsigned n, const ProceduralMixer::State& state)
    {
        if (!hasTrack(n) || closed() || !ProceduralMixer::valid(state)) return false;
        std::lock_guard<std::mutex> lock(mControl);
        mMixerTarget[n-1]=state; mMixerDirty[n-1]=true; return true;
    }
    bool ProceduralStudio::effects(const ProceduralReturns::State& state, float master)
    {
        if (closed() || !ProceduralReturns::valid(state) || !std::isfinite(master) || master<0 || master>1) return false;
        std::lock_guard<std::mutex> lock(mControl);
        mEffectsTarget=state;mMasterTarget=master;mEffectsDirty=true;return true;
    }
    bool ProceduralStudio::set(unsigned n, std::string_view name, float value)
    {
        if (!hasTrack(n) || closed() || !std::isfinite(value)) return false;
        if (name == "polyphony")
        {
            if (value < 1 || value > 32 || std::floor(value) != value) return false;
            value = std::min(8.f, value);
        }
        return std::atomic_load(&mTracks[n - 1])->set(name, value);
    }
    bool ProceduralStudio::noteOn(unsigned n, int pitch, float velocity)
    {
        return pitch >= 0 && pitch <= 127 && std::isfinite(velocity) && velocity >= 0 && velocity <= 1 && enqueue(n, pitch, velocity);
    }
    bool ProceduralStudio::noteOff(unsigned n, int pitch)
    {
        return pitch >= 0 && pitch <= 127 && enqueue(n, pitch, -1);
    }
    void ProceduralStudio::releaseTrack(unsigned n)
    {
        enqueue(n, 0, -2);
    }
    bool ProceduralStudio::enqueue(unsigned n, int pitch, float velocity)
    {
        if (!hasTrack(n) || closed()) return false;
        std::lock_guard<std::mutex> lock(mControl);
        if (mLiveCount == mLive.size()) { mLiveCount = 0; mStopPending = true; return false; }
        mLive[mLiveCount++] = {n - 1, pitch, velocity}; return true;
    }
    float ProceduralStudio::trackPeak(unsigned n) const
    {
        return hasTrack(n) ? mLanePeak[n-1].load() : 0;
    }
    unsigned ProceduralStudio::trackVoices(unsigned n) const
    {
        return hasTrack(n) ? std::atomic_load(&mTracks[n - 1])->voices() : 0;
    }
    bool ProceduralStudio::loadScore(std::string_view bytes, std::uint32_t length, bool loop)
    {
        // A routed output may have no notes while retaining the song clock.
        if (closed() || bytes.size() % 11 || bytes.size() / 11 > MaxNotes + MaxControls || !length || length > MaxFrames) return false;
        auto score = std::make_shared<Score>(); score->length = length; score->loop = loop;
        score->notes.reserve(bytes.size() / 11);
        auto u32 = [&](std::size_t offset) {
            std::uint32_t value = 0;
            for (unsigned i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + i])) << (8 * i);
            return value;
        };
        double tokens = 80; std::uint32_t previous = 0, previousNote = 0; unsigned controls = 0;
        for (std::size_t at = 0; at < bytes.size(); at += 11)
        {
            Note note{ u32(at), u32(at + 4), static_cast<unsigned char>(bytes[at + 8]),
                static_cast<unsigned char>(bytes[at + 9]), static_cast<unsigned char>(bytes[at + 10]) };
            if (note.frame < previous || note.frame >= length || note.track < 1 || note.track > MaxTracks || !hasTrack(note.track)) return false;
            previous = note.frame;
            if (note.pitch >= 128)
            {
                const unsigned code = note.pitch - 128;
                const bool program = note.velocity == 129;
                if (++controls > MaxControls || code >= ProceduralSynth::ControlCount || note.duration > 65535
                    || (note.velocity != 0 && note.velocity != 128 && !program)
                    || (program && code >= ProceduralSynth::ParameterCount)
                    || (note.velocity == 128 && (code == 59 || (code < 55 && ProceduralSynth::Parameters[code].discrete)))) return false;
                auto curve = std::find_if(score->curves.begin(), score->curves.end(), [&](const auto& c) {
                    return c.track == note.track-1u && c.code == code && c.program == program;
                });
                if (curve == score->curves.end())
                {
                    score->curves.push_back({note.track-1u, code, program, {}}); curve = score->curves.end()-1;
                }
                if (!curve->points.empty() && curve->points.back().frame >= note.frame) return false;
                curve->points.push_back({note.frame, note.duration, note.velocity == 128});
                continue;
            }
            if (!note.duration || note.duration > MaxFrames || !note.velocity || note.velocity > 127
                || (!loop && static_cast<std::uint64_t>(note.frame) + note.duration > length) || score->notes.size() >= MaxNotes) return false;
            tokens = std::min(80.0, tokens + (note.frame - previousNote) * (40.0 / 48000));
            if (tokens < 1 - 1e-9) return false;
            --tokens; previousNote = note.frame; score->notes.push_back(note);
        }
        if (loop && score->notes.size() * 48000.0 > 40.0 * length) return false;
        std::lock_guard<std::mutex> lock(mControl);
        mRetained.erase(std::remove_if(mRetained.begin(), mRetained.end(), [](const auto& p) { return p.use_count() == 1; }), mRetained.end());
        if (mRetained.size() >= 4) return false;
        mRetained.push_back(score); mLoaded = std::move(score); return true;
    }
    bool ProceduralStudio::start(double delay, double offset)
    {
        if (closed() || !std::isfinite(delay) || !std::isfinite(offset) || delay < 0 || delay > 8 || offset < 0 || offset > 1800) return false;
        std::lock_guard<std::mutex> lock(mControl);
        if (!mLoaded || (!mLoaded->loop && offset * 48000 >= mLoaded->length)) return false;
        mPendingDelay = mRendered.load() + static_cast<std::uint64_t>(std::llround(delay * 48000));
        mPendingOffset = static_cast<std::uint64_t>(std::llround(offset * 48000));
        mStartPending = true; mStopPending = false; return true;
    }
    void ProceduralStudio::stop()
    {
        std::lock_guard<std::mutex> lock(mControl); mLiveCount = 0; mStartPending = false; mStopPending = true;
    }
    bool ProceduralStudio::replace(double delay)
    {
        if (!std::isfinite(delay) || delay < 0 || delay > 8 || closed()) return false;
        std::lock_guard<std::mutex> lock(mControl);
        if (!mLoaded || !mPlaying) return false;
        mReplacePending = true; mPendingDelay = mRendered.load() + static_cast<std::uint64_t>(std::llround(delay * 48000));return true;
    }
    void ProceduralStudio::clearVoices()
    {
        mOffCount = 0;
        resetControls();
        for (unsigned n = 0; n < MaxTracks; ++n)
        {
            mHeld[n].fill(0); mLiveHeld[n].fill(false); mPedalHeld[n].fill(false); ++mGeneration[n];
            for (auto& bits : mNoteBits[n]) bits = 0;
            if (auto track = std::atomic_load(&mTracks[n]))
                for (auto& voice : track->mVoices) { voice.amp.release(); voice.filter.release(); voice.drumChoke = true; }
        }
    }
    void ProceduralStudio::trigger(const Note& note)
    {
        auto track = std::atomic_load(&mTracks[note.track - 1]);
        if (!track) return;
        if (mOffCount >= MaxActive) { mFault = 1; mPlaying = false; clearVoices(); return; }
        if (!track->mSampleCurrent.data && track->mCurrent[28] == 3)
            for (unsigned n = 0; n < MaxTracks; ++n)
                if (auto open = std::atomic_load(&mTracks[n]); open && !open->mSampleCurrent.data && open->mCurrent[28] == 4)
                {
                    ++mGeneration[n]; mHeld[n].fill(0); mLiveHeld[n].fill(false); for (auto& bits : mNoteBits[n]) bits = 0;
                    for (auto& voice : open->mVoices) { voice.amp.release(); voice.filter.release(); voice.drumChoke = true; }
                }
        const unsigned n = note.track - 1;
        ++mHeld[n][note.pitch]; mNoteBits[n][note.pitch / 64].fetch_or(std::uint64_t(1) << (note.pitch % 64));
        track->apply({ note.pitch, note.velocity / 127.f }); ++mStrikes;
        mOffs[mOffCount++] = { mTime + note.duration, n, note.pitch, mGeneration[n] };
        std::push_heap(mOffs.begin(), mOffs.begin() + mOffCount);
    }
    void ProceduralStudio::releaseDue()
    {
        while (mOffCount && mOffs[0].frame <= mTime)
        {
            std::pop_heap(mOffs.begin(), mOffs.begin() + mOffCount); const auto off = mOffs[--mOffCount];
            if (off.generation != mGeneration[off.track] || !mHeld[off.track][off.pitch]) continue;
            if (--mHeld[off.track][off.pitch] == 0 && !mLiveHeld[off.track][off.pitch])
            {
                if (mPedal[off.track]) { mPedalHeld[off.track][off.pitch] = true; continue; }
                mNoteBits[off.track][off.pitch / 64].fetch_and(~(std::uint64_t(1) << (off.pitch % 64)));
                if (auto track = std::atomic_load(&mTracks[off.track])) track->apply({ static_cast<int>(off.pitch), -1 });
            }
        }
    }
    void ProceduralStudio::pedal(unsigned n, bool down)
    {
        mPedal[n] = down;
        if (!down) for (unsigned pitch=0; pitch<128; ++pitch) if (mPedalHeld[n][pitch])
        {
            mPedalHeld[n][pitch] = false;
            if (!mHeld[n][pitch] && !mLiveHeld[n][pitch])
            {
                mNoteBits[n][pitch/64].fetch_and(~(std::uint64_t(1) << (pitch%64)));
                if (auto track = std::atomic_load(&mTracks[n])) track->apply({static_cast<int>(pitch), -1});
            }
        }
    }
    void ProceduralStudio::resetControls()
    {
        for (unsigned n=0; n<MaxTracks; ++n)
        {
            pedal(n, false);
            if (auto track = std::atomic_load(&mTracks[n])) track->resetAutomation();
        }
    }
    void ProceduralStudio::chaseControls()
    {
        resetControls(); mCurveApplied.fill(false); mChasingControls=true;
        const auto phase = mActive->loop ? mTime % mActive->length : std::min<std::uint64_t>(mTime,mActive->length);
        for (std::size_t c=0; c<mActive->curves.size(); ++c)
        {
            const auto& points = mActive->curves[c].points;
            mCurveIndices[c] = std::upper_bound(points.begin(), points.end(), phase,
                [](std::uint64_t f, const Point& p) {return f < p.frame;}) - points.begin();
        }
    }
    std::uint64_t ProceduralStudio::applyControls()
    {
        const auto phase = mActive->loop ? mTime % mActive->length : std::min<std::uint64_t>(mTime,mActive->length);
        std::uint64_t next = (mCycle+1)*mActive->length;
        for (std::size_t c=0; c<mActive->curves.size(); ++c)
        {
            const auto& curve = mActive->curves[c]; const auto& points = curve.points;
            auto& index = mCurveIndices[c]; const auto previousIndex = index;
            while (index < points.size() && points[index].frame <= phase) ++index;
            if (index < points.size()) next = std::min(next, mCycle*mActive->length+points[index].frame);
            if (!index || (mCurveApplied[c] && previousIndex == index)) continue;
            mCurveApplied[c] = true;
            auto track = std::atomic_load(&mTracks[curve.track]); if (!track) continue;
            const auto& p = points[index-1]; double value=p.value, slope=0;
            if (p.linear && index<points.size())
            {
                slope = (static_cast<double>(points[index].value)-p.value)/(points[index].frame-p.frame);
                value += slope*(phase-p.frame);
            }
            const auto code=curve.code;
            const double low = code<55 ? ProceduralSynth::Parameters[code].min : code==55 ? -2 : 0;
            const double high = code<55 ? ProceduralSynth::Parameters[code].max : code==55 ? 2 : 1;
            value=low+(high-low)*value/65535; slope *= (high-low)/65535;
            if (code==55 && std::abs(value)<0.00004) value=0;
            if (code<55 && ProceduralSynth::Parameters[code].discrete) value=std::round(value);
            if (code==25) value=std::min(8.0,value);
            if (curve.program) { track->mProgrammed[code]=true; track->mProgram[code]=static_cast<float>(value); }
            else { track->mAutomated[code]=true; track->mAutomation[code]=value; track->mAutomationSlope[code]=slope; }
            if (code==59) pedal(curve.track, value>=0.5);
        }
        for (const auto& slot : mTracks) if (auto track=std::atomic_load(&slot))
        {
            if (mChasingControls)
            {
                for (unsigned i=0; i<55; ++i) if (track->mAutomated[i] || track->mProgrammed[i])
                    track->mCurrent[i]=static_cast<float>(track->mAutomated[i] ? track->mAutomation[i] : track->mProgram[i]);
                for (unsigned i=0; i<4; ++i) track->mPerformance[i]=track->mAutomated[55+i] ? track->mAutomation[55+i] : (i<2 ? 0.0 : 1.0);
            }
            track->automationDiscrete();
        }
        mChasingControls=false; return next;
    }
    std::vector<unsigned> ProceduralStudio::activeNotes() const
    {
        std::vector<unsigned> result;
        for (unsigned n = 0; n < MaxTracks; ++n)
            for (unsigned word = 0; word < 2; ++word)
            {
                auto bits = mNoteBits[n][word].load();
                for (unsigned bit = 0; bits; ++bit, bits >>= 1)
                    if (bits & 1) result.push_back((n + 1) * 128 + word * 64 + bit);
            }
        return result;
    }
    void ProceduralStudio::render(float* output, std::size_t frames)
    {
        if (closed() || expired()) { std::fill_n(output, frames, 0.f); mPlaying = false; mVoiceCount = 0; mPeak = 0; return; }
        for (const auto& slot : mTracks)
            if (auto track = std::atomic_load(&slot))
            {
                std::unique_lock<std::mutex> lock(track->mMutex, std::try_to_lock);
                if (lock.owns_lock())
                {
                    track->syncSample();
                    for (auto parameter : {std::size_t(25), std::size_t(28), std::size_t(41)}) track->mCurrent[parameter] = track->mTarget[parameter];
                }
            }
        {
            std::unique_lock<std::mutex> lock(mControl, std::try_to_lock);
            if (lock.owns_lock())
            {
                if (mEffectsDirty)
                {
                    mReturns.target(mEffectsTarget);mMasterAudioTarget=mMasterTarget;mEffectsDirty=false;
                    if (!mMasterInitialized) {mMasterCurrent=mMasterTarget;mMasterInitialized=true;}
                }
                if (mStopPending) mReturns.stop();
                if (mStartPending) mReturns.resume();
                for (unsigned n=0;n<MaxTracks;++n) if (mMixerDirty[n])
                { mMixer[n].target(mMixerTarget[n],mLevelTarget[n]); mMixerDirty[n]=false; }
                if (mStopPending || mStartPending) { clearVoices(); mPlaying = false; mStopPending = false; mReplacement.reset(); mReplacePending = false; }
                if (mStartPending)
                {
                    const auto rendered = mRendered.load();
                    mActive = mLoaded; mDelay = mPendingDelay > rendered ? mPendingDelay - rendered : 0;
                    mTime = mPendingOffset + (rendered > mPendingDelay ? rendered - mPendingDelay : 0); mPosition = mTime; mFault = 0;
                    mCycle = mTime / mActive->length;
                    const auto frame = mTime % mActive->length;
                    mIndex = std::lower_bound(mActive->notes.begin(), mActive->notes.end(), frame,
                        [](const Note& note, std::uint64_t f) { return note.frame < f; }) - mActive->notes.begin();
                    chaseControls(); mPlaying = true; mStartPending = false;
                }
                if (mReplacePending) { mReplacement = mLoaded; mReplaceDelay = mPendingDelay > mRendered.load() ? mPendingDelay - mRendered.load() : 0; mReplacePending = false; }
                for (std::size_t c = 0; c < mLiveCount; ++c)
                {
                    const auto command = mLive[c];
                    if (auto track = std::atomic_load(&mTracks[command.track]))
                    {
                        if (command.velocity == -2)
                        {
                            mHeld[command.track].fill(0); mLiveHeld[command.track].fill(false); ++mGeneration[command.track];
                            for (auto& bits : mNoteBits[command.track]) bits = 0;
                            for (auto& voice : track->mVoices) { voice.amp.release(); voice.filter.release(); voice.drumChoke = true; }
                        }
                        else
                        {
                            const auto mask=std::uint64_t(1)<<(command.pitch%64);
                            mLiveHeld[command.track][command.pitch]=command.velocity>=0;
                            if (command.velocity>=0)
                            {
                                mReturns.resume();mNoteBits[command.track][command.pitch/64].fetch_or(mask);
                                track->apply({command.pitch,command.velocity});
                            }
                            else if (!mHeld[command.track][command.pitch])
                            {
                                if (mPedal[command.track]) { mPedalHeld[command.track][command.pitch]=true; continue; }
                                mNoteBits[command.track][command.pitch/64].fetch_and(~mask);
                                track->apply({command.pitch,command.velocity});
                            }
                        }
                    }
                }
                mLiveCount = 0;
            }
        }
        float peak = 0;
        std::array<float, 480> lane{}, mixed{},delayInput{},reverbInput{};
        std::size_t done = 0;
        while (done < frames)
        {
            if (mReplacement && !mReplaceDelay)
            {
                mActive = mReplacement; mReplacement.reset(); mCycle = mTime / mActive->length;
                mIndex = std::lower_bound(mActive->notes.begin(), mActive->notes.end(), mTime % mActive->length,
                    [](const Note& note, std::uint64_t frame) {return note.frame < frame;}) - mActive->notes.begin();
                chaseControls();
            }
            std::size_t count = std::min(frames - done, mixed.size());
            if (!mPlaying || mDelay) releaseDue();
            if (!mPlaying && mOffCount) count = static_cast<std::size_t>(std::min<std::uint64_t>(count, mOffs[0].frame - mTime));
            if (mReplacement) count = static_cast<std::size_t>(std::min<std::uint64_t>(count, mReplaceDelay));
            if (mPlaying && mDelay) count = static_cast<std::size_t>(std::min<std::uint64_t>(count, mDelay));
            else if (mPlaying)
            {
                if (mIndex == mActive->notes.size() && mActive->loop && mTime >= (mCycle + 1) * mActive->length) { ++mCycle; mIndex = 0; chaseControls(); }
                const auto nextControl = applyControls();
                releaseDue();
                while (mPlaying && mIndex < mActive->notes.size()
                    && mCycle * mActive->length + mActive->notes[mIndex].frame <= mTime) trigger(mActive->notes[mIndex++]);
                if (!mActive->loop && mTime >= mActive->length) { mPlaying = false; resetControls(); }
                if (mPlaying)
                {
                    std::uint64_t next = std::min((mCycle + 1) * mActive->length, nextControl);
                    if (mIndex < mActive->notes.size()) next = std::min(next, mCycle * mActive->length + mActive->notes[mIndex].frame);
                    if (mOffCount) next = std::min(next, mOffs[0].frame);
                    count = static_cast<std::size_t>(std::min<std::uint64_t>(count, next - mTime));
                }
            }
            mixed.fill(0);delayInput.fill(0);reverbInput.fill(0);
            for (unsigned n=0;n<MaxTracks;++n)
                if (auto track = std::atomic_load(&mTracks[n]))
                {
                    track->keepAlive(); track->render(lane.data(), count);
                    float held=mLanePeak[n];
                    for (std::size_t i=0;i<count;++i)
                    {
                        const float sample=mMixer[n].process(lane[i]); mixed[i]+=sample;
                        delayInput[i]+=sample*mMixer[n].delaySend();reverbInput[i]+=sample*mMixer[n].reverbSend();
                        held=std::max(std::abs(sample),ProceduralDsp::silence(held*0.99993056f));
                        mPower[n]+=0.00013888*(sample*sample-mPower[n]);
                        if (mPower[n]<1e-25) mPower[n]=0;
                    }
                    mLanePeak[n]=held; mRms[n]=static_cast<float>(std::sqrt(mPower[n]));
                    mReduction[n]=mMixer[n].reduction();
                }
            float mixPeak=mMixPeak,delayPeak=mDelayPeak,reverbPeak=mReverbPeak;
            for (std::size_t i=0;i<count;++i)
            {
                const auto wet=mReturns.process(delayInput[i],reverbInput[i]);
                delayPeak=std::max(std::abs(wet.delay),ProceduralDsp::silence(delayPeak*0.99993056f));reverbPeak=std::max(std::abs(wet.reverb),ProceduralDsp::silence(reverbPeak*0.99993056f));
                mMasterCurrent+=(mMasterAudioTarget-mMasterCurrent)*0.0020811647;
                if (std::abs(mMasterAudioTarget-mMasterCurrent)<1e-12) mMasterCurrent=mMasterAudioTarget;
                mixed[i]=static_cast<float>((mixed[i]+wet.delay+wet.reverb)*mMasterCurrent);
                mixPeak=std::max(std::abs(mixed[i]),ProceduralDsp::silence(mixPeak*0.99993056f));
                output[done+i]=std::tanh(mixed[i]);peak=std::max(peak,std::abs(output[done+i]));
                mMasterPower+=0.00013888*(output[done+i]*output[done+i]-mMasterPower);
                if (mMasterPower<1e-25) mMasterPower=0;
            }
            mDelayPeak=delayPeak;mReverbPeak=reverbPeak;
            mMixPeak=mixPeak; mMasterRms=static_cast<float>(std::sqrt(mMasterPower));
            if (mPlaying)
            {
                if (mDelay) mDelay -= count;
                else { mTime += count; mPosition = mTime; }
            }
            else if (mOffCount) mTime += count;
            done += count;
            mRendered.fetch_add(count);
            if (mReplacement) mReplaceDelay -= count;
        }
        unsigned voices = 0; for (const auto& slot : mTracks) if (auto track = std::atomic_load(&slot)) voices += track->voices();
        mVoiceCount = voices; mPeak = peak;
    }
}
