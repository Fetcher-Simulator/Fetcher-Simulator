#ifndef OPENMW_MWMP_SYNC_MECHANICSSNAPSHOTBUILDER_HPP
#define OPENMW_MWMP_SYNC_MECHANICSSNAPSHOTBUILDER_HPP

#include <cstdint>
#include <string>

#include <components/openmw-mp/Base/MechanicsSnapshot.hpp>

namespace MWWorld
{
    class Ptr;
}

namespace mwmp
{
    MechanicsSnapshot captureMechanicsSnapshot(const MWWorld::Ptr& ptr, MechanicsSubjectKind kind,
        std::uint32_t playerGuid, ActorInstanceId actorInstanceId, const std::string& cellId,
        std::uint32_t migrationGeneration, std::uint32_t authorityGeneration,
        std::uint32_t snapshotSequence);
}

#endif
