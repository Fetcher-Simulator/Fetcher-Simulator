#include <gtest/gtest.h>

#include <components/openmw-mp/Base/ActorSyncProtocol.hpp>
#include <components/openmw-mp/CombatEvent.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorCombatRequest.hpp>
#include <components/openmw-mp/Packets/Actor/PacketActorCombatResult.hpp>

namespace
{
    mwmp::ActorInstanceId victimId()
    {
        return mwmp::packActorInstanceKey({ mwmp::ActorKeyKind::VanillaRefNum, 42 });
    }

    mwmp::ActorList boundCombatList()
    {
        mwmp::ActorList list;
        list.cellId = "Balmora";
        list.authorityGuid = 7;
        list.combatEventId = 91;
        list.combatVictimActorInstanceId = victimId();
        list.combatVictimMigrationGeneration = 3;
        list.combatVictimAuthorityGeneration = 5;
        mwmp::BaseActor victim;
        victim.refId = "hlaalu guard";
        victim.refNum = 42;
        victim.cellId = list.cellId;
        victim.attack.targetKind = mwmp::Attack::TargetActor;
        victim.attack.damage = 12.f;
        list.actors.push_back(victim);
        return list;
    }
}

TEST(CombatEventProtocol, CriminalQualificationPreservesNativeExclusions)
{
    EXPECT_TRUE(mwmp::isQualifyingCriminalAttack(mwmp::CombatResultApplied));
    EXPECT_FALSE(mwmp::isQualifyingCriminalAttack(0));
    EXPECT_FALSE(mwmp::isQualifyingCriminalAttack(
        mwmp::CombatResultApplied | mwmp::CombatVictimWasAggressive));
    EXPECT_FALSE(mwmp::isQualifyingCriminalAttack(
        mwmp::CombatResultApplied | mwmp::CombatVictimWasEngaged));
    EXPECT_FALSE(mwmp::isQualifyingCriminalAttack(
        mwmp::CombatResultApplied | mwmp::CombatVictimWasPursuing));
}

TEST(CombatEventProtocol, ResultValidationRejectsIncompleteAndConflictingFields)
{
    EXPECT_TRUE(mwmp::validateCombatResultFields(1, victimId(), 1, 1, 1,
        mwmp::CombatResultApplied, 4.f));
    EXPECT_FALSE(mwmp::validateCombatResultFields(0, victimId(), 1, 1, 1,
        mwmp::CombatResultApplied, 4.f));
    EXPECT_FALSE(mwmp::validateCombatResultFields(1, victimId(), 1, 1, 1, 0, 4.f));
    EXPECT_FALSE(mwmp::validateCombatResultFields(1, victimId(), 1, 1, 1,
        mwmp::CombatVictimDied, 0.f));
}

TEST(CombatEventProtocol, ProposalAndDelegatedResultRoundTrip)
{
    mwmp::ActorList proposal = boundCombatList();
    mwmp::PacketActorCombatRequest outgoingProposal;
    outgoingProposal.setActorList(&proposal);
    const auto proposalBytes = outgoingProposal.encode();
    mwmp::ActorList decodedProposal;
    mwmp::PacketActorCombatRequest incomingProposal;
    incomingProposal.setActorList(&decodedProposal);
    ASSERT_TRUE(incomingProposal.decode(proposalBytes.data(), proposalBytes.size()));
    EXPECT_EQ(decodedProposal.combatEventId, proposal.combatEventId);
    EXPECT_EQ(decodedProposal.combatVictimActorInstanceId, proposal.combatVictimActorInstanceId);

    mwmp::ActorList result = boundCombatList();
    result.combatResultSequence = 11;
    result.combatResultFlags = mwmp::CombatResultApplied;
    result.combatAppliedDamage = 12.f;
    mwmp::PacketActorCombatResult outgoingResult;
    outgoingResult.setActorList(&result);
    const auto resultBytes = outgoingResult.encode();
    mwmp::ActorList decodedResult;
    mwmp::PacketActorCombatResult incomingResult;
    incomingResult.setActorList(&decodedResult);
    ASSERT_TRUE(incomingResult.decode(resultBytes.data(), resultBytes.size()));
    EXPECT_EQ(decodedResult.combatEventId, result.combatEventId);
    EXPECT_EQ(decodedResult.combatResultSequence, result.combatResultSequence);
    EXPECT_EQ(decodedResult.combatResultFlags, result.combatResultFlags);
    EXPECT_EQ(decodedResult.combatAppliedDamage, result.combatAppliedDamage);
}
