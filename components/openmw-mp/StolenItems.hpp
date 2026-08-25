#ifndef OPENMW_COMPONENTS_OPENMW_MP_STOLENITEMS_H
#define OPENMW_COMPONENTS_OPENMW_MP_STOLENITEMS_H

#include <cstdint>
#include <string>

namespace mwmp
{
    // Mirrors OpenMW's vanilla StolenItemsMap entry:
    // <item refId, <owner id, is-faction>, count>.
    struct StolenItemRecord
    {
        std::string refId;
        std::string ownerId;
        bool isFaction = false;
        std::int64_t count = 0;

        bool operator==(const StolenItemRecord&) const = default;
    };

    // Applied inside the same transaction as the inventory operation that
    // creates or consumes stolen provenance. Positive values record theft;
    // negative values consume provenance during return/confiscation.
    struct StolenItemMutation
    {
        std::string refId;
        std::string ownerId;
        bool isFaction = false;
        std::int64_t countDelta = 0;

        bool operator==(const StolenItemMutation&) const = default;
    };
}

#endif
