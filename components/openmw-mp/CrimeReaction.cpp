#include "CrimeReaction.hpp"

#include <algorithm>
#include <string_view>
#include <unordered_set>

namespace
{
    bool validText(std::string_view value, std::size_t maximum)
    {
        if (value.empty() || value.size() > maximum)
            return false;
        return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
            return ch >= 0x20 && ch != 0x7f;
        });
    }
}

bool mwmp::validateCrimeReactionDirective(const CrimeReactionDirective& directive)
{
    if (directive.protocolVersion != CrimeReactionProtocolVersion
        || !validText(directive.eventId, MaximumCrimeReactionEventIdLength)
        || !validText(directive.cellId, MaximumCrimeReactionCellIdLength)
        || directive.actors.empty() || directive.actors.size() > MaximumCrimeReactionActors)
        return false;

    std::unordered_set<ActorInstanceId> actorIds;
    for (const CrimeActorReaction& actor : directive.actors)
    {
        if (!isValidActorInstanceId(actor.actorNetId) || actor.migrationGeneration == 0
            || (actor.flags & ~KnownCrimeReactionFlags) != 0
            || (actor.dialogue != CrimeReactionDialogue::None
                && actor.dialogue != CrimeReactionDialogue::Thief
                && actor.dialogue != CrimeReactionDialogue::Intruder)
            || (actor.dialogue == CrimeReactionDialogue::None && actor.flags == 0)
            || (((actor.flags & CrimeReactionSetFight) != 0)
                ? (actor.fight < 0 || actor.fight > 100
                    || (actor.flags & CrimeReactionStartCombat) == 0)
                : actor.fight != 0)
            || (((actor.flags & CrimeReactionPursueOffender) != 0
                    || (actor.flags & CrimeReactionClearPursuit) != 0
                    || (actor.flags & CrimeReactionStartCombat) != 0)
                && directive.offenderGuid == 0)
            || (((actor.flags & CrimeReactionPursueOffender) != 0
                    || (actor.flags & CrimeReactionStartCombat) != 0)
                && (actor.flags & CrimeReactionSetAlarmed) == 0)
            || (((actor.flags & CrimeReactionPursueOffender) != 0
                    ? 1u : 0u)
                    + ((actor.flags & CrimeReactionClearPursuit) != 0 ? 1u : 0u)
                    + ((actor.flags & CrimeReactionStartCombat) != 0 ? 1u : 0u)
                > 1u)
            || !actorIds.insert(actor.actorNetId).second)
            return false;
    }
    return true;
}

std::string mwmp::crimeReactionOffenderTargetId(std::uint32_t offenderGuid)
{
    return offenderGuid == 0
        ? std::string() : std::string("mp_remote_") + std::to_string(offenderGuid);
}
