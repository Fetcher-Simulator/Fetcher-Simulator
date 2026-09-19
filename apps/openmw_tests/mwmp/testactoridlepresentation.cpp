#include <gtest/gtest.h>

#include <limits>

#include <apps/openmw/mwrender/animation.hpp>
#include <components/openmw-mp/Base/ActorSyncProtocol.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorPresentationV2.hpp>

namespace
{
    struct AnimationStates : MWRender::Animation
    {
        using Animation::AnimState;
    };

    mwmp::ActorPresentationSnapshot relay(const mwmp::ActorPresentationSnapshot& incoming)
    {
        mwmp::BaseActor stored;
        stored.animFlags.currentAnimGroup = incoming.currentAnimGroup;
        stored.animFlags.currentAnimCompletion = incoming.currentAnimCompletion;
        mwmp::applyActorIdlePresentationFlags(stored, incoming.presentationFlags);

        mwmp::ActorPresentationSnapshot outgoing = incoming;
        outgoing.presentationFlags = mwmp::makeActorPresentationFlags(stored);
        mwmp::ActorPresentationV2List list;
        list.snapshots.push_back(outgoing);
        mwmp::PacketActorPresentationV2 encoder;
        encoder.setPresentationList(&list);
        mwmp::ActorPresentationV2List received;
        mwmp::PacketActorPresentationV2 decoder;
        decoder.setPresentationList(&received);
        EXPECT_TRUE(decoder.decode(encoder.encode()));
        EXPECT_EQ(received.snapshots.size(), 1u);
        return received.snapshots.at(0);
    }
}

TEST(ActorIdlePresentation, OneShotAndRestartEdgeSurviveRelayAndWireRoundTrip)
{
    mwmp::ActorPresentationSnapshot sample;
    sample.currentAnimGroup = "idle2";
    sample.currentAnimCompletion = 0.946502f;
    for (bool parity : { false, true, false })
    {
        sample.presentationFlags = mwmp::ActorPresentationIdleSingleCycle
            | (parity ? mwmp::ActorPresentationIdleEventParity : 0);
        const auto received = relay(sample);
        EXPECT_EQ(received.presentationFlags, sample.presentationFlags);
        EXPECT_EQ(received.currentAnimGroup, "idle2");
        EXPECT_FLOAT_EQ(received.currentAnimCompletion, sample.currentAnimCompletion);
    }
}

TEST(ActorIdlePresentation, BaseIdleRemainsExplicitThroughRelay)
{
    mwmp::ActorPresentationSnapshot sample;
    sample.currentAnimGroup = "idle";
    sample.currentAnimCompletion = -1.f;
    const auto received = relay(sample);
    EXPECT_EQ(received.currentAnimGroup, "idle");
    EXPECT_FLOAT_EQ(received.currentAnimCompletion, -1.f);
    EXPECT_EQ(received.presentationFlags & mwmp::ActorPresentationIdleMask, 0);
}

TEST(ActorIdlePresentation, PositionSamplesCannotOverwriteTheReliableIdleEvent)
{
    mwmp::BaseActor observer;
    observer.animFlags.idleSingleCycle = true;
    observer.animFlags.idleEventParity = true;
    mwmp::CompactActorSnapshot oldPosition;
    oldPosition.presentationFlags = mwmp::ActorPresentationMoving;
    oldPosition.animFwd = 127;
    mwmp::applyCompactActorSnapshotState(observer, oldPosition);
    EXPECT_TRUE(observer.isMoving);
    EXPECT_TRUE(observer.animFlags.idleSingleCycle);
    EXPECT_TRUE(observer.animFlags.idleEventParity);
    EXPECT_EQ(mwmp::makeCompactActorSnapshot(observer, 1).presentationFlags & mwmp::ActorPresentationIdleMask, 0);
}

TEST(ActorIdlePresentation, LegacyOrScriptedIdleRetainsAuthoredLoopBehavior)
{
    mwmp::BaseActor actor;
    actor.animFlags.idleSingleCycle = true;
    actor.animFlags.idleEventParity = true;
    mwmp::applyActorIdlePresentationFlags(actor, 0);
    EXPECT_FALSE(actor.animFlags.idleSingleCycle);
    EXPECT_FALSE(actor.animFlags.idleEventParity);

    AnimationStates::AnimState looping;
    looping.mStartTime = 0.f;
    looping.mStopTime = 15.f;
    looping.mLoopStartTime = 0.f;
    looping.mLoopStopTime = 12.333333f;
    looping.mLoopCount = std::numeric_limits<uint32_t>::max();
    looping.setTime(14.f);
    EXPECT_TRUE(looping.shouldLoop());
}

TEST(ActorIdlePresentation, OneShotDoesNotWrapAtAnEarlierLoopStop)
{
    // Reproduces the shape of the observed idle2 failure: still before the
    // full stop, but already past the internal loop stop. One-shot authority
    // and observer must continue through the tail instead of jumping early.
    AnimationStates::AnimState oneShot;
    oneShot.mStartTime = 0.f;
    oneShot.mStopTime = 15.f;
    oneShot.mLoopStartTime = 0.f;
    oneShot.mLoopStopTime = 12.333333f;
    oneShot.mLoopCount = 0;
    oneShot.setTime(14.f);
    EXPECT_FALSE(oneShot.shouldLoop());
    EXPECT_FLOAT_EQ(oneShot.getCompletion(), 14.f / 15.f);
    oneShot.setTime(15.f);
    EXPECT_FALSE(oneShot.shouldLoop());
    EXPECT_FLOAT_EQ(oneShot.getCompletion(), 1.f);
}

TEST(ActorIdlePresentation, PlaybackModeChangeRecoversWithoutAParityEdge)
{
    // Reproduce startup: the observer first received an unmarked idle, then
    // received a single-cycle play whose parity also happened to be false.
    mwmp::BaseActor observer;
    const auto incoming = mwmp::ActorPresentationIdleSingleCycle;
    EXPECT_TRUE(mwmp::hasActorIdlePlaybackChange(observer.animFlags, incoming));
    mwmp::applyActorIdlePresentationFlags(observer, incoming);
    EXPECT_FALSE(mwmp::hasActorIdlePlaybackChange(observer.animFlags, incoming));
    EXPECT_TRUE(mwmp::hasActorIdlePlaybackChange(observer.animFlags, 0));
    EXPECT_TRUE(mwmp::hasActorIdlePlaybackChange(observer.animFlags,
        incoming | mwmp::ActorPresentationIdleEventParity));
}

TEST(ActorIdlePresentation, PeriodicRefreshAndOtherFlagsDoNotRestartAnIdle)
{
    mwmp::BaseActor observer;
    for (uint8_t idleFlags : { uint8_t(0), uint8_t(mwmp::ActorPresentationIdleSingleCycle),
             uint8_t(mwmp::ActorPresentationIdleEventParity), uint8_t(mwmp::ActorPresentationIdleMask) })
    {
        mwmp::applyActorIdlePresentationFlags(observer, idleFlags);
        for (uint8_t otherFlags = 0; otherFlags < mwmp::ActorPresentationIdleSingleCycle; ++otherFlags)
            EXPECT_FALSE(mwmp::hasActorIdlePlaybackChange(observer.animFlags, idleFlags | otherFlags));
    }
}
