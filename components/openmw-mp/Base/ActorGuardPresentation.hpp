#ifndef OPENMW_MP_ACTORGUARDPRESENTATION_HPP
#define OPENMW_MP_ACTORGUARDPRESENTATION_HPP
namespace mwmp
{
    inline bool isGuardHostileCreature(bool dead, bool inCombat, bool aggressionCapable,
        int fightTerm, bool followerOrEscorter)
    {
        return !dead && inCombat && aggressionCapable && fightTerm >= 100 && !followerOrEscorter;
    }
}
#endif
