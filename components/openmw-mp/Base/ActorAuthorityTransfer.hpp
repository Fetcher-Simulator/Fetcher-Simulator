#ifndef OPENMW_MP_ACTORAUTHORITYTRANSFER_HPP
#define OPENMW_MP_ACTORAUTHORITYTRANSFER_HPP

#include <cstdint>
#include <string>

#include "ActorSyncProtocol.hpp"

namespace mwmp
{
    enum class ActorAuthorityTransferPhase : std::uint8_t
    {
        Prepare = 1,
        Ready = 2,
        Commit = 3,
        Cancel = 4,
        Applied = 5,
    };

    struct ActorAuthorityTransfer
    {
        ActorAuthorityTransferPhase phase = ActorAuthorityTransferPhase::Prepare;
        ActorInstanceId actorNetId = 0;
        std::uint64_t transferId = 0;
        std::uint32_t oldOwnerGuid = 0;
        std::uint32_t newOwnerGuid = 0;
        std::uint32_t authorityGeneration = 0;
        std::uint32_t migrationGeneration = 0;
        std::string cellId;
        BaseActor actor;
    };

    inline bool matchesActorAuthorityTransferReady(
        const ActorAuthorityTransfer& prepare, const ActorAuthorityTransfer& ready,
        std::uint32_t senderGuid)
    {
        return prepare.phase == ActorAuthorityTransferPhase::Prepare
            && ready.phase == ActorAuthorityTransferPhase::Ready
            && prepare.actorNetId != 0 && prepare.transferId != 0
            && senderGuid == prepare.newOwnerGuid
            && ready.actorNetId == prepare.actorNetId
            && ready.transferId == prepare.transferId
            && ready.oldOwnerGuid == prepare.oldOwnerGuid
            && ready.newOwnerGuid == prepare.newOwnerGuid
            && ready.authorityGeneration == prepare.authorityGeneration
            && ready.migrationGeneration == prepare.migrationGeneration
            && ready.cellId == prepare.cellId;
    }

    inline bool matchesActorAuthorityTransferCommit(
        const ActorAuthorityTransfer& prepare, const ActorAuthorityTransfer& commit)
    {
        return prepare.phase == ActorAuthorityTransferPhase::Prepare
            && commit.phase == ActorAuthorityTransferPhase::Commit
            && prepare.actorNetId != 0 && prepare.transferId != 0
            && commit.actorNetId == prepare.actorNetId
            && commit.transferId == prepare.transferId
            && commit.oldOwnerGuid == prepare.oldOwnerGuid
            && commit.newOwnerGuid == prepare.newOwnerGuid
            && commit.authorityGeneration == prepare.authorityGeneration
            && commit.migrationGeneration == prepare.migrationGeneration
            && commit.cellId == prepare.cellId;
    }

    inline bool matchesActorAuthorityTransferCancel(
        const ActorAuthorityTransfer& prepare, const ActorAuthorityTransfer& cancel)
    {
        return prepare.phase == ActorAuthorityTransferPhase::Prepare
            && cancel.phase == ActorAuthorityTransferPhase::Cancel
            && prepare.actorNetId != 0 && prepare.transferId != 0
            && cancel.actorNetId == prepare.actorNetId
            && cancel.transferId == prepare.transferId
            && cancel.oldOwnerGuid == prepare.oldOwnerGuid
            && cancel.newOwnerGuid == prepare.newOwnerGuid
            && cancel.authorityGeneration == prepare.authorityGeneration
            && cancel.migrationGeneration == prepare.migrationGeneration
            && cancel.cellId == prepare.cellId;
    }

    inline bool matchesActorAuthorityTransferApplied(
        const ActorAuthorityTransfer& commit, const ActorAuthorityTransfer& applied,
        std::uint32_t senderGuid)
    {
        return commit.phase == ActorAuthorityTransferPhase::Commit
            && applied.phase == ActorAuthorityTransferPhase::Applied
            && commit.actorNetId != 0 && commit.transferId != 0
            && senderGuid == commit.newOwnerGuid
            && applied.actorNetId == commit.actorNetId
            && applied.transferId == commit.transferId
            && applied.oldOwnerGuid == commit.oldOwnerGuid
            && applied.newOwnerGuid == commit.newOwnerGuid
            && applied.authorityGeneration == commit.authorityGeneration
            && applied.migrationGeneration == commit.migrationGeneration
            && applied.cellId == commit.cellId;
    }

    inline bool shouldAcceptActorAuthorityLease(
        bool hasKnownGeneration, std::uint32_t knownGeneration, std::uint32_t knownOwner,
        std::uint32_t incomingGeneration, std::uint32_t incomingOwner)
    {
        if (!hasKnownGeneration)
            return true;
        if (generationIsNewer(knownGeneration, incomingGeneration))
            return false;
        return knownGeneration != incomingGeneration || knownOwner == incomingOwner;
    }

    // A delayed Prepare must not replace a newer transaction or re-gate an
    // actor whose lease has already committed to this client.
    inline bool shouldAcceptActorAuthorityTransferPrepare(
        const ActorAuthorityTransfer& incoming, const ActorAuthorityTransfer* pending,
        bool hasKnownLease, std::uint32_t knownGeneration, std::uint32_t knownOwner)
    {
        if (incoming.phase != ActorAuthorityTransferPhase::Prepare
            || incoming.actorNetId == 0 || incoming.transferId == 0)
            return false;
        if (hasKnownLease
            && (generationIsNewer(knownGeneration, incoming.authorityGeneration)
                || (knownGeneration == incoming.authorityGeneration
                    && knownOwner == incoming.newOwnerGuid)))
            return false;
        if (!pending)
            return true;
        if (pending->actorNetId != incoming.actorNetId)
            return false;
        if (generationIsNewer(pending->authorityGeneration, incoming.authorityGeneration))
            return false;
        return pending->authorityGeneration != incoming.authorityGeneration
            || incoming.transferId >= pending->transferId;
    }
}

#endif
