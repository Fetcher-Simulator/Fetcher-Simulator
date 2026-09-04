#include <gtest/gtest.h>

#include <apps/openmw-server/DoorStateAuthority.hpp>

namespace
{
    mwmp::DoorEntry proposal()
    {
        mwmp::DoorEntry result;
        result.cellId = "EXT:0,0";
        result.refId = "door_01";
        result.refNum = 42;
        result.isOpen = true;
        result.revision = 1;
        return result;
    }

    mwmp::DoorStateProposalContext context()
    {
        mwmp::DoorStateProposalContext result;
        result.packetCellId = "EXT:0,0";
        result.relevantCellIds = { "EXT:0,0" };
        result.playerPosition = { 10.f, 20.f, 30.f };
        result.maximumDistance = 512.f;
        result.reference = mwmp::DoorStateReference { "EXT:0,0", "Door_01", 42, { 20.f, 20.f, 30.f } };
        return result;
    }
}

TEST(DoorStateAuthorityTest, AcceptsExactNextRevisionForNearbyStaticDoor)
{
    EXPECT_EQ(mwmp::validateDoorStateProposal(proposal(), context()), mwmp::DoorStateProposalError::None);
}

TEST(DoorStateAuthorityTest, RejectsReplayAndLeavesCurrentContractUnchanged)
{
    auto current = proposal();
    current.isOpen = false;
    current.revision = 4;
    auto proposalValue = proposal();
    proposalValue.revision = 4;
    auto contextValue = context();
    contextValue.current = current;
    EXPECT_EQ(mwmp::validateDoorStateProposal(proposalValue, contextValue),
        mwmp::DoorStateProposalError::StaleRevision);
    EXPECT_EQ(contextValue.current, current);
}

TEST(DoorStateAuthorityTest, RejectsDuplicateStateAndLockMutation)
{
    auto duplicate = proposal();
    duplicate.isOpen = false;
    EXPECT_EQ(mwmp::validateDoorStateProposal(duplicate, context()),
        mwmp::DoorStateProposalError::InvalidTransition);

    auto lockMutation = proposal();
    lockMutation.isLocked = true;
    lockMutation.lockLevel = 50;
    EXPECT_EQ(mwmp::validateDoorStateProposal(lockMutation, context()),
        mwmp::DoorStateProposalError::LockStateMutation);
}

TEST(DoorStateAuthorityTest, RejectsUnknownIrrelevantAndRemoteDoors)
{
    auto contextValue = context();
    contextValue.reference.reset();
    EXPECT_EQ(mwmp::validateDoorStateProposal(proposal(), contextValue),
        mwmp::DoorStateProposalError::UnknownDoor);

    contextValue = context();
    contextValue.relevantCellIds = { "EXT:1,0" };
    EXPECT_EQ(mwmp::validateDoorStateProposal(proposal(), contextValue),
        mwmp::DoorStateProposalError::IrrelevantCell);

    contextValue = context();
    contextValue.reference->position = { 1000.f, 20.f, 30.f };
    EXPECT_EQ(mwmp::validateDoorStateProposal(proposal(), contextValue),
        mwmp::DoorStateProposalError::TooFar);
}

TEST(DoorStateAuthorityTest, RejectsDynamicDoorUntilAuthoritativeGeometryExists)
{
    auto proposalValue = proposal();
    proposalValue.refNum = 0;
    proposalValue.mpNum = 7;
    EXPECT_EQ(mwmp::validateDoorStateProposal(proposalValue, context()),
        mwmp::DoorStateProposalError::UnsupportedDynamicDoor);
}

TEST(DoorStateAuthorityTest, BaseLockedDoorRemainsLockedWithoutPersistedOverride)
{
    auto contextValue = context();
    contextValue.reference->baseLocked = true;
    contextValue.reference->baseLockLevel = 75;

    auto proposed = proposal();
    proposed.isLocked = true;
    proposed.lockLevel = 75;
    EXPECT_EQ(mwmp::validateDoorStateProposal(proposed, contextValue),
        mwmp::DoorStateProposalError::None);

    proposed.isLocked = false;
    proposed.lockLevel = 0;
    EXPECT_EQ(mwmp::validateDoorStateProposal(proposed, contextValue),
        mwmp::DoorStateProposalError::LockStateMutation);
}
