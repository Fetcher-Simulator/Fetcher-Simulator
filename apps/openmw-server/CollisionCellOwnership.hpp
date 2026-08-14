#ifndef OPENMW_SERVER_COLLISIONCELLOWNERSHIP_HPP
#define OPENMW_SERVER_COLLISIONCELLOWNERSHIP_HPP

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <components/openmw-mp/Base/BasePlayer.hpp>

namespace mwmp
{
    /// Tracks logical owners independently from the collision backend. Updates
    /// return deterministic set differences that callers apply as cell
    /// acquire/release operations.
    class CollisionCellOwnership
    {
    public:
        struct Transition
        {
            std::vector<std::string> acquire;
            std::vector<std::string> release;
        };

        Transition update(std::string_view ownerId, std::vector<std::string> desiredCells);
        Transition remove(std::string_view ownerId);
        std::vector<std::string> cells(std::string_view ownerId) const;
        std::size_t ownerCount() const { return mOwners.size(); }

    private:
        std::unordered_map<std::string, std::unordered_set<std::string>> mOwners;
    };

    /// Computes the bounded collision interest for an authenticated player's
    /// accepted cell and position. Exterior neighbors are included only when
    /// the observation-radius circle intersects their cell rectangle.
    std::vector<std::string> collisionCellsForPlayer(
        const CellId& cell, const Position& position, float observationRadius);
}

#endif
