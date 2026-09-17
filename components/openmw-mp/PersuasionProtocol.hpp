#pragma once
#include <cstdint>
#include <string>

namespace mwmp
{
    enum class PersuasionAction : std::uint8_t { Admire, Intimidate, Taunt, Bribe10, Bribe100, Bribe1000, Read, Close };
    enum class PersuasionError : std::uint8_t { None, Invalid, Stale, Unavailable, InsufficientGold, Conflict, Pending };
    struct PersuasionRequest
    {
        std::string requestId;
        std::uint64_t actorId = 0, inventoryRevision = 0, relationshipRevision = 0;
        std::uint32_t generation = 0;
        PersuasionAction action = PersuasionAction::Read;
    };
    struct PersuasionResult
    {
        std::string requestId;
        std::uint64_t actorId = 0, inventoryRevision = 0, relationshipRevision = 0;
        PersuasionError error = PersuasionError::None;
        bool success = false;
        int baseDisposition = 0, currentDisposition = 0, chargedGold = 0, fight = 0, flee = 0;
    };
}
