#ifndef OPENMW_MP_ACTORAUTHORITYELIGIBILITY_HPP
#define OPENMW_MP_ACTORAUTHORITYELIGIBILITY_HPP

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <optional>
#include <string_view>

#include <components/misc/constants.hpp>

namespace mwmp
{
    inline bool exteriorCellWithinActorAuthorityRadius(
        int ownerX, int ownerY, int cellX, int cellY, int radius)
    {
        return radius >= 0 && std::abs(ownerX - cellX) <= radius
            && std::abs(ownerY - cellY) <= radius;
    }

    // AiPackage::isNearInactiveCell stops path movement within 200 units of
    // the edge of the 3x3 grid around the local player's cell. A new owner
    // needs additional room for actor movement while Ready/Commit is in flight.
    inline bool exteriorActorWithinAiBounds(
        int playerCellX, int playerCellY, float actorX, float actorY, float edgeMargin)
    {
        if (!std::isfinite(actorX) || !std::isfinite(actorY) || edgeMargin < 200.f
            || edgeMargin >= Constants::CellSizeInUnits)
            return false;
        const float localX = actorX - static_cast<float>(playerCellX) * Constants::CellSizeInUnits;
        const float localY = actorY - static_cast<float>(playerCellY) * Constants::CellSizeInUnits;
        const float min = -Constants::CellSizeInUnits + edgeMargin;
        const float max = 2 * Constants::CellSizeInUnits - edgeMargin;
        return localX >= min && localX <= max && localY >= min && localY <= max;
    }

    inline std::optional<std::uint32_t> remotePlayerGuidFromActorTarget(std::string_view targetId)
    {
        constexpr std::string_view prefix = "mp_remote_";
        if (!targetId.starts_with(prefix))
            return std::nullopt;

        targetId.remove_prefix(prefix.size());
        if (targetId.empty())
            return std::nullopt;

        std::uint32_t guid = 0;
        const auto [end, error] = std::from_chars(
            targetId.data(), targetId.data() + targetId.size(), guid);
        if (error != std::errc() || end != targetId.data() + targetId.size() || guid == 0)
            return std::nullopt;
        return guid;
    }

    // An authority client cannot turn an unresolved, still-connected player
    // target into a canonical idle state just because that player's proxy left
    // the client's active cells. A legitimate combat end remains reportable
    // while the target is locally available, or after it disconnects.
    inline bool shouldPreserveCombatAcrossTargetHorizon(
        bool canonicalCombat, bool incomingCombat, bool targetConnected,
        bool targetAvailableToOwner)
    {
        return canonicalCombat && !incomingCombat && targetConnected && !targetAvailableToOwner;
    }

    // A capable actor simulator stays in place across cell and AI changes,
    // including a combat target switch. Candidate preference applies only
    // after the owner loses actor or target coverage.
    inline bool shouldRetainActorSimulator(
        bool ownerCanSimulateActor, bool ownerCanSimulateCombatTarget)
    {
        return ownerCanSimulateActor && ownerCanSimulateCombatTarget;
    }
}

#endif
