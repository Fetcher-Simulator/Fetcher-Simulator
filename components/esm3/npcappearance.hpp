#ifndef OPENMW_ESM3_NPCAPPEARANCE_HPP
#define OPENMW_ESM3_NPCAPPEARANCE_HPP

#include <components/esm/refid.hpp>
#include <array>
#include <cstdint>

namespace ESM
{
    // Runtime presentation only. Multiplayer persists this separately from
    // NPC_ content records; mRace remains the character's gameplay identity.
    struct NpcAppearance
    {
        RefId bodyRace; // empty: use identity race
        uint8_t animationRig = 0; // automatic, humanoid, beast
        bool tail = true;
        std::array<RefId, 15> parts; // BODY mesh kinds; head/hair use mHead/mHair
        bool operator==(const NpcAppearance&) const = default;
    };
}
#endif
