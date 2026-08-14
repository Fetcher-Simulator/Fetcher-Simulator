#include "DoorStateAuthority.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <components/esm/refid.hpp>

mwmp::DoorStateProposalError mwmp::validateDoorStateProposal(
    const DoorEntry& proposed, const DoorStateProposalContext& context)
{
    if (!validateDoorEntry(proposed))
        return DoorStateProposalError::InvalidEntry;
    if (proposed.cellId != context.packetCellId)
        return DoorStateProposalError::WrongCell;
    if (!std::binary_search(context.relevantCellIds.begin(), context.relevantCellIds.end(), proposed.cellId))
        return DoorStateProposalError::IrrelevantCell;
    if (proposed.mpNum != 0 || proposed.refNum == 0)
        return DoorStateProposalError::UnsupportedDynamicDoor;
    if (!context.reference)
        return DoorStateProposalError::UnknownDoor;
    if (context.reference->cellId != proposed.cellId
        || ESM::RefId::stringRefId(context.reference->refId) != ESM::RefId::stringRefId(proposed.refId)
        || context.reference->refNum != proposed.refNum)
        return DoorStateProposalError::WrongDoorIdentity;
    if (proposed.revision == 0)
        return DoorStateProposalError::InvalidRevision;

    const std::uint64_t currentRevision = context.current ? context.current->revision : 0;
    if (currentRevision == static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
        || proposed.revision != currentRevision + 1)
        return DoorStateProposalError::StaleRevision;

    const bool currentOpen = context.current ? context.current->isOpen : false;
    const bool currentLocked = context.current ? context.current->isLocked : false;
    const int currentLockLevel = context.current ? context.current->lockLevel : 0;
    if (proposed.isOpen == currentOpen)
        return DoorStateProposalError::InvalidTransition;
    if (proposed.isLocked != currentLocked || proposed.lockLevel != currentLockLevel)
        return DoorStateProposalError::LockStateMutation;

    if (!std::isfinite(context.maximumDistance) || context.maximumDistance <= 0.f)
        return DoorStateProposalError::InvalidDistance;
    float distanceSquared = 0.f;
    for (std::size_t axis = 0; axis < context.playerPosition.size(); ++axis)
    {
        if (!std::isfinite(context.playerPosition[axis]) || !std::isfinite(context.reference->position[axis]))
            return DoorStateProposalError::InvalidDistance;
        const float delta = context.playerPosition[axis] - context.reference->position[axis];
        distanceSquared += delta * delta;
    }
    if (!std::isfinite(distanceSquared)
        || distanceSquared > context.maximumDistance * context.maximumDistance)
        return DoorStateProposalError::TooFar;
    return DoorStateProposalError::None;
}

const char* mwmp::doorStateProposalErrorName(DoorStateProposalError error)
{
    switch (error)
    {
        case DoorStateProposalError::None: return "none";
        case DoorStateProposalError::InvalidEntry: return "invalid_entry";
        case DoorStateProposalError::WrongCell: return "wrong_cell";
        case DoorStateProposalError::IrrelevantCell: return "irrelevant_cell";
        case DoorStateProposalError::UnsupportedDynamicDoor: return "unsupported_dynamic_door";
        case DoorStateProposalError::UnknownDoor: return "unknown_door";
        case DoorStateProposalError::WrongDoorIdentity: return "wrong_door_identity";
        case DoorStateProposalError::InvalidRevision: return "invalid_revision";
        case DoorStateProposalError::StaleRevision: return "stale_revision";
        case DoorStateProposalError::InvalidTransition: return "invalid_transition";
        case DoorStateProposalError::LockStateMutation: return "lock_state_mutation";
        case DoorStateProposalError::InvalidDistance: return "invalid_distance";
        case DoorStateProposalError::TooFar: return "too_far";
    }
    return "unknown";
}
