#ifndef OPENMW_MP_ACTORCELLHANDOFF_HPP
#define OPENMW_MP_ACTORCELLHANDOFF_HPP

#include <cstdint>
#include <string_view>

#include "ActorSyncProtocol.hpp"

namespace mwmp
{
    inline bool shouldRejectActorIdentityRefresh(uint32_t canonicalGeneration, uint32_t incomingGeneration,
        std::string_view canonicalCell, std::string_view incomingCell, bool explicitTransformReset)
    {
        if (canonicalGeneration == 0)
            return false;
        if (incomingGeneration == 0 || generationIsNewer(canonicalGeneration, incomingGeneration))
            return true;
        // Within one migration generation, an ordinary refresh cannot move the
        // actor back to a cell from before the reliable migration commit.
        return incomingGeneration == canonicalGeneration && !explicitTransformReset
            && !canonicalCell.empty() && canonicalCell != incomingCell;
    }

    inline bool shouldForceActorIdentityBinding(bool hadRuntime, bool explicitTransformReset, bool deathBaseline)
    {
        return !hadRuntime || explicitTransformReset || deathBaseline;
    }

    inline bool shouldCollectActorInCanonicalCell(
        std::string_view canonicalCell, std::string_view collectionCell)
    {
        return canonicalCell.empty() || canonicalCell == collectionCell;
    }

    inline bool shouldIgnoreDuplicateActorCellChange(uint32_t runtimeGeneration, uint32_t commitGeneration,
        std::string_view runtimeCell, std::string_view destinationCell)
    {
        // Generation and canonical cell form one migration state. A prior
        // identity/baseline can advance the generation before the reliable
        // ActorCellChange arrives; equal generation is only a duplicate once
        // the runtime is already in the committed destination cell.
        return commitGeneration != 0 && runtimeGeneration == commitGeneration
            && runtimeCell == destinationCell;
    }

    inline bool shouldRetainCanonicalRuntimeInCompleteActorList(
        std::string_view runtimeCell, std::string_view outboundCell, bool missingIdentity)
    {
        // ActorList is a complete server-registry replacement. It must retain
        // canonical runtimes even if CellStore enumeration missed a locally
        // owned actor for one frame after authority/cell handoff.
        return !missingIdentity && runtimeCell == outboundCell;
    }

    inline float authorityHandoffWireBridgeDuration(float handoffDistance, bool moving)
    {
        if (!moving || handoffDistance <= 1.f || handoffDistance > 128.f)
            return 0.f;
        constexpr float maxCorrectionRate = 100.f;
        const float duration = handoffDistance / maxCorrectionRate;
        return duration < 0.3f ? 0.3f : (duration > 1.5f ? 1.5f : duration);
    }

    inline float authorityHandoffWireBridgeScale(float remaining, float duration)
    {
        if (remaining <= 0.f || duration <= 0.f)
            return 0.f;
        if (remaining >= duration)
            return 1.f;
        return remaining / duration;
    }

    inline bool shouldDeferObserverCellStoreRebind(bool preserveBufferedTimeline,
        bool /*destinationAuthority*/, bool boundToSourceCell, bool visualInDestinationCell)
    {
        // Seamless handoff applies to a newly promoted destination authority too.
        // It can continue simulating from the smoothed source-side position while
        // the reliable commit owns canonical cell identity, then rebind its Ptr
        // once the live coordinates actually cross the seam.
        return preserveBufferedTimeline && boundToSourceCell && !visualInDestinationCell;
    }

    inline bool shouldExpireRemoteLocomotion(float latestSnapshotAge,
        bool snapshotMoving, bool hasMovementAxes)
    {
        // Moving actors normally arrive every ~25 ms.  Once a remote stream has
        // been absent for this long, continuing to drive locomotion from the last
        // sample can only produce a stationary run/walk cycle.  Keep the timeout
        // comfortably above normal handoff/network jitter while bounding stale
        // presentation after interest loss or an unresolved destination binding.
        constexpr float staleLocomotionTimeout = 0.5f;
        return snapshotMoving && hasMovementAxes && latestSnapshotAge >= staleLocomotionTimeout;
    }

    inline bool shouldSuppressExteriorActorListCellMismatch(bool positionCellKnown,
        bool positionMatchesIncomingCell, float borderDistance, bool positionCellLoaded)
    {
        constexpr float hysteresis = 64.f;
        return positionCellKnown && !positionMatchesIncomingCell
            && borderDistance > hysteresis && positionCellLoaded;
    }

    inline bool shouldSuppressExteriorActorListCanonicalMigration(bool positionCellKnown,
        bool positionMatchesIncomingCell, float)
    {
        // A vanilla actor's engine Ptr changes CellStore as soon as it crosses an
        // exterior boundary. Waiting for additional depth inside the destination
        // makes the old authority stop enumerating it before the canonical server
        // cell can advance, freezing observers at the seam. Reject only snapshots
        // whose coordinates do not actually belong to the incoming cell.
        return !positionCellKnown || !positionMatchesIncomingCell;
    }

    inline bool shouldPreserveSourceAiAcrossVanillaMigration(
        bool hasMigratedRecord, uint32_t mpNum, bool sourceDiffersDestination)
    {
        return hasMigratedRecord && mpNum == 0 && sourceDiffersDestination;
    }

    inline bool shouldMergeCanonicalAiOnIdentityMigration(
        bool hadRuntime, bool explicitTransformReset, bool deathBaseline, bool cellChanged)
    {
        return hadRuntime && !explicitTransformReset && !deathBaseline && cellChanged;
    }

    inline bool shouldCollectPrimaryForExplicitCellChange(
        bool serverSpawned, bool /*positionOutsideCanonicalCell*/, bool alreadyCollected = true)
    {
        // CellStore enumeration can temporarily miss a Ptr immediately after an
        // exterior move. A canonical authoritative runtime must still produce
        // fresh snapshots or observers will loop its last locomotion forever.
        // A vanilla actor already sampled by CellStore enumeration has a fresh
        // outgoing state even when its coordinates crossed the seam. Sampling
        // it again repeats AI/presentation work and emits a duplicate warning
        // every frame until the commit. Spawned actors keep their canonical-Ptr
        // reconciliation pass for duplicate local copies.
        return !alreadyCollected || serverSpawned;
    }

    inline bool shouldAcceptLateExplicitVanillaCellChange(bool sourceMissing, uint32_t mpNum,
        bool destinationTracked, bool previousCellMatches, bool previousOwnerMatches,
        uint64_t ageMs, uint64_t graceMs = 1000)
    {
        return sourceMissing && mpNum == 0 && destinationTracked
            && previousCellMatches && previousOwnerMatches && ageMs <= graceMs;
    }

    inline bool shouldSuppressDestinationActorListMigration(
        bool trackedExteriorCrossCell, bool sourceAuthorityAvailable)
    {
        // Once a canonical actor has a live source authority, only that source may
        // migrate it. A destination ActorList can contain an interpolated puppet
        // that crossed the seam before the authoritative Ptr did; treating that
        // observer snapshot as a migration lets puppet AI/state become canonical.
        return trackedExteriorCrossCell && sourceAuthorityAvailable;
    }

    inline bool shouldRestoreCanonicalCombat(
        bool wasRemotePuppet, bool hadSavedAi, bool packetRequestsCombat)
    {
        // CellStore rebinding can replace scene-node user values. Canonical Combat
        // remains authoritative even if the old puppet/saved-AI markers were lost.
        return wasRemotePuppet || hadSavedAi || packetRequestsCombat;
    }
}

#endif
