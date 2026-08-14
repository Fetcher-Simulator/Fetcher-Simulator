#include <gtest/gtest.h>

#include <cmath>

#include <apps/openmw-server/LiveObservationRuntime.hpp>

TEST(LiveObservationRuntimeTest, ConvertsAcceptedSnapshotWithExplicitDelegatedProvenance)
{
    mwmp::AcceptedMechanicsSnapshot accepted;
    accepted.source = mwmp::MechanicsSnapshotSource::ActorAuthorityDelegated;
    accepted.receivedAtMs = 1234;
    accepted.snapshot.kind = mwmp::MechanicsSubjectKind::Npc;
    accepted.snapshot.actorInstanceId
        = mwmp::packActorInstanceKey({ mwmp::ActorKeyKind::VanillaRefNum, 42 });
    accepted.snapshot.position.pos[0] = 10.f;
    accepted.snapshot.position.pos[1] = 20.f;
    accepted.snapshot.position.pos[2] = 30.f;
    accepted.snapshot.position.rot[2] = 3.14159265f / 2.f;
    accepted.snapshot.stateFlags = mwmp::MechanicsEnabled | mwmp::MechanicsAlive
        | mwmp::MechanicsConscious | mwmp::MechanicsSneaking | mwmp::MechanicsOnGround;
    accepted.snapshot.sneakSkill = 40.f;
    accepted.snapshot.agility = 50.f;
    accepted.snapshot.luck = 60.f;
    accepted.snapshot.fatigueCurrent = 70.f;
    accepted.snapshot.fatigueMaximumModified = 80.f;
    accepted.snapshot.chameleon = 10.f;
    accepted.snapshot.invisibility = 20.f;
    accepted.snapshot.blind = 30.f;
    accepted.snapshot.migrationGeneration = 2;
    accepted.snapshot.authorityGeneration = 3;
    accepted.snapshot.snapshotSequence = 4;

    const mwmp::ObservationActorSnapshot result = mwmp::makeLiveObservationSnapshot(accepted, 12.5f);
    EXPECT_EQ(result.identity.kind, mwmp::ObservationActorKind::Npc);
    EXPECT_EQ(result.identity.actorInstanceId, accepted.snapshot.actorInstanceId);
    EXPECT_TRUE(result.enabled);
    EXPECT_TRUE(result.alive);
    EXPECT_TRUE(result.conscious);
    EXPECT_TRUE(result.sneaking);
    EXPECT_TRUE(result.onGround);
    EXPECT_NEAR(result.forward.x, -1.f, 0.0001f);
    EXPECT_NEAR(result.forward.y, 0.f, 0.0001f);
    EXPECT_FLOAT_EQ(result.bootWeight, 12.5f);
    EXPECT_EQ(result.snapshotGeneration, 4u);
    EXPECT_EQ(result.sampledAtMs, 1234u);
    EXPECT_EQ(result.authority, mwmp::ObservationAuthority::ActorAuthorityDelegated);
}

TEST(LiveObservationRuntimeTest, ServerRollSourceIsBounded)
{
    mwmp::ServerAwarenessRollSource rolls(7);
    for (int i = 0; i < 1000; ++i)
    {
        const int roll = rolls.nextRoll0To99();
        EXPECT_GE(roll, 0);
        EXPECT_LE(roll, 99);
    }
}
