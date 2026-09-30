#include <gtest/gtest.h>
#include <components/openmw-mp/Base/ActorGuardPresentation.hpp>
#include <components/openmw-mp/Base/ActorCellHandoff.hpp>
#include <components/openmw-mp/Base/ActorReliableEvent.hpp>
#include <apps/openmw/mwmechanics/actorsupport.hpp>
#include <components/openmw-mp/Base/ActorAttackHandoff.hpp>
#include <components/openmw-mp/Base/ActorAuthorityEligibility.hpp>
#include <components/openmw-mp/Base/ActorAuthorityTransfer.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorAuthorityTransfer.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorPositionV2.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorCellChange.hpp>

TEST(ActorAuthorityEligibility, CrossingBeyondOwnerHorizonCannotClearCombat)
{
    // Mario owns three combatants in an adjacent cell. When Sith moves two
    // cells away, his proxy disappears on Mario's client, which reports idle.
    // The server must retain Combat until an eligible simulator takes over.
    EXPECT_EQ(mwmp::remotePlayerGuidFromActorTarget("mp_remote_4"), 4u);
    EXPECT_TRUE(mwmp::exteriorCellWithinActorAuthorityRadius(
        -120, -55, -120, -56, 1)); // Mario still has the NPC cell.
    EXPECT_FALSE(mwmp::exteriorCellWithinActorAuthorityRadius(
        -120, -55, -119, -57, 1)); // Sith's proxy has left Mario's cells.
    EXPECT_TRUE(mwmp::exteriorCellWithinActorAuthorityRadius(
        -119, -57, -120, -56, 1)); // Sith can simulate the NPC cell.
    EXPECT_TRUE(mwmp::shouldPreserveCombatAcrossTargetHorizon(
        true, false, true, false));

    // Actual combat end remains possible when the target is present, and a
    // disconnected target must not leave an actor permanently locked in combat.
    EXPECT_FALSE(mwmp::shouldPreserveCombatAcrossTargetHorizon(
        true, false, true, true));
    EXPECT_FALSE(mwmp::shouldPreserveCombatAcrossTargetHorizon(
        true, false, false, false));
    EXPECT_FALSE(mwmp::shouldPreserveCombatAcrossTargetHorizon(
        true, true, true, false));
    EXPECT_FALSE(mwmp::shouldPreserveCombatAcrossTargetHorizon(
        false, false, true, false));
    EXPECT_FALSE(mwmp::remotePlayerGuidFromActorTarget("mp_remote_0"));
    EXPECT_FALSE(mwmp::remotePlayerGuidFromActorTarget("mp_remote_4junk"));
}

TEST(ActorAuthorityEligibility, ActorSimulatorNeedsActorAndTargetCoverage)
{
    // Crossing a cell or switching combat targets does not itself require a
    // transfer while the current owner covers both the actor and target.
    EXPECT_TRUE(mwmp::shouldRetainActorSimulator(true, true));
    // An actor or live combat target leaving the owner's horizon needs a handoff.
    EXPECT_FALSE(mwmp::shouldRetainActorSimulator(false, true));
    EXPECT_FALSE(mwmp::shouldRetainActorSimulator(true, false));
}

TEST(ActorAuthorityEligibility, CandidateMustHaveRoomToPathBeforeAuthorityTransfer)
{
    // Mario is centered in -57. This actor has just entered -56, but is only
    // 66 units from the outer edge of Mario's 3x3 grid. Sith, centered in -56,
    // can still run its pathfinding.
    EXPECT_FALSE(mwmp::exteriorActorWithinAiBounds(-120, -57, -982000.f, -450626.f, 512.f));
    EXPECT_TRUE(mwmp::exteriorActorWithinAiBounds(-120, -56, -982000.f, -450626.f, 512.f));
    EXPECT_TRUE(mwmp::exteriorActorWithinAiBounds(-120, -57, -982000.f, -451100.f, 512.f));
    EXPECT_FALSE(mwmp::exteriorActorWithinAiBounds(-120, -57, -982000.f, -450626.f, 200.f));
}

TEST(ActorAuthorityTransfer, ReadyMustMatchTheExactPreparedActorAndGeneration)
{
    mwmp::ActorAuthorityTransfer prepare;
    prepare.actorNetId = 335636026;
    prepare.transferId = 19;
    prepare.oldOwnerGuid = 3;
    prepare.newOwnerGuid = 4;
    prepare.authorityGeneration = 2;
    prepare.migrationGeneration = 11;
    prepare.cellId = "EXT:-119,-56";
    prepare.actor.refId = "pc_m1_guard_anv_mw";
    prepare.actor.refNum = 335636026;
    prepare.actor.cellId = prepare.cellId;
    prepare.actor.ai.type = mwmp::BaseActor::AIAction::Type::Combat;
    prepare.actor.ai.targetId = "mp_remote_4";
    prepare.actor.ai.combatPlayerTargets = { "mp_remote_3", "mp_remote_4" };
    prepare.actor.deathEventId = 12;
    prepare.actor.deathCauseCombatEventId = 123456;

    mwmp::PacketActorAuthorityTransfer encoder;
    encoder.setTransfer(&prepare);
    mwmp::ActorAuthorityTransfer received;
    mwmp::PacketActorAuthorityTransfer decoder;
    decoder.setTransfer(&received);
    ASSERT_TRUE(decoder.decode(encoder.encode()));
    EXPECT_EQ(received.actorNetId, prepare.actorNetId);
    EXPECT_EQ(received.actor.ai.type, prepare.actor.ai.type);
    EXPECT_EQ(received.actor.ai.targetId, prepare.actor.ai.targetId);
    EXPECT_EQ(received.actor.ai.combatPlayerTargets, prepare.actor.ai.combatPlayerTargets);
    EXPECT_EQ(received.actor.deathEventId, prepare.actor.deathEventId);
    EXPECT_EQ(received.actor.deathCauseCombatEventId, prepare.actor.deathCauseCombatEventId);

    mwmp::ActorAuthorityTransfer ready = received;
    ready.phase = mwmp::ActorAuthorityTransferPhase::Ready;
    mwmp::PacketActorAuthorityTransfer readyEncoder;
    readyEncoder.setTransfer(&ready);
    mwmp::ActorAuthorityTransfer receivedReady;
    mwmp::PacketActorAuthorityTransfer readyDecoder;
    readyDecoder.setTransfer(&receivedReady);
    ASSERT_TRUE(readyDecoder.decode(readyEncoder.encode()));
    EXPECT_TRUE(mwmp::matchesActorAuthorityTransferReady(received, receivedReady, 4));
    EXPECT_FALSE(mwmp::matchesActorAuthorityTransferReady(received, ready, 3));
    ++ready.transferId;
    EXPECT_FALSE(mwmp::matchesActorAuthorityTransferReady(received, ready, 4));
    --ready.transferId;
    ++ready.authorityGeneration;
    EXPECT_FALSE(mwmp::matchesActorAuthorityTransferReady(received, ready, 4));
    --ready.authorityGeneration;
    ++ready.migrationGeneration;
    EXPECT_FALSE(mwmp::matchesActorAuthorityTransferReady(received, ready, 4));

    // Preparation can be delayed while the old owner keeps moving. The
    // commit carries the newer canonical state, not the prepare snapshot.
    mwmp::ActorAuthorityTransfer commit = received;
    commit.phase = mwmp::ActorAuthorityTransferPhase::Commit;
    commit.actor.position.pos[0] = 1234.f;
    mwmp::PacketActorAuthorityTransfer commitEncoder;
    commitEncoder.setTransfer(&commit);
    mwmp::ActorAuthorityTransfer receivedCommit;
    mwmp::PacketActorAuthorityTransfer commitDecoder;
    commitDecoder.setTransfer(&receivedCommit);
    ASSERT_TRUE(commitDecoder.decode(commitEncoder.encode()));
    EXPECT_TRUE(mwmp::matchesActorAuthorityTransferCommit(received, receivedCommit));
    EXPECT_FLOAT_EQ(receivedCommit.actor.position.pos[0], 1234.f);
    EXPECT_EQ(receivedCommit.actor.ai.targetId, "mp_remote_4");
    EXPECT_EQ(receivedCommit.actor.ai.combatPlayerTargets, prepare.actor.ai.combatPlayerTargets);
    ++receivedCommit.transferId;
    EXPECT_FALSE(mwmp::matchesActorAuthorityTransferCommit(received, receivedCommit));

    mwmp::ActorAuthorityTransfer applied = commit;
    applied.phase = mwmp::ActorAuthorityTransferPhase::Applied;
    mwmp::PacketActorAuthorityTransfer appliedEncoder;
    appliedEncoder.setTransfer(&applied);
    mwmp::ActorAuthorityTransfer receivedApplied;
    mwmp::PacketActorAuthorityTransfer appliedDecoder;
    appliedDecoder.setTransfer(&receivedApplied);
    ASSERT_TRUE(appliedDecoder.decode(appliedEncoder.encode()));
    EXPECT_TRUE(mwmp::matchesActorAuthorityTransferApplied(commit, receivedApplied, 4));
    EXPECT_FALSE(mwmp::matchesActorAuthorityTransferApplied(commit, receivedApplied, 3));
    ++receivedApplied.authorityGeneration;
    EXPECT_FALSE(mwmp::matchesActorAuthorityTransferApplied(commit, receivedApplied, 4));
    --receivedApplied.authorityGeneration;
    ++receivedApplied.migrationGeneration;
    EXPECT_FALSE(mwmp::matchesActorAuthorityTransferApplied(commit, receivedApplied, 4));
    --receivedApplied.migrationGeneration;
    ++receivedApplied.transferId;
    EXPECT_FALSE(mwmp::matchesActorAuthorityTransferApplied(commit, receivedApplied, 4));

    mwmp::ActorAuthorityTransfer cancel = received;
    cancel.phase = mwmp::ActorAuthorityTransferPhase::Cancel;
    mwmp::PacketActorAuthorityTransfer cancelEncoder;
    cancelEncoder.setTransfer(&cancel);
    mwmp::ActorAuthorityTransfer receivedCancel;
    mwmp::PacketActorAuthorityTransfer cancelDecoder;
    cancelDecoder.setTransfer(&receivedCancel);
    ASSERT_TRUE(cancelDecoder.decode(cancelEncoder.encode()));
    EXPECT_TRUE(mwmp::matchesActorAuthorityTransferCancel(received, receivedCancel));
    ++receivedCancel.transferId;
    EXPECT_FALSE(mwmp::matchesActorAuthorityTransferCancel(received, receivedCancel));
}

TEST(ActorAuthorityTransfer, DelayedPrepareCannotReplaceCommittedOrNewerTransaction)
{
    mwmp::ActorAuthorityTransfer prepare;
    prepare.actorNetId = 335636026;
    prepare.transferId = 19;
    prepare.newOwnerGuid = 4;
    prepare.authorityGeneration = 2;
    EXPECT_TRUE(mwmp::shouldAcceptActorAuthorityTransferPrepare(prepare, nullptr, true, 1, 3));

    mwmp::ActorAuthorityTransfer newer = prepare;
    newer.transferId = 20;
    EXPECT_FALSE(mwmp::shouldAcceptActorAuthorityTransferPrepare(prepare, &newer, true, 1, 3));
    EXPECT_TRUE(mwmp::shouldAcceptActorAuthorityTransferPrepare(newer, &prepare, true, 1, 3));
    EXPECT_FALSE(mwmp::shouldAcceptActorAuthorityTransferPrepare(prepare, nullptr, true, 2, 4));
    EXPECT_FALSE(mwmp::shouldAcceptActorAuthorityTransferPrepare(prepare, nullptr, true, 3, 0));
}

TEST(ActorAuthorityTransfer, RevokedLeaseCannotBeResurrectedByOlderGrant)
{
    EXPECT_TRUE(mwmp::shouldAcceptActorAuthorityLease(false, 0, 0, 1, 3));
    EXPECT_TRUE(mwmp::shouldAcceptActorAuthorityLease(true, 2, 0, 3, 4));
    EXPECT_TRUE(mwmp::shouldAcceptActorAuthorityLease(true, 2, 0, 2, 0));
    EXPECT_FALSE(mwmp::shouldAcceptActorAuthorityLease(true, 2, 0, 1, 3));
    EXPECT_FALSE(mwmp::shouldAcceptActorAuthorityLease(true, 2, 0, 2, 3));
    EXPECT_FALSE(mwmp::shouldAcceptActorAuthorityLease(true, 3, 4, 3, 5));
}

TEST(ActorCellHandoff, PhysicalCrossingCannotCollectBeforeCommitOrAfterDemotion)
{
    const std::string source = "EXT:-120,-55";
    const std::string destination = "EXT:-121,-55";
    mwmp::ActorList commit;
    commit.cellId = source;
    mwmp::BaseActor actor;
    actor.refNum = 335636026;
    actor.refId = "pc_m1_guard_anv_mw";
    actor.cellId = destination;
    actor.migrationGeneration = 5;
    actor.hasWeaponDrawn = true;
    actor.ai.type = mwmp::BaseActor::AIAction::Type::Combat;
    actor.ai.targetId = "mp_remote_2";
    commit.actors.push_back(actor);

    // A physically crossed puppet still belongs to the source before commit.
    EXPECT_TRUE(mwmp::shouldCollectActorInCanonicalCell(source, source));
    EXPECT_FALSE(mwmp::shouldCollectActorInCanonicalCell(source, destination));
    mwmp::PacketActorCellChange encoder;
    encoder.setActorList(&commit);
    mwmp::ActorList received;
    mwmp::PacketActorCellChange decoder;
    decoder.setActorList(&received);
    ASSERT_TRUE(decoder.decode(encoder.encode()));
    ASSERT_EQ(received.actors.size(), 1u);
    const auto& canonical = received.actors.front();
    EXPECT_EQ(canonical.migrationGeneration, 5u);
    EXPECT_TRUE(canonical.hasWeaponDrawn);
    EXPECT_EQ(canonical.ai.type, mwmp::BaseActor::AIAction::Type::Combat);
    EXPECT_EQ(canonical.ai.targetId, "mp_remote_2");
    // After commit, a Ptr retained in the source cannot regain authority.
    EXPECT_FALSE(mwmp::shouldCollectActorInCanonicalCell(canonical.cellId, source));
    EXPECT_TRUE(mwmp::shouldCollectActorInCanonicalCell(canonical.cellId, destination));
    // Undiscovered references still enter normal discovery.
    EXPECT_TRUE(mwmp::shouldCollectActorInCanonicalCell({}, source));
}

TEST(ActorCellHandoff, SameGenerationCommitStillAppliesWhenCanonicalCellIsOld)
{
    EXPECT_TRUE(mwmp::shouldIgnoreDuplicateActorCellChange(
        5, 5, "EXT:-120,-55", "EXT:-120,-55"));
    EXPECT_FALSE(mwmp::shouldIgnoreDuplicateActorCellChange(
        5, 5, "EXT:-121,-55", "EXT:-120,-55"));
    EXPECT_FALSE(mwmp::shouldIgnoreDuplicateActorCellChange(
        4, 5, "EXT:-121,-55", "EXT:-120,-55"));
    EXPECT_FALSE(mwmp::shouldIgnoreDuplicateActorCellChange(
        0, 0, "EXT:-121,-55", "EXT:-120,-55"));
}

TEST(ActorCellHandoff, CompleteActorListRetainsCanonicalRuntimeMissedByCollection)
{
    EXPECT_TRUE(mwmp::shouldRetainCanonicalRuntimeInCompleteActorList(
        "EXT:-120,-55", "EXT:-120,-55", false));
    EXPECT_FALSE(mwmp::shouldRetainCanonicalRuntimeInCompleteActorList(
        "EXT:-121,-55", "EXT:-120,-55", false));
    EXPECT_FALSE(mwmp::shouldRetainCanonicalRuntimeInCompleteActorList(
        "EXT:-120,-55", "EXT:-120,-55", true));
}

TEST(ActorCellHandoff, AuthorityWireBridgeStartsAtCommitAndDecaysSmoothly)
{
    EXPECT_FLOAT_EQ(mwmp::authorityHandoffWireBridgeDuration(44.f, true), 0.44f);
    EXPECT_FLOAT_EQ(mwmp::authorityHandoffWireBridgeDuration(10.f, true), 0.3f);
    EXPECT_FLOAT_EQ(mwmp::authorityHandoffWireBridgeDuration(58.f, true), 0.58f);
    EXPECT_FLOAT_EQ(mwmp::authorityHandoffWireBridgeDuration(129.f, true), 0.f);
    EXPECT_FLOAT_EQ(mwmp::authorityHandoffWireBridgeDuration(44.f, false), 0.f);

    EXPECT_FLOAT_EQ(mwmp::authorityHandoffWireBridgeScale(0.44f, 0.44f), 1.f);
    EXPECT_FLOAT_EQ(mwmp::authorityHandoffWireBridgeScale(0.22f, 0.44f), 0.5f);
    EXPECT_FLOAT_EQ(mwmp::authorityHandoffWireBridgeScale(0.f, 0.44f), 0.f);
}

TEST(ActorAttackHandoff, OnlyPreviousOwnerCellAndGenerationWithinGrace)
{
    const mwmp::ActorAttackHandoff h { "EXT:-120,-55", 1, 7, 1000 };
    EXPECT_TRUE(h.accepts(h.cellId, 1, 1, 7, 1000));
    EXPECT_TRUE(h.accepts(h.cellId, 1, 1, 7, 1500));
    EXPECT_FALSE(h.accepts(h.cellId, 1, 1, 7, 1501));
    EXPECT_FALSE(h.accepts(h.cellId, 1, 1, 7, 999));
    EXPECT_FALSE(h.accepts(h.cellId, 2, 1, 7, 1100));
    EXPECT_FALSE(h.accepts(h.cellId, 1, 2, 7, 1100));
    EXPECT_FALSE(h.accepts(h.cellId, 1, 1, 6, 1100));
    EXPECT_FALSE(h.accepts(h.cellId, 1, 1, 8, 1100));
    EXPECT_FALSE(h.accepts("EXT:-121,-55", 1, 1, 7, 1100));
    EXPECT_FALSE(mwmp::ActorAttackHandoff{}.accepts("", 0, 0, 0, 0));
}

TEST(ActorCellHandoff, IdentityRefreshCannotUndoCommittedGenerationOrCell)
{
    const std::string source = "EXT:-121,-55";
    const std::string destination = "EXT:-120,-55";
    // A delayed source identity follows the gen-7 commit from the 16:12 run.
    EXPECT_TRUE(mwmp::shouldRejectActorIdentityRefresh(7, 6, destination, source, false));
    EXPECT_TRUE(mwmp::shouldRejectActorIdentityRefresh(7, 7, destination, source, false));
    EXPECT_TRUE(mwmp::shouldRejectActorIdentityRefresh(7, 0, destination, source, false));
    EXPECT_TRUE(mwmp::shouldRejectActorIdentityRefresh(7, 6, destination, source, true));
    EXPECT_FALSE(mwmp::shouldRejectActorIdentityRefresh(7, 7, destination, destination, false));
    EXPECT_FALSE(mwmp::shouldRejectActorIdentityRefresh(7, 8, destination, source, false));
    EXPECT_FALSE(mwmp::shouldRejectActorIdentityRefresh(7, 7, destination, source, true));
    EXPECT_FALSE(mwmp::shouldRejectActorIdentityRefresh(0, 1, {}, source, false));
    // Generation rollover is still a forward migration.
    EXPECT_FALSE(mwmp::shouldRejectActorIdentityRefresh(0xffffffffu, 1, destination, source, false));
    EXPECT_TRUE(mwmp::shouldRejectActorIdentityRefresh(1, 0xffffffffu, destination, source, false));
}

TEST(ActorCellHandoff, OrdinaryIdentityRefreshDoesNotForcePhysicalCellBinding)
{
    EXPECT_FALSE(mwmp::shouldForceActorIdentityBinding(true, false, false));
    EXPECT_TRUE(mwmp::shouldForceActorIdentityBinding(false, false, false));
    EXPECT_TRUE(mwmp::shouldForceActorIdentityBinding(true, true, false));
    EXPECT_TRUE(mwmp::shouldForceActorIdentityBinding(true, false, true));
}
TEST(ActorAttackHandoff, ReplaysAndInterleavedOwners)
{
    mwmp::ActorAttackReplayGuard r;
    EXPECT_TRUE(r.accept("A", 1, 7, 60));
    EXPECT_TRUE(r.accept("A", 2, 8, 1));
    EXPECT_TRUE(r.accept("A", 1, 7, 61));
    EXPECT_FALSE(r.accept("A", 1, 7, 60));
    EXPECT_FALSE(r.accept("A", 2, 8, 1));
    EXPECT_TRUE(r.accept("A", 2, 8, 2));
    EXPECT_EQ(r.relayEventId, 4u);
    EXPECT_FALSE(r.accept("A", 2, 8, 0));
    EXPECT_TRUE(r.accept("B", 2, 8, 1)); // cell counters are independent
    EXPECT_EQ(r.relayEventId, 5u);
}
TEST(ActorAttackHandoff, EventCounterWrap)
{
    mwmp::ActorAttackReplayGuard r;
    EXPECT_TRUE(r.accept("A", 1, 1, 0xffffffffu));
    EXPECT_TRUE(r.accept("A", 1, 1, 1));
    EXPECT_FALSE(r.accept("A", 1, 1, 0xffffffffu));
    r.relayEventId = 0xffffffffu;
    EXPECT_TRUE(r.accept("A", 1, 1, 2));
    EXPECT_EQ(r.relayEventId, 1u);
}
TEST(ActorCellHandoff, SeamlessHandoffKeepsSourceBindingUntilVisualCrossesDestination)
{
    EXPECT_TRUE(mwmp::shouldDeferObserverCellStoreRebind(true, false, true, false));
    EXPECT_TRUE(mwmp::shouldDeferObserverCellStoreRebind(true, true, true, false));
    EXPECT_FALSE(mwmp::shouldDeferObserverCellStoreRebind(true, false, true, true));
    EXPECT_FALSE(mwmp::shouldDeferObserverCellStoreRebind(false, false, true, false));
    EXPECT_FALSE(mwmp::shouldDeferObserverCellStoreRebind(true, false, false, false));
}

TEST(ActorCellHandoff, RemoteLocomotionExpiresAfterMissingStream)
{
    EXPECT_FALSE(mwmp::shouldExpireRemoteLocomotion(0.49f, true, true));
    EXPECT_TRUE(mwmp::shouldExpireRemoteLocomotion(0.5f, true, true));
    EXPECT_TRUE(mwmp::shouldExpireRemoteLocomotion(10.f, true, true));
    EXPECT_FALSE(mwmp::shouldExpireRemoteLocomotion(10.f, false, true));
    EXPECT_FALSE(mwmp::shouldExpireRemoteLocomotion(10.f, true, false));
}

TEST(ActorCellHandoff, FastObserverKeepsRealSpeedAcrossNormalSeamGap)
{
    EXPECT_NEAR(mwmp::cellHandoffPredictionTimeMs(100.0, 750.f), 100.0, 0.001);
    EXPECT_NEAR(mwmp::cellHandoffPredictionTimeMs(150.0, 750.f), 150.0, 0.001);
    EXPECT_NEAR(mwmp::cellHandoffPredictionTimeMs(300.0, 750.f), 190.0, 0.001);
    EXPECT_NEAR(mwmp::cellHandoffPredictionTimeMs(300.0, 200.f), 300.0, 0.001);
}

TEST(ActorCellHandoff, SuppressesExteriorActorListOnlyPastHysteresis)
{
    EXPECT_TRUE(mwmp::shouldSuppressExteriorActorListCellMismatch(true, false, 65.f, true));
    EXPECT_FALSE(mwmp::shouldSuppressExteriorActorListCellMismatch(true, false, 64.f, true));
    EXPECT_FALSE(mwmp::shouldSuppressExteriorActorListCellMismatch(true, true, 128.f, true));
    EXPECT_FALSE(mwmp::shouldSuppressExteriorActorListCellMismatch(false, false, 128.f, true));
    EXPECT_FALSE(mwmp::shouldSuppressExteriorActorListCellMismatch(true, false, 128.f, false));
}

TEST(ActorCellHandoff, VanillaActorListMigrationRequiresDestinationCellCoordinates)
{
    EXPECT_TRUE(mwmp::shouldSuppressExteriorActorListCanonicalMigration(false, false, 128.f));
    EXPECT_TRUE(mwmp::shouldSuppressExteriorActorListCanonicalMigration(true, false, 128.f));
    EXPECT_FALSE(mwmp::shouldSuppressExteriorActorListCanonicalMigration(true, true, 0.f));
    EXPECT_FALSE(mwmp::shouldSuppressExteriorActorListCanonicalMigration(true, true, 20.f));
    EXPECT_FALSE(mwmp::shouldSuppressExteriorActorListCanonicalMigration(true, true, 65.f));
}

TEST(ActorCellHandoff, PreservesSourceAiOnlyForVanillaCrossCellMigration)
{
    EXPECT_TRUE(mwmp::shouldPreserveSourceAiAcrossVanillaMigration(true, 0, true));
    EXPECT_FALSE(mwmp::shouldPreserveSourceAiAcrossVanillaMigration(false, 0, true));
    EXPECT_FALSE(mwmp::shouldPreserveSourceAiAcrossVanillaMigration(true, 42, true));
    EXPECT_FALSE(mwmp::shouldPreserveSourceAiAcrossVanillaMigration(true, 0, false));
}

TEST(ActorCellHandoff, IdentityMigrationMergesCanonicalAiWhilePreservingTransform)
{
    EXPECT_TRUE(mwmp::shouldMergeCanonicalAiOnIdentityMigration(true, false, false, true));
    EXPECT_FALSE(mwmp::shouldMergeCanonicalAiOnIdentityMigration(false, false, false, true));
    EXPECT_FALSE(mwmp::shouldMergeCanonicalAiOnIdentityMigration(true, true, false, true));
    EXPECT_FALSE(mwmp::shouldMergeCanonicalAiOnIdentityMigration(true, false, true, true));
    EXPECT_FALSE(mwmp::shouldMergeCanonicalAiOnIdentityMigration(true, false, false, false));
}

TEST(ActorCellHandoff, PrimaryAuthorityFallbackCoversMissedCellStoreEnumeration)
{
    EXPECT_FALSE(mwmp::shouldCollectPrimaryForExplicitCellChange(false, true, true));
    EXPECT_TRUE(mwmp::shouldCollectPrimaryForExplicitCellChange(false, true, false));
    EXPECT_TRUE(mwmp::shouldCollectPrimaryForExplicitCellChange(true, false, true));
    EXPECT_TRUE(mwmp::shouldCollectPrimaryForExplicitCellChange(false, false, false));
    EXPECT_FALSE(mwmp::shouldCollectPrimaryForExplicitCellChange(false, false, true));
}

TEST(ActorCellHandoff, AcceptsOnlyMatchingRecentLateExplicitVanillaCommit)
{
    EXPECT_TRUE(mwmp::shouldAcceptLateExplicitVanillaCellChange(
        true, 0, true, true, true, 50));
    EXPECT_TRUE(mwmp::shouldAcceptLateExplicitVanillaCellChange(
        true, 0, true, true, true, 1000));
    EXPECT_FALSE(mwmp::shouldAcceptLateExplicitVanillaCellChange(
        true, 0, true, true, true, 1001));
    EXPECT_FALSE(mwmp::shouldAcceptLateExplicitVanillaCellChange(
        false, 0, true, true, true, 50));
    EXPECT_FALSE(mwmp::shouldAcceptLateExplicitVanillaCellChange(
        true, 42, true, true, true, 50));
    EXPECT_FALSE(mwmp::shouldAcceptLateExplicitVanillaCellChange(
        true, 0, false, true, true, 50));
    EXPECT_FALSE(mwmp::shouldAcceptLateExplicitVanillaCellChange(
        true, 0, true, false, true, 50));
    EXPECT_FALSE(mwmp::shouldAcceptLateExplicitVanillaCellChange(
        true, 0, true, true, false, 50));
}

TEST(ActorCellHandoff, DestinationActorListCannotPreemptLiveSourceAuthority)
{
    EXPECT_TRUE(mwmp::shouldSuppressDestinationActorListMigration(true, true));
    EXPECT_FALSE(mwmp::shouldSuppressDestinationActorListMigration(true, false));
    EXPECT_FALSE(mwmp::shouldSuppressDestinationActorListMigration(false, true));
}

TEST(ActorCellHandoff, CanonicalCombatSurvivesLostPuppetMarkers)
{
    EXPECT_TRUE(mwmp::shouldRestoreCanonicalCombat(true, false, false));
    EXPECT_TRUE(mwmp::shouldRestoreCanonicalCombat(false, true, false));
    EXPECT_TRUE(mwmp::shouldRestoreCanonicalCombat(false, false, true));
    EXPECT_FALSE(mwmp::shouldRestoreCanonicalCombat(false, false, false));
}

TEST(ActorReliableEvent, SentIdentityDoesNotWaitForRoundTripAck)
{
    EXPECT_TRUE(mwmp::canRelayReliableActorEvent(true, false));
    EXPECT_TRUE(mwmp::canRelayReliableActorEvent(true, true));
    EXPECT_FALSE(mwmp::canRelayReliableActorEvent(false, false));
    EXPECT_FALSE(mwmp::canRelayReliableActorEvent(false, true));
}

TEST(ActorSupportPresentation, RestingContactDoesNotRequirePhysicalGroundOrZeroGravity)
{
    EXPECT_TRUE(MWMechanics::hasStableActorSupport(true, false, 0, -1000, 0, 0.016f));
    EXPECT_TRUE(MWMechanics::hasStableActorSupport(true, false, 0, -1000, -0.1f, 0.016f));
}
TEST(ActorSupportPresentation, JumpFallSlopeFlightAndInvalidTimeStayAirborne)
{
    EXPECT_FALSE(MWMechanics::hasStableActorSupport(true, false, 1, 0, 0, 0.016f));
    EXPECT_FALSE(MWMechanics::hasStableActorSupport(true, false, 0, 100, 0, 0.016f));
    EXPECT_FALSE(MWMechanics::hasStableActorSupport(true, false, 0, -100, -2, 0.016f));
    EXPECT_FALSE(MWMechanics::hasStableActorSupport(false, false, 0, -100, 0, 0.016f));
    EXPECT_FALSE(MWMechanics::hasStableActorSupport(true, true, 0, -100, 0, 0.016f));
    EXPECT_FALSE(MWMechanics::hasStableActorSupport(true, false, 0, -100, 0, 0));
}
TEST(ActorGuardPresentation, HostilityMetadataSurvivesCompactPositionWire)
{
    mwmp::ActorPositionV2List list;
    mwmp::CompactActorSnapshot sample;
    sample.actorNetId = mwmp::packActorInstanceKey({ mwmp::ActorKeyKind::SpawnedMpNum, 6940 });
    sample.movementFlags = mwmp::AnimFlags::MF_GUARD_HOSTILE;
    list.snapshots.push_back(sample);
    mwmp::PacketActorPositionV2 encoder;
    encoder.setPositionList(&list);
    mwmp::ActorPositionV2List result;
    mwmp::PacketActorPositionV2 decoder;
    decoder.setPositionList(&result);
    ASSERT_TRUE(decoder.decode(encoder.encode()));
    ASSERT_EQ(result.snapshots.size(), 1u);
    EXPECT_EQ(result.snapshots[0].movementFlags, mwmp::AnimFlags::MF_GUARD_HOSTILE);
}


TEST(ActorGuardPresentation, CombatAloneDoesNotMakeFollowersOrPassiveCreaturesHostile)
{
    EXPECT_TRUE(mwmp::isGuardHostileCreature(false, true, true, 100, false));
    EXPECT_FALSE(mwmp::isGuardHostileCreature(false, true, true, 100, true));
    EXPECT_FALSE(mwmp::isGuardHostileCreature(false, true, true, 99, false));
    EXPECT_FALSE(mwmp::isGuardHostileCreature(false, true, false, 100, false));
    EXPECT_FALSE(mwmp::isGuardHostileCreature(false, false, true, 100, false));
    EXPECT_FALSE(mwmp::isGuardHostileCreature(true, true, true, 100, false));
}
