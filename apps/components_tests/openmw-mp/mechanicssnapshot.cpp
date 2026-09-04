#include <limits>

#include <gtest/gtest.h>

#include <apps/openmw-server/ActorRegistryInvariant.hpp>
#include <components/openmw-mp/Base/MechanicsSnapshot.hpp>
#include <components/openmw-mp/MasterServerProtocol.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorDeath.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorIdentity.hpp>
#include <components/openmw-mp/Packets/Actor/PacketMechanicsSnapshot.hpp>

namespace
{
    mwmp::MechanicsSnapshot makePlayerSnapshot()
    {
        mwmp::MechanicsSnapshot snapshot;
        snapshot.kind = mwmp::MechanicsSubjectKind::Player;
        snapshot.playerGuid = 42;
        snapshot.cellId = "EXT:-2,-9";
        snapshot.position.pos[0] = -17000.f;
        snapshot.position.pos[1] = -73000.f;
        snapshot.position.pos[2] = 128.f;
        snapshot.position.rot[2] = 1.5f;
        snapshot.stateFlags = mwmp::MechanicsEnabled | mwmp::MechanicsAlive
            | mwmp::MechanicsConscious | mwmp::MechanicsOnGround | mwmp::MechanicsWerewolf;
        snapshot.sneakSkill = 31.f;
        snapshot.agility = 52.f;
        snapshot.luck = 44.f;
        snapshot.fatigueCurrent = 90.f;
        snapshot.fatigueMaximumModified = 100.f;
        snapshot.chameleon = 10.f;
        snapshot.migrationGeneration = 1;
        snapshot.authorityGeneration = 42;
        snapshot.snapshotSequence = 7;
        return snapshot;
    }

    mwmp::MechanicsSnapshot makeNpcSnapshot()
    {
        mwmp::MechanicsSnapshot snapshot = makePlayerSnapshot();
        snapshot.kind = mwmp::MechanicsSubjectKind::Npc;
        snapshot.playerGuid = 0;
        snapshot.actorInstanceId
            = mwmp::packActorInstanceKey({ mwmp::ActorKeyKind::VanillaRefNum, 1234 });
        snapshot.migrationGeneration = 9;
        snapshot.authorityGeneration = 11;
        snapshot.witnessStateFlags = mwmp::MechanicsWitnessRelationshipKnown
            | mwmp::MechanicsWitnessHasCombatTarget | mwmp::MechanicsWitnessEffectiveAlarmKnown
            | mwmp::MechanicsWitnessEffectiveFightKnown;
        snapshot.effectiveAlarm = 100;
        snapshot.effectiveFight = 30;
        snapshot.combatTargetKind = mwmp::MechanicsSubjectKind::Player;
        snapshot.combatTargetPlayerGuid = 42;
        return snapshot;
    }

    std::vector<std::uint8_t> encode(mwmp::MechanicsSnapshotBatch& batch)
    {
        mwmp::PacketMechanicsSnapshot packet;
        packet.setBatch(&batch);
        return packet.encode();
    }
}

TEST(MechanicsSnapshotProtocol, MultiplayerProtocolIsNineteen)
{
    EXPECT_EQ(mwmp::MultiplayerProtocolVersion, 19);
}

TEST(MechanicsSnapshotProtocol, CanonicalRoundTripIsDeterministic)
{
    mwmp::MechanicsSnapshotBatch outgoing;
    outgoing.snapshots = { makePlayerSnapshot(), makeNpcSnapshot() };
    const auto first = encode(outgoing);
    const auto second = encode(outgoing);
    EXPECT_EQ(first, second);

    mwmp::MechanicsSnapshotBatch incoming;
    mwmp::PacketMechanicsSnapshot decoder;
    decoder.setBatch(&incoming);
    ASSERT_TRUE(decoder.decode(first));
    EXPECT_EQ(incoming, outgoing);
}

TEST(MechanicsSnapshotProtocol, RejectsTruncationTrailingBytesAndHeaderMismatch)
{
    mwmp::MechanicsSnapshotBatch outgoing;
    outgoing.snapshots = { makePlayerSnapshot() };
    const auto bytes = encode(outgoing);

    mwmp::MechanicsSnapshotBatch incoming;
    mwmp::PacketMechanicsSnapshot decoder;
    decoder.setBatch(&incoming);

    auto truncated = bytes;
    truncated.pop_back();
    EXPECT_FALSE(decoder.decode(truncated));

    auto trailing = bytes;
    trailing.push_back(0);
    EXPECT_FALSE(decoder.decode(trailing));

    auto badLength = bytes;
    ++badLength[2];
    EXPECT_FALSE(decoder.decode(badLength));
}

TEST(MechanicsSnapshotProtocol, RejectsIdentityGenerationSequenceAndCellErrors)
{
    auto snapshot = makePlayerSnapshot();
    snapshot.playerGuid = 0;
    EXPECT_FALSE(mwmp::validateMechanicsSnapshot(snapshot));
    snapshot = makeNpcSnapshot();
    snapshot.actorInstanceId = 0;
    EXPECT_FALSE(mwmp::validateMechanicsSnapshot(snapshot));
    snapshot = makeNpcSnapshot();
    snapshot.kind = static_cast<mwmp::MechanicsSubjectKind>(99);
    EXPECT_FALSE(mwmp::validateMechanicsSnapshot(snapshot));
    snapshot = makeNpcSnapshot();
    snapshot.migrationGeneration = 0;
    EXPECT_FALSE(mwmp::validateMechanicsSnapshot(snapshot));
    snapshot = makeNpcSnapshot();
    snapshot.authorityGeneration = 0;
    EXPECT_FALSE(mwmp::validateMechanicsSnapshot(snapshot));
    snapshot = makeNpcSnapshot();
    snapshot.snapshotSequence = 0;
    EXPECT_FALSE(mwmp::validateMechanicsSnapshot(snapshot));
    snapshot = makeNpcSnapshot();
    snapshot.cellId = "EXT:01,2";
    EXPECT_FALSE(mwmp::validateMechanicsSnapshot(snapshot));
    snapshot.cellId = "bad\ncell";
    EXPECT_FALSE(mwmp::validateMechanicsSnapshot(snapshot));
    snapshot = makeNpcSnapshot();
    snapshot.stateFlags |= 0x80;
    EXPECT_FALSE(mwmp::validateMechanicsSnapshot(snapshot));
    snapshot = makeNpcSnapshot();
    snapshot.witnessStateFlags |= 0x80;
    EXPECT_FALSE(mwmp::validateMechanicsSnapshot(snapshot));
    snapshot = makeNpcSnapshot();
    snapshot.effectiveAlarm = 101;
    EXPECT_FALSE(mwmp::validateMechanicsSnapshot(snapshot));
    snapshot = makeNpcSnapshot();
    snapshot.effectiveFight = 101;
    EXPECT_FALSE(mwmp::validateMechanicsSnapshot(snapshot));
    snapshot = makeNpcSnapshot();
    snapshot.witnessStateFlags &= ~mwmp::MechanicsWitnessEffectiveFightKnown;
    EXPECT_FALSE(mwmp::validateMechanicsSnapshot(snapshot));
    snapshot = makeNpcSnapshot();
    snapshot.witnessStateFlags &= ~mwmp::MechanicsWitnessRelationshipKnown;
    EXPECT_FALSE(mwmp::validateMechanicsSnapshot(snapshot));
    snapshot = makeNpcSnapshot();
    snapshot.combatTargetPlayerGuid = 0;
    EXPECT_FALSE(mwmp::validateMechanicsSnapshot(snapshot));
}

TEST(MechanicsSnapshotProtocol, CanonicalActorGenerationSeedsZeroAndPreservesExistingLifetime)
{
    mwmp::BaseActor actor;
    std::uint32_t registryGeneration = 0;
    actor.migrationGeneration = 99;

    mwmp::ensureCanonicalActorMigrationGeneration(registryGeneration, actor);
    EXPECT_EQ(registryGeneration, 1u);
    EXPECT_EQ(actor.migrationGeneration, 1u);

    registryGeneration = 7;
    actor.migrationGeneration = 0;
    mwmp::ensureCanonicalActorMigrationGeneration(registryGeneration, actor);
    EXPECT_EQ(registryGeneration, 7u);
    EXPECT_EQ(actor.migrationGeneration, 7u);
}

TEST(ActorRegistryInvariant, CorpseDisposedIdentityIsValidForPreWorldBootstrap)
{
    mwmp::BaseActor actor;
    actor.refId = "zombie 1 walker";
    actor.refNum = 0x6a000b88u;
    actor.cellId = "Seyda Neen, Lighthouse";
    actor.isDead = true;

    auto identity = mwmp::makeCorpseDisposedActorIdentity(actor, actor.cellId);
    ASSERT_TRUE(identity.has_value());
    EXPECT_EQ(identity->actorNetId, mwmp::actorInstanceIdFromActor(actor));
    EXPECT_TRUE(identity->removed);
    EXPECT_FALSE(identity->serverSpawned);
    EXPECT_FALSE(identity->persistent);
    EXPECT_EQ(identity->removalReason, mwmp::ActorRemovalReason::CorpseDisposed);
    EXPECT_EQ(identity->migrationGeneration, 1u);
    EXPECT_EQ(identity->actor.migrationGeneration, 1u);
    EXPECT_FALSE(identity->actor.isDead);
    EXPECT_EQ(identity->actor.cellId, actor.cellId);
}

TEST(ActorRegistryInvariant, CorpseDisposedIdentityRejectsWrongCellAndRuntimeActors)
{
    mwmp::BaseActor actor;
    actor.refId = "zombie 1 walker";
    actor.refNum = 0x6a000b88u;
    actor.cellId = "Seyda Neen, Lighthouse";

    EXPECT_FALSE(mwmp::makeCorpseDisposedActorIdentity(actor, "Seyda Neen, Census and Excise Office"));
    actor.mpNum = 123;
    EXPECT_FALSE(mwmp::makeCorpseDisposedActorIdentity(actor, actor.cellId));
}

TEST(ActorRegistryInvariant, DurableVanillaDeathUsesInstantBaselineSemantics)
{
    mwmp::BaseActor actor;
    actor.refId = "zombie 2 g imperial";
    actor.refNum = 0x6a000b95u;
    actor.cellId = "EXT:-2,-9";
    actor.isDead = true;
    actor.isInstantDeath = false;
    actor.isMoving = true;
    actor.isAttackingOrCasting = true;
    actor.velocity.linear[0] = 42.f;
    actor.dynamicStats.health.current = -12.f;
    actor.deathAnimGroup = "death1";

    auto baseline = mwmp::makeDurableVanillaDeathBaseline(actor, actor.cellId);
    ASSERT_TRUE(baseline.has_value());
    EXPECT_TRUE(baseline->isDead);
    EXPECT_TRUE(baseline->isInstantDeath);
    EXPECT_FALSE(baseline->isMoving);
    EXPECT_FALSE(baseline->isAttackingOrCasting);
    EXPECT_EQ(baseline->velocity.linear[0], 0.f);
    EXPECT_EQ(baseline->dynamicStats.health.current, 0.f);
    EXPECT_EQ(baseline->deathAnimGroup, "death1");
    EXPECT_EQ(baseline->position.pos[0], actor.position.pos[0]);
}

TEST(ActorRegistryInvariant, DurableVanillaDeathRejectsLiveSpawnedAndWrongCellActors)
{
    mwmp::BaseActor actor;
    actor.refId = "zombie 1 walker";
    actor.refNum = 0x6a000b88u;
    actor.cellId = "EXT:-2,-9";

    EXPECT_FALSE(mwmp::makeDurableVanillaDeathBaseline(actor, actor.cellId));
    actor.isDead = true;
    EXPECT_FALSE(mwmp::makeDurableVanillaDeathBaseline(actor, "EXT:-1,-9"));
    actor.mpNum = 77;
    EXPECT_FALSE(mwmp::makeDurableVanillaDeathBaseline(actor, actor.cellId));
}

TEST(ActorRegistryInvariant, DurableVanillaDeathTargetsOnlyOffCellV2Players)
{
    EXPECT_TRUE(mwmp::shouldSendDurableVanillaDeathToClient(
        false, true, mwmp::ActorSyncProtocolVersionV2, false));
    EXPECT_FALSE(mwmp::shouldSendDurableVanillaDeathToClient(
        true, true, mwmp::ActorSyncProtocolVersionV2, false));
    EXPECT_FALSE(mwmp::shouldSendDurableVanillaDeathToClient(
        false, false, mwmp::ActorSyncProtocolVersionV2, false));
    EXPECT_FALSE(mwmp::shouldSendDurableVanillaDeathToClient(
        false, true, mwmp::ActorSyncProtocolVersionV1, false));
    EXPECT_FALSE(mwmp::shouldSendDurableVanillaDeathToClient(
        false, true, mwmp::ActorSyncProtocolVersionV2, true));
}

TEST(ActorRegistryInvariant, OnlyVanillaCorpseDisposalIsGloballyDurable)
{
    mwmp::BaseActor actor;
    EXPECT_TRUE(mwmp::isGloballyDurableVanillaRemoval(
        mwmp::ActorRemovalReason::CorpseDisposed, actor));
    EXPECT_FALSE(mwmp::isGloballyDurableVanillaRemoval(
        mwmp::ActorRemovalReason::Generic, actor));
    actor.mpNum = 42;
    EXPECT_FALSE(mwmp::isGloballyDurableVanillaRemoval(
        mwmp::ActorRemovalReason::CorpseDisposed, actor));
}

TEST(ActorRegistryInvariant, DurableLifecyclePacketsRoundTripBeforeWorldEntry)
{
    mwmp::BaseActor dead;
    dead.refId = "zombie 1 walker";
    dead.refNum = 0x6a000b88u;
    dead.cellId = "EXT:-2,-9";
    dead.isDead = true;
    dead.deathAnimGroup = "death1";
    auto baseline = mwmp::makeDurableVanillaDeathBaseline(dead, dead.cellId);
    ASSERT_TRUE(baseline.has_value());

    mwmp::ActorList outgoingDeaths;
    outgoingDeaths.cellId = dead.cellId;
    outgoingDeaths.actors.push_back(*baseline);
    mwmp::PacketActorDeath deathWriter;
    deathWriter.setActorList(&outgoingDeaths);
    const std::vector<uint8_t> deathBytes = deathWriter.encode();

    mwmp::ActorList incomingDeaths;
    mwmp::PacketActorDeath deathReader;
    deathReader.setActorList(&incomingDeaths);
    ASSERT_TRUE(deathReader.decode(deathBytes));
    ASSERT_EQ(incomingDeaths.actors.size(), 1u);
    EXPECT_TRUE(incomingDeaths.actors.front().isDead);
    EXPECT_TRUE(incomingDeaths.actors.front().isInstantDeath);
    EXPECT_EQ(incomingDeaths.actors.front().deathAnimGroup, "death1");

    auto tombstone = mwmp::makeCorpseDisposedActorIdentity(dead, dead.cellId);
    ASSERT_TRUE(tombstone.has_value());
    mwmp::ActorIdentityList outgoingIdentities;
    outgoingIdentities.cellId = dead.cellId;
    outgoingIdentities.actors.push_back(*tombstone);
    mwmp::PacketActorIdentity identityWriter;
    identityWriter.setIdentityList(&outgoingIdentities);
    const std::vector<uint8_t> identityBytes = identityWriter.encode();

    mwmp::ActorIdentityList incomingIdentities;
    mwmp::PacketActorIdentity identityReader;
    identityReader.setIdentityList(&incomingIdentities);
    ASSERT_TRUE(identityReader.decode(identityBytes));
    ASSERT_EQ(incomingIdentities.actors.size(), 1u);
    EXPECT_TRUE(incomingIdentities.actors.front().removed);
    EXPECT_EQ(incomingIdentities.actors.front().removalReason,
        mwmp::ActorRemovalReason::CorpseDisposed);
}

TEST(ActorIdentityProtocol, EmptyCompleteSnapshotRoundTripsAllMetadata)
{
    mwmp::ActorIdentityList outgoing;
    outgoing.protocolVersion = mwmp::ActorSyncProtocolVersionV2;
    outgoing.cellId = "EXT:-2,-9";
    outgoing.authorityGuid = 2;
    outgoing.authorityGeneration = 17;
    outgoing.sequence = 91;
    outgoing.serverTimestamp = 123456789;
    outgoing.completeCellSnapshot = true;
    ASSERT_TRUE(outgoing.actors.empty());

    mwmp::PacketActorIdentity writer;
    writer.setIdentityList(&outgoing);
    const std::vector<uint8_t> encoded = writer.encode();

    mwmp::ActorIdentityList incoming;
    mwmp::PacketActorIdentity reader;
    reader.setIdentityList(&incoming);
    ASSERT_TRUE(reader.decode(encoded));
    EXPECT_EQ(incoming.protocolVersion, outgoing.protocolVersion);
    EXPECT_EQ(incoming.cellId, outgoing.cellId);
    EXPECT_EQ(incoming.authorityGuid, outgoing.authorityGuid);
    EXPECT_EQ(incoming.authorityGeneration, outgoing.authorityGeneration);
    EXPECT_EQ(incoming.sequence, outgoing.sequence);
    EXPECT_EQ(incoming.serverTimestamp, outgoing.serverTimestamp);
    EXPECT_TRUE(incoming.completeCellSnapshot);
    EXPECT_TRUE(incoming.actors.empty());
}

TEST(MechanicsSnapshotProtocol, RepairedActorIdentityProducesValidMechanicsGeneration)
{
    mwmp::BaseActor actor;
    actor.refId = "test_persisted_actor";
    actor.mpNum = 5257;
    actor.cellId = "T_Test_TR";
    std::uint32_t registryGeneration = 0;
    mwmp::ensureCanonicalActorMigrationGeneration(registryGeneration, actor);

    mwmp::ActorIdentityRecord identity;
    identity.actorNetId = mwmp::actorInstanceIdFromActor(actor);
    identity.serverSpawned = true;
    identity.persistent = true;
    identity.migrationGeneration = registryGeneration;
    identity.actor = actor;

    auto snapshot = makeNpcSnapshot();
    snapshot.actorInstanceId = identity.actorNetId;
    snapshot.cellId = identity.actor.cellId;
    snapshot.migrationGeneration = identity.migrationGeneration;
    EXPECT_TRUE(mwmp::validateMechanicsSnapshot(snapshot));
}

TEST(MechanicsSnapshotProtocol, RejectsNonFiniteAndAbsurdNumericValues)
{
    auto snapshot = makeNpcSnapshot();
    snapshot.position.pos[0] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(mwmp::validateMechanicsSnapshot(snapshot));
    snapshot = makeNpcSnapshot();
    snapshot.blind = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(mwmp::validateMechanicsSnapshot(snapshot));
    snapshot = makeNpcSnapshot();
    snapshot.agility = mwmp::MaximumMechanicsValueMagnitude + 1.f;
    EXPECT_FALSE(mwmp::validateMechanicsSnapshot(snapshot));
    snapshot = makeNpcSnapshot();
    snapshot.fatigueMaximumModified = -1.f;
    EXPECT_FALSE(mwmp::validateMechanicsSnapshot(snapshot));
}

TEST(MechanicsSnapshotProtocol, DecoderRejectsInvalidSnapshotAtomically)
{
    mwmp::MechanicsSnapshotBatch outgoing;
    outgoing.snapshots = { makeNpcSnapshot() };
    outgoing.snapshots.front().snapshotSequence = 0;
    const auto bytes = encode(outgoing);

    mwmp::MechanicsSnapshotBatch incoming;
    incoming.snapshots = { makePlayerSnapshot() };
    const auto before = incoming;
    mwmp::PacketMechanicsSnapshot decoder;
    decoder.setBatch(&incoming);
    EXPECT_FALSE(decoder.decode(bytes));
    EXPECT_EQ(incoming, before);
}
