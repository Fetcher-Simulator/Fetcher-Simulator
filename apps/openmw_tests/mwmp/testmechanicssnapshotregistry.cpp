#include <gtest/gtest.h>

#include <apps/openmw-server/MechanicsSnapshotRegistry.hpp>

namespace
{
    mwmp::MechanicsSnapshot playerSnapshot(std::uint32_t guid, std::uint32_t sequence)
    {
        mwmp::MechanicsSnapshot snapshot;
        snapshot.kind = mwmp::MechanicsSubjectKind::Player;
        snapshot.playerGuid = guid;
        snapshot.cellId = "Balmora, Guild of Mages";
        snapshot.stateFlags = mwmp::MechanicsEnabled | mwmp::MechanicsAlive | mwmp::MechanicsConscious;
        snapshot.fatigueMaximumModified = 100.f;
        snapshot.migrationGeneration = 1;
        snapshot.authorityGeneration = guid;
        snapshot.snapshotSequence = sequence;
        return snapshot;
    }

    mwmp::MechanicsSnapshot actorSnapshot(std::uint32_t sequence)
    {
        mwmp::MechanicsSnapshot snapshot = playerSnapshot(7, sequence);
        snapshot.kind = mwmp::MechanicsSubjectKind::Npc;
        snapshot.playerGuid = 0;
        snapshot.actorInstanceId
            = mwmp::packActorInstanceKey({ mwmp::ActorKeyKind::VanillaRefNum, 123 });
        snapshot.cellId = "EXT:-2,-9";
        snapshot.migrationGeneration = 4;
        snapshot.authorityGeneration = 8;
        return snapshot;
    }

    mwmp::MechanicsSnapshotExpectation playerExpectation(std::uint32_t guid)
    {
        mwmp::MechanicsSnapshotExpectation expected;
        expected.subject = { mwmp::MechanicsSubjectKind::Player, guid, 0 };
        expected.cellId = "Balmora, Guild of Mages";
        expected.migrationGeneration = 1;
        expected.authorityGeneration = guid;
        expected.authenticatedPlayerGuid = guid;
        return expected;
    }

    mwmp::MechanicsSnapshotExpectation actorExpectation()
    {
        mwmp::MechanicsSnapshotExpectation expected;
        expected.subject = { mwmp::MechanicsSubjectKind::Npc, 0,
            mwmp::packActorInstanceKey({ mwmp::ActorKeyKind::VanillaRefNum, 123 }) };
        expected.cellId = "EXT:-2,-9";
        expected.migrationGeneration = 4;
        expected.authorityGeneration = 8;
        expected.actorSenderEntitled = true;
        return expected;
    }
}

TEST(MechanicsSnapshotRegistry, AuthenticatedPlayerMayOnlySubmitSelf)
{
    mwmp::MechanicsSnapshotRegistry registry;
    EXPECT_EQ(registry.accept(playerSnapshot(7, 1), playerExpectation(7), 100),
        mwmp::MechanicsSnapshotError::None);

    auto anotherPlayer = playerSnapshot(8, 1);
    EXPECT_EQ(registry.accept(anotherPlayer, playerExpectation(7), 101),
        mwmp::MechanicsSnapshotError::WrongSubject);
    EXPECT_EQ(registry.size(), 1u);
}

TEST(MechanicsSnapshotRegistry, ActorRequiresCurrentEntitlementKindAndGenerations)
{
    mwmp::MechanicsSnapshotRegistry registry;
    const auto snapshot = actorSnapshot(1);
    auto expected = actorExpectation();
    EXPECT_EQ(registry.accept(snapshot, expected, 100), mwmp::MechanicsSnapshotError::None);

    expected.actorSenderEntitled = false;
    EXPECT_EQ(registry.accept(actorSnapshot(2), expected, 101),
        mwmp::MechanicsSnapshotError::UnauthorizedSender);
    expected = actorExpectation();
    expected.subject.kind = mwmp::MechanicsSubjectKind::Creature;
    EXPECT_EQ(registry.accept(actorSnapshot(2), expected, 101),
        mwmp::MechanicsSnapshotError::WrongActorKind);
    expected = actorExpectation();
    expected.migrationGeneration++;
    EXPECT_EQ(registry.accept(actorSnapshot(2), expected, 101),
        mwmp::MechanicsSnapshotError::WrongMigrationGeneration);
    expected = actorExpectation();
    expected.authorityGeneration++;
    EXPECT_EQ(registry.accept(actorSnapshot(2), expected, 101),
        mwmp::MechanicsSnapshotError::WrongAuthorityGeneration);
}

TEST(MechanicsSnapshotRegistry, SequenceIsStrictlyMonotonicWithinAuthorityEpoch)
{
    mwmp::MechanicsSnapshotRegistry registry;
    const auto expected = actorExpectation();
    EXPECT_EQ(registry.accept(actorSnapshot(1), expected, 100), mwmp::MechanicsSnapshotError::None);
    EXPECT_EQ(registry.accept(actorSnapshot(1), expected, 101),
        mwmp::MechanicsSnapshotError::ReplayOrOutOfOrder);
    EXPECT_EQ(registry.accept(actorSnapshot(0), expected, 102),
        mwmp::MechanicsSnapshotError::InvalidSnapshot);
    EXPECT_EQ(registry.accept(actorSnapshot(2), expected, 103), mwmp::MechanicsSnapshotError::None);
    ASSERT_NE(registry.find(expected.subject), nullptr);
    EXPECT_EQ(registry.find(expected.subject)->snapshot.snapshotSequence, 2u);
}

TEST(MechanicsSnapshotRegistry, RejectedSnapshotDoesNotChangeAcceptedState)
{
    mwmp::MechanicsSnapshotRegistry registry;
    const auto expected = actorExpectation();
    auto accepted = actorSnapshot(3);
    accepted.agility = 45.f;
    ASSERT_EQ(registry.accept(accepted, expected, 100), mwmp::MechanicsSnapshotError::None);
    const auto before = *registry.find(expected.subject);

    auto rejected = actorSnapshot(4);
    rejected.agility = 99.f;
    rejected.cellId = "EXT:-2,-8";
    EXPECT_EQ(registry.accept(rejected, expected, 101), mwmp::MechanicsSnapshotError::WrongCell);
    EXPECT_EQ(*registry.find(expected.subject), before);
}

TEST(MechanicsSnapshotRegistry, NewAuthorityEpochMayRestartSequence)
{
    mwmp::MechanicsSnapshotRegistry registry;
    auto expected = actorExpectation();
    ASSERT_EQ(registry.accept(actorSnapshot(9), expected, 100), mwmp::MechanicsSnapshotError::None);

    auto replacement = actorSnapshot(1);
    replacement.authorityGeneration = 9;
    expected.authorityGeneration = 9;
    ASSERT_EQ(registry.accept(replacement, expected, 101), mwmp::MechanicsSnapshotError::None);
    EXPECT_EQ(registry.find(expected.subject)->snapshot.snapshotSequence, 1u);
}

TEST(MechanicsSnapshotRegistry, FreshnessUsesServerReceiptTime)
{
    mwmp::MechanicsSnapshotRegistry registry;
    const auto expected = playerExpectation(7);
    ASSERT_EQ(registry.accept(playerSnapshot(7, 1), expected, 1000), mwmp::MechanicsSnapshotError::None);
    EXPECT_NE(registry.findFresh(expected.subject, 1500, 500), nullptr);
    EXPECT_EQ(registry.findFresh(expected.subject, 1501, 500), nullptr);
    EXPECT_EQ(registry.findFresh(expected.subject, 999, 500), nullptr);
}
