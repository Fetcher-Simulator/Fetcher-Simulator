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
    bool ProceduralStudio::configure(unsigned n, const ProceduralSynth::State& state, float level, bool audible)
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
            mOwnedTracks[n - 1] = std::make_unique<ProceduralSynth>(n - 1);
            track = mOwnedTracks[n - 1].get();
        }
        for (std::size_t i = 0; i < state.size(); ++i)
            track->set(ProceduralSynth::Parameters[i].name, i == 25 ? std::min(8.f, state[i]) : state[i]);
        mLevelTarget[n-1]=level; mAudibleTarget[n-1]=audible; mMixerDirty[n-1]=true;
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
    bool ProceduralStudio::buses(const ProceduralMixer::State& member,float level,bool audible,const ProceduralMixer::State& song)
    {
        if(closed() || !ProceduralMixer::valid(member) || !ProceduralMixer::valid(song)
            || member[12]!=0 || member[13]!=0 || song[12]!=0 || song[13]!=0
            || !std::isfinite(level) || level<0 || level>1)return false;
        std::lock_guard<std::mutex> lock(mControl);
        mMemberTarget=member;mSongTarget=song;mMemberLevel=level;mMemberAudible=audible;mBusesDirty=true;return true;
    }
    bool ProceduralStudio::linkMaster(std::shared_ptr<ProceduralMaster> master,unsigned slot)
    {
        if(!master || closed())return false;
        std::lock_guard<std::mutex> lock(mControl);
        if(mMasterOwner)return mMasterOwner==master && mMasterSlot==slot;
        const auto token=master->claim(slot);if(!token)return false;
        mMasterSlot=slot;mMasterToken=token;mMasterOwner=std::move(master);
        mMasterGroup.store(mMasterOwner.get(),std::memory_order_release);return true;
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
    bool ProceduralStudio::strike(unsigned n, int pitch, float velocity, ProceduralSynth::Strike expression)
    {
        return expression.valid() && pitch>=0 && pitch<=127 && std::isfinite(velocity) && velocity>=0 && velocity<=1
            && enqueue(n,pitch,velocity,expression);
    }
    void ProceduralStudio::releaseTrack(unsigned n)
    {
        enqueue(n, 0, -2);
    }
    bool ProceduralStudio::enqueue(unsigned n, int pitch, float velocity, ProceduralSynth::Strike expression)
    {
        if (!hasTrack(n) || closed()) return false;
        std::lock_guard<std::mutex> lock(mControl);
        if (mLiveCount == mLive.size()) { mLiveCount = 0; mStopPending = true; return false; }
        mLive[mLiveCount++] = {n - 1, pitch, velocity, expression}; return true;
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
        if (closed() || bytes.size() % 11 || bytes.size() / 11 > MaxRecords || !length || length > MaxFrames) return false;
        auto score = std::make_shared<Score>(); score->length = length; score->loop = loop;
        score->notes.reserve(std::min<std::size_t>(MaxNotes,bytes.size() / 11));
        auto u32 = [&](std::size_t offset) {
            std::uint32_t value = 0;
            for (unsigned i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + i])) << (8 * i);
            return value;
        };
        double tokens = NoteBurst; std::uint32_t previous = 0, previousNote = 0; unsigned controls = 0;
        Note pending{};bool hasExpression=false;
        for (std::size_t at = 0; at < bytes.size(); at += 11)
        {
            Note note{ u32(at), u32(at + 4), static_cast<unsigned char>(bytes[at + 8]),
                static_cast<unsigned char>(bytes[at + 9]), static_cast<unsigned char>(bytes[at + 10]) };
            if (note.frame < previous || note.frame >= length || note.track < 1 || note.track > MaxTracks || !hasTrack(note.track)) return false;
            previous = note.frame;
            if(note.pitch<128 && note.velocity==130)
            {
                if(hasExpression || !note.duration || note.duration>179)return false;
                pending=note;hasExpression=true;continue;
            }
            if(hasExpression)
            {
                if(note.pitch>=128 || note.frame!=pending.frame || note.track!=pending.track || note.pitch!=pending.pitch)return false;
                note.expression=ProceduralSynth::Strike::fromByte(pending.duration);hasExpression=false;
            }
            if (note.pitch >= 128)
            {
                const unsigned wire = note.pitch - 128;
                const unsigned code = wire >= ProceduralSynth::ControlCount ? wire : ProceduralSynth::decodeControl(wire);
                const bool program = note.velocity == 129;
                if (++controls > MaxControls || wire >= ProceduralSynth::ControlCount+ProceduralMixer::AutomationCount || note.duration > 65535
                    || (note.velocity != 0 && note.velocity != 128 && !program)
                    || (program && code >= ProceduralSynth::ParameterCount)
                    || (note.velocity == 128 && (code == ProceduralSynth::ParameterCount+4 || (code < ProceduralSynth::ParameterCount && ProceduralSynth::Parameters[code].discrete)))) return false;
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
            tokens = std::min(double(NoteBurst), tokens + (note.frame - previousNote) * (double(NoteRate) / 48000));
            if (tokens < 1 - 1e-9) return false;
            --tokens; previousNote = note.frame; score->notes.push_back(note);
        }
        if(hasExpression)return false;
        if (loop && score->notes.size() * 48000.0 > NoteRate * double(length)) return false;
        if (loop)
            for (const auto& note : score->notes)
            {
                const auto frame = note.frame + length;
                tokens = std::min(double(NoteBurst), tokens + (frame - previousNote) * (double(NoteRate) / 48000));
                if (tokens < 1 - 1e-9) return false;
                --tokens; previousNote = frame;
            }
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
        for(const auto& slot:mTracks)if(auto track=std::atomic_load(&slot))track->resetConnections();
        resetControls();
        for (unsigned n = 0; n < MaxTracks; ++n)
        {
            mHeld[n].fill(0); mHeldKeys->counts[n].fill(0); mHeldKeys->pedal[n].fill(false); mHeldKeys->live[n].fill(false); mLiveHeld[n].fill(false); mPedalHeld[n].fill(false); ++mGeneration[n];clearPresentation(n);
            for (auto& bits : mNoteBits[n]) bits = 0;
            if (auto track = std::atomic_load(&mTracks[n]))
            {
                for (auto& voice : track->mVoices) { voice.amp.release(); voice.filter.release(); voice.drumChoke = true; }
            }
        }
    }
    void ProceduralStudio::chokeSampleGroup(unsigned n,int pitch,float velocity)
    {
        auto source=std::atomic_load(&mTracks[n]);if(!source)return;
        const auto* zone=source->sampleZone(pitch,velocity,false);
        if(!zone || !zone->offBy)return;
        for(unsigned other=0;other<MaxTracks;++other)if(other!=n)
            if(auto target=std::atomic_load(&mTracks[other]);target && target->mSampleCurrent.data==source->mSampleCurrent.data)
                target->chokeSamples(zone->offBy);
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
                    ++mGeneration[n]; mHeld[n].fill(0); mHeldKeys->counts[n].fill(0); mHeldKeys->pedal[n].fill(false); mHeldKeys->live[n].fill(false); mPedalHeld[n].fill(false); mLiveHeld[n].fill(false); for (auto& bits : mNoteBits[n]) bits = 0;clearPresentation(n);
                    for (auto& voice : open->mVoices) { voice.amp.release(); voice.filter.release(); voice.drumChoke = true; }
                }
        const unsigned n = note.track - 1;
        ++mHeld[n][note.pitch]; mNoteBits[n][note.pitch / 64].fetch_or(std::uint64_t(1) << (note.pitch % 64));
        chokeSampleGroup(n,note.pitch,note.velocity/127.f);
        track->apply({ note.pitch, note.velocity / 127.f, note.expression }); ++mStrikes;
        const unsigned key=track->keyIndex(note.pitch,note.expression.selection);++mHeldKeys->counts[n][key];publishAttack(n,key,note.expression);
        mOffs[mOffCount++] = { mTime + note.duration, n, note.pitch, mGeneration[n], key };
        std::push_heap(mOffs.begin(), mOffs.begin() + mOffCount);
    }
    void ProceduralStudio::releaseDue()
    {
        while(mOffCount && mOffs[0].frame<=mTime)
        {
            std::pop_heap(mOffs.begin(),mOffs.begin()+mOffCount);const auto off=mOffs[--mOffCount];
            if(off.generation!=mGeneration[off.track] || !mHeld[off.track][off.pitch] || !mHeldKeys->counts[off.track][off.key])continue;
            --mHeld[off.track][off.pitch];
            const bool live=mHeldKeys->live[off.track][off.key];
            if(--mHeldKeys->counts[off.track][off.key]==0 && !live)
            {
                if(mPedal[off.track]) {mPedalHeld[off.track][off.pitch]=true;mHeldKeys->pedal[off.track][off.key]=true;continue;}
                if(!mHeld[off.track][off.pitch] && !mLiveHeld[off.track][off.pitch])
                    mNoteBits[off.track][off.pitch/64].fetch_and(~(std::uint64_t(1)<<(off.pitch%64)));
                releasePresentation(off.track,off.key);
                if(auto track=std::atomic_load(&mTracks[off.track]))track->apply({int(off.pitch),-1,{},int(off.key)});
            }
        }
    }
    void ProceduralStudio::pedal(unsigned n,bool down)
    {
        mPedal[n]=down;
        if(down)return;
        for(unsigned key=0;key<1024;++key)if(mHeldKeys->pedal[n][key])
        {
            mHeldKeys->pedal[n][key]=false;const unsigned pitch=key%128;
            const bool live=mHeldKeys->live[n][key];
            if(!mHeldKeys->counts[n][key] && !live)
            {
                if(!mHeld[n][pitch] && !mLiveHeld[n][pitch])mNoteBits[n][pitch/64].fetch_and(~(std::uint64_t(1)<<(pitch%64)));
                releasePresentation(n,key);
                if(auto track=std::atomic_load(&mTracks[n]))track->apply({int(pitch),-1,{},int(key)});
            }
        }
        mPedalHeld[n].fill(false);
    }
    void ProceduralStudio::resetControls()
    {
        for (unsigned n=0; n<MaxTracks; ++n)
        {
            pedal(n, false); mMixer[n].resetAutomation();
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
            const double low = code<ProceduralSynth::ParameterCount ? ProceduralSynth::Parameters[code].min : code==ProceduralSynth::ParameterCount ? -2 : 0;
            const double high = code<ProceduralSynth::ParameterCount ? ProceduralSynth::Parameters[code].max : code==ProceduralSynth::ParameterCount ? 2 : 1;
            value=low+(high-low)*value/65535; slope *= (high-low)/65535;
            if ((code==ProceduralSynth::ParameterCount || code==ProceduralSynth::PickPitch) && std::abs(value)<0.00004) value=0;
            if (code<ProceduralSynth::ParameterCount && ProceduralSynth::Parameters[code].discrete) value=std::round(value);
            if (code==25) value=std::min(8.0,value);
            if (code>=ProceduralSynth::ControlCount)
                mMixer[curve.track].automate(code-ProceduralSynth::ControlCount,value,slope,mChasingControls);
            else if (curve.program) { track->mProgrammed[code]=true; track->mProgram[code]=static_cast<float>(value); }
            else { track->mAutomated[code]=true; track->mAutomation[code]=value; track->mAutomationSlope[code]=slope; }
            if (code==ProceduralSynth::ParameterCount+4) pedal(curve.track, value>=0.5);
        }
        for (const auto& slot : mTracks) if (auto track=std::atomic_load(&slot))
        {
            if (mChasingControls)
            {
                for (unsigned i=0; i<ProceduralSynth::ParameterCount; ++i) if (track->mAutomated[i] || track->mProgrammed[i])
                    track->mCurrent[i]=static_cast<float>(track->mAutomated[i] ? track->mAutomation[i] : track->mProgram[i]);
                for (unsigned i=0; i<4; ++i) track->mPerformance[i]=track->mAutomated[ProceduralSynth::ParameterCount+i] ? track->mAutomation[ProceduralSynth::ParameterCount+i] : (i<2 ? 0.0 : 1.0);
            }
            track->automationDiscrete();
        }
        mChasingControls=false; return next;
    }
    void ProceduralStudio::publishAttack(unsigned n,unsigned key,const ProceduralSynth::Strike& expression)
    {
        const auto track=std::atomic_load(&mTracks[n]);if(!track)return;
        const auto choose=[&](int selected,unsigned code){return unsigned(selected>=0?selected:std::lround(track->mCurrent[code]));};
        unsigned direction=choose(expression.direction,ProceduralSynth::PickDirection);
        if(direction==2)direction=unsigned(track->mAge&1);
        const unsigned layout=unsigned(std::lround(track->mCurrent[ProceduralSynth::StringLayout]));
        const unsigned selection=choose(expression.selection,ProceduralSynth::StringSelect);
        const unsigned articulation=choose(expression.articulation,ProceduralSynth::StringArticulation);
        const double tune=track->mAutomated[2]?track->mAutomation[2]:track->mProgrammed[2]?track->mProgram[2]:mPresentationTune[n];
        const unsigned transpose=unsigned(std::lround(tune)+24);
        const std::uint64_t packed=layout|(selection<<3)|(articulation<<6)|(direction<<8)|(transpose<<10)
            |(1u<<16)|(1u<<17)|(std::uint64_t(std::uint32_t(mRendered.load()))<<32);
        mPresentation->keys[n][key].store(packed,std::memory_order_relaxed);
    }
    std::vector<ProceduralStudio::PerformanceNote> ProceduralStudio::performance() const
    {
        std::vector<PerformanceNote> result;if(closed() || expired())return result;
        const auto now=std::uint32_t(mRendered.load());
        for(unsigned n=0;n<MaxTracks;++n)if(hasTrack(n+1))for(unsigned key=0;key<1024;++key)
        {
            const auto bits=mPresentation->keys[n][key].load(std::memory_order_relaxed);
            if(!(bits&(1u<<17)))continue;
            const auto stamp=std::uint32_t(bits>>32);auto age=std::uint32_t(now-stamp);
            // A writer can advance between the clock and packed-word reads.
            if(age>UINT32_MAX-48000)age=0;
            const bool held=bits&(1u<<16);
            if(!held && age>12000)continue;
            result.push_back({n+1,key%128,unsigned(bits>>3)&7,unsigned(bits)&7,unsigned(bits>>6)&3,
                unsigned(bits>>8)&3,int((bits>>10)&63)-24,stamp,double(age)/48000,held});
        }
        return result;
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
        for (unsigned n=0;n<MaxTracks;++n)
            if (auto track = std::atomic_load(&mTracks[n]))
            {
                std::unique_lock<std::mutex> lock(track->mMutex, std::try_to_lock);
                if (lock.owns_lock())
                {
                    mPresentationTune[n]=track->mTarget[2];
                    track->syncSample();
                    for (auto parameter : {std::size_t(25), std::size_t(28), std::size_t(41)}) track->mCurrent[parameter] = track->mTarget[parameter];
                    for(unsigned i=ProceduralSynth::StringArticulation;i<=ProceduralSynth::PickDirection;++i)
                        if(i!=ProceduralSynth::StringResonance && !track->mAutomated[i] && !track->mProgrammed[i])track->mCurrent[i]=track->mTarget[i];
                    for(auto i:{ProceduralSynth::StringLegato,ProceduralSynth::StringSlideTime,ProceduralSynth::StringLayout,ProceduralSynth::StringSelect})
                        if(!track->mAutomated[i] && !track->mProgrammed[i])track->mCurrent[i]=track->mTarget[i];
                    track->reconcileConnections();
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
                if(mBusesDirty)
                {
                    mMemberStrip.target(mMemberTarget,mMemberLevel,mMemberAudible);
                    mSongStrip.target(mSongTarget,1);mBusesDirty=false;mBusesActive=true;
                }
                if(mStopPending || mStartPending)
                {
                    mMemberStrip.resetDetector();mSongStrip.resetDetector();mLinkedEnvelope=0;mBusFrames=0;
                    if(auto* master=mMasterGroup.load())master->publish(mMasterSlot,mMasterToken,0);
                }
                if (mStopPending) mReturns.stop();
                if (mStartPending) mReturns.resume();
                for (unsigned n=0;n<MaxTracks;++n) if (mMixerDirty[n])
                { mMixer[n].target(mMixerTarget[n],mLevelTarget[n],mAudibleTarget[n]); mMixerDirty[n]=false; }
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
                            track->resetConnections();
                            mHeld[command.track].fill(0); mHeldKeys->counts[command.track].fill(0); mHeldKeys->pedal[command.track].fill(false); mHeldKeys->live[command.track].fill(false); mPedalHeld[command.track].fill(false); mLiveHeld[command.track].fill(false); ++mGeneration[command.track];
                            for (auto& bits : mNoteBits[command.track]) bits = 0;clearPresentation(command.track);
                            for (auto& voice : track->mVoices) { voice.amp.release(); voice.filter.release(); voice.drumChoke = true; }
                        }
                        else
                        {
                            const auto mask=std::uint64_t(1)<<(command.pitch%64);
                            mLiveHeld[command.track][command.pitch]=command.velocity>=0;
                            if (command.velocity>=0)
                            {
                                mReturns.resume();mNoteBits[command.track][command.pitch/64].fetch_or(mask);
                                chokeSampleGroup(command.track,command.pitch,command.velocity);
                                track->apply({command.pitch,command.velocity,command.expression});
                                const unsigned key=track->keyIndex(command.pitch,command.expression.selection);
                                mHeldKeys->live[command.track][key]=true;publishAttack(command.track,key,command.expression);
                            }
                            else
                            {
                                for(unsigned string=0;string<8;++string)
                                {
                                    const unsigned key=unsigned(command.pitch)+128*string;
                                    if(!mHeldKeys->live[command.track][key])continue;
                                    mHeldKeys->live[command.track][key]=false;
                                    if(mHeldKeys->counts[command.track][key])continue;
                                    if(mPedal[command.track]) {mPedalHeld[command.track][command.pitch]=true;mHeldKeys->pedal[command.track][key]=true;continue;}
                                    releasePresentation(command.track,key);
                                    track->apply({command.pitch,command.velocity,{},int(key)});
                                }
                                if(!mHeld[command.track][command.pitch] && !mPedalHeld[command.track][command.pitch])
                                    mNoteBits[command.track][command.pitch/64].fetch_and(~mask);
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
                    const float monitor=mMonitorTarget[n];
                    for (std::size_t i=0;i<count;++i)
                    {
                        const float sample=mMixer[n].process(lane[i]);
                        mMonitorCurrent[n]+=(monitor-mMonitorCurrent[n])*0.0020811647;
                        if(std::abs(monitor-mMonitorCurrent[n])<1e-12)mMonitorCurrent[n]=monitor;
                        const float heard=static_cast<float>(sample*mMonitorCurrent[n]);mixed[i]+=heard;
                        delayInput[i]+=heard*mMixer[n].delaySend();reverbInput[i]+=heard*mMixer[n].reverbSend();
                        held=std::max(std::abs(sample),ProceduralDsp::silence(held*0.99993056f));
                        mPower[n]+=0.00013888*(sample*sample-mPower[n]);
                        if (mPower[n]<1e-25) mPower[n]=0;
                    }
                    mLanePeak[n]=held; mRms[n]=static_cast<float>(std::sqrt(mPower[n]));
                    mReduction[n]=mMixer[n].reduction();
                }
            float mixPeak=mMixPeak,delayPeak=mDelayPeak,reverbPeak=mReverbPeak;
            float memberPeak=mMemberPeak;auto* const masterGroup=mMasterGroup.load(std::memory_order_acquire);
            for (std::size_t i=0;i<count;++i)
            {
                const auto wet=mReturns.process(delayInput[i],reverbInput[i]);
                delayPeak=std::max(std::abs(wet.delay),ProceduralDsp::silence(delayPeak*0.99993056f));reverbPeak=std::max(std::abs(wet.reverb),ProceduralDsp::silence(reverbPeak*0.99993056f));
                mMasterCurrent+=(mMasterAudioTarget-mMasterCurrent)*0.0020811647;
                if (std::abs(mMasterAudioTarget-mMasterCurrent)<1e-12) mMasterCurrent=mMasterAudioTarget;
                float value=mixed[i]+wet.delay+wet.reverb;
                if(mBusesActive)
                {
                    value=mMemberStrip.process(value);
                    mMemberPower+=.00013888*(double(value)*value-mMemberPower);
                    if(mMemberPower<1e-25)mMemberPower=0;
                    memberPeak=std::max(std::abs(value),ProceduralDsp::silence(memberPeak*.99993056f));
                    const double shaped=mSongStrip.equalize(value);
                    const double detector=mSongStrip.detect(shaped);
                    if(auto* master=masterGroup)
                    {
                        if((mBusFrames++&31)==0)
                        {master->publish(mMasterSlot,mMasterToken,float(detector));mLinkedEnvelope=master->envelope();}
                        value=mSongStrip.finish(shaped,mLinkedEnvelope);
                    }
                    else value=mSongStrip.finish(shaped,detector);
                }
                mixed[i]=static_cast<float>(value*mMasterCurrent);
                mixPeak=std::max(std::abs(mixed[i]),ProceduralDsp::silence(mixPeak*0.99993056f));
                output[done+i]=std::tanh(mixed[i]);peak=std::max(peak,std::abs(output[done+i]));
                mMasterPower+=0.00013888*(output[done+i]*output[done+i]-mMasterPower);
                if (mMasterPower<1e-25) mMasterPower=0;
            }
            if(mBusesActive){mMemberPeak=memberPeak;mMemberRms=float(std::sqrt(mMemberPower));mMemberReduction=mMemberStrip.reduction();mSongReduction=mSongStrip.reduction();}
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
