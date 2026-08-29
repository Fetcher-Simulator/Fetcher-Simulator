#ifndef OPENMW_APPS_OPENMW_MWMP_SYNC_CRIMEREACTIONREADINESS_HPP
#define OPENMW_APPS_OPENMW_MWMP_SYNC_CRIMEREACTIONREADINESS_HPP

#include <components/openmw-mp/CrimeReaction.hpp>

namespace mwmp
{
    inline bool crimeReactionOffenderReady(
        const CrimeReactionDirective& directive, bool offenderPresent, bool offenderInDirectiveCell)
    {
        bool requiresOffender = false;
        bool requiresSameCell = false;
        for (const CrimeActorReaction& reaction : directive.actors)
        {
            if ((reaction.flags & (CrimeReactionPursueOffender | CrimeReactionClearPursuit
                    | CrimeReactionStartCombat)) != 0)
                requiresOffender = true;
            if ((reaction.flags & (CrimeReactionPursueOffender | CrimeReactionStartCombat)) != 0)
                requiresSameCell = true;
        }

        return !requiresOffender
            || (offenderPresent && (!requiresSameCell || offenderInDirectiveCell));
    }
}

#endif
