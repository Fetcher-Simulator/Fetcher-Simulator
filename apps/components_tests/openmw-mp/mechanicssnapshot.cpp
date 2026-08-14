#include <limits>

#include <gtest/gtest.h>

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
            | mwmp::MechanicsConscious | mwmp::MechanicsOnGround;
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
        return snapshot;
    }

    std::vector<std::uint8_t> encode(mwmp::MechanicsSnapshotBatch& batch)
    {
        mwmp::PacketMechanicsSnapshot packet;
        packet.setBatch(&batch);
        return packet.encode();
    }
}

TEST(MechanicsSnapshotProtocol, MultiplayerProtocolIsNine)
{
    EXPECT_EQ(mwmp::MultiplayerProtocolVersion, 9);
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
