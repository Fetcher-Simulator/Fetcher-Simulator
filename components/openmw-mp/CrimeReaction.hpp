#ifndef OPENMW_COMPONENTS_OPENMW_MP_CRIMEREACTION_HPP
#define OPENMW_COMPONENTS_OPENMW_MP_CRIMEREACTION_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <components/openmw-mp/Base/ActorSyncProtocol.hpp>

namespace mwmp
{
    inline constexpr std::uint16_t CrimeReactionProtocolVersion = 3;
    inline constexpr std::size_t MaximumCrimeReactionEventIdLength = 128;
    inline constexpr std::size_t MaximumCrimeReactionCellIdLength = 255;
    inline constexpr std::size_t MaximumCrimeReactionActors = 64;

    enum class CrimeReactionDialogue : std::uint8_t
    {
        None = 0,
        Thief = 1,
        Intruder = 2,
    };

    enum CrimeReactionFlag : std::uint8_t
    {
        CrimeReactionSetAlarmed = 1u << 0,
        CrimeReactionPursueOffender = 1u << 1,
        CrimeReactionClearPursuit = 1u << 2,
        CrimeReactionStartCombat = 1u << 3,
        CrimeReactionSetFight = 1u << 4,
    };

    inline constexpr std::uint8_t KnownCrimeReactionFlags = CrimeReactionSetAlarmed
        | CrimeReactionPursueOffender | CrimeReactionClearPursuit | CrimeReactionStartCombat
        | CrimeReactionSetFight;

    struct CrimeActorReaction
    {
        ActorInstanceId actorNetId = 0;
        std::uint32_t migrationGeneration = 0;
        CrimeReactionDialogue dialogue = CrimeReactionDialogue::None;
        std::uint8_t flags = 0;
        std::int32_t fight = 0;

        bool operator==(const CrimeActorReaction&) const = default;
    };

    struct CrimeReactionDirective
    {
        std::uint16_t protocolVersion = CrimeReactionProtocolVersion;
        std::string eventId;
        std::string cellId;
        std::uint32_t offenderGuid = 0;
        std::vector<CrimeActorReaction> actors;

        bool operator==(const CrimeReactionDirective&) const = default;
    };

    bool validateCrimeReactionDirective(const CrimeReactionDirective& directive);
    std::string crimeReactionOffenderTargetId(std::uint32_t offenderGuid);
}

#endif
