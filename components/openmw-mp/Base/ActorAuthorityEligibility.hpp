#ifndef OPENMW_MP_ACTORAUTHORITYELIGIBILITY_HPP
#define OPENMW_MP_ACTORAUTHORITYELIGIBILITY_HPP

#include <charconv>
#include <cstdlib>
#include <cstdint>
#include <optional>
#include <string_view>

namespace mwmp
{
    inline bool exteriorCellWithinActorAuthorityRadius(
        int ownerX, int ownerY, int cellX, int cellY, int radius)
    {
        return radius >= 0 && std::abs(ownerX - cellX) <= radius
            && std::abs(ownerY - cellY) <= radius;
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
}

#endif
