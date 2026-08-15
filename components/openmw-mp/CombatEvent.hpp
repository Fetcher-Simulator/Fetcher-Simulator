#ifndef OPENMW_MP_COMBATEVENT_HPP
#define OPENMW_MP_COMBATEVENT_HPP

#include <cmath>
#include <cstdint>

namespace mwmp
{
    inline constexpr std::uint16_t CombatEventWireVersion = 1;
    inline constexpr std::uint64_t MaximumCombatProposalAgeMs = 5000;

    enum CombatResultFlag : std::uint8_t
    {
        CombatResultApplied = 1u << 0,
        CombatVictimWasAggressive = 1u << 1,
        CombatVictimWasEngaged = 1u << 2,
        CombatVictimWasPursuing = 1u << 3,
        CombatVictimWasWerewolf = 1u << 4,
        CombatVictimWasVampire = 1u << 5,
        CombatVictimDied = 1u << 6,
    };

    inline constexpr std::uint8_t KnownCombatResultFlags = CombatResultApplied
        | CombatVictimWasAggressive | CombatVictimWasEngaged | CombatVictimWasPursuing
        | CombatVictimWasWerewolf | CombatVictimWasVampire | CombatVictimDied;

    inline bool isQualifyingCriminalAttack(std::uint8_t flags)
    {
        constexpr std::uint8_t exclusions = CombatVictimWasAggressive | CombatVictimWasEngaged
            | CombatVictimWasPursuing | CombatVictimWasWerewolf | CombatVictimWasVampire;
        return (flags & CombatResultApplied) != 0 && (flags & exclusions) == 0;
    }

    inline bool validateCombatResultFields(std::uint64_t eventId, std::uint64_t victimActorInstanceId,
        std::uint32_t migrationGeneration, std::uint32_t authorityGeneration,
        std::uint32_t resultSequence, std::uint8_t flags, float appliedDamage)
    {
        return eventId != 0 && victimActorInstanceId != 0 && migrationGeneration != 0
            && authorityGeneration != 0 && resultSequence != 0
            && (flags & ~KnownCombatResultFlags) == 0 && std::isfinite(appliedDamage)
            && appliedDamage >= 0.f && appliedDamage <= 1000000.f
            && (((flags & CombatResultApplied) != 0) == (appliedDamage > 0.f))
            && ((flags & CombatVictimDied) == 0 || (flags & CombatResultApplied) != 0);
    }
}

#endif
