#include <limits>

#include <gtest/gtest.h>

#include <apps/openmw-server/ActorRegistryInvariant.hpp>
#include <components/openmw-mp/Base/MechanicsSnapshot.hpp>
#include <components/openmw-mp/MasterServerProtocol.hpp>
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
            | mwmp::MechanicsWitnessHasCombatTarget | mwmp::MechanicsWitnessEffectiveAlarmKnown;
        snapshot.effectiveAlarm = 100;
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

TEST(MechanicsSnapshotProtocol, MultiplayerProtocolIsFifteen)
{
    EXPECT_EQ(mwmp::MultiplayerProtocolVersion, 15);
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
