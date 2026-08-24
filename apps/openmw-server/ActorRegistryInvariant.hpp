#ifndef OPENMW_SERVER_ACTORREGISTRYINVARIANT_HPP
#define OPENMW_SERVER_ACTORREGISTRYINVARIANT_HPP

#include <cstdint>

#include <components/openmw-mp/Base/BaseActor.hpp>

namespace mwmp
{
    // The server owns actor lifetime generations. Zero represents an
    // uninitialized canonical lifetime and must never be published for an actor
    // with a usable network identity.
    inline void ensureCanonicalActorMigrationGeneration(
        std::uint32_t& migrationGeneration, BaseActor& actor)
    {
        if (migrationGeneration == 0)
            migrationGeneration = 1;
        actor.migrationGeneration = migrationGeneration;
    }
}

#endif // OPENMW_SERVER_ACTORREGISTRYINVARIANT_HPP
