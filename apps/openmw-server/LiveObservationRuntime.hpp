#ifndef OPENMW_SERVER_LIVEOBSERVATIONRUNTIME_HPP
#define OPENMW_SERVER_LIVEOBSERVATIONRUNTIME_HPP

#include <cstdint>
#include <random>

#include "MechanicsSnapshotRegistry.hpp"
#include "ObservationService.hpp"

namespace mwmp
{
    class ServerAwarenessRollSource final : public AwarenessRollSource
    {
    public:
        ServerAwarenessRollSource();
        explicit ServerAwarenessRollSource(std::uint64_t seed);
        int nextRoll0To99() override;

    private:
        std::mt19937_64 mEngine;
        std::uniform_int_distribution<int> mDistribution{ 0, 99 };
    };

    ObservationActorSnapshot makeLiveObservationSnapshot(
        const AcceptedMechanicsSnapshot& accepted, float serverDerivedBootWeight);
}

#endif
