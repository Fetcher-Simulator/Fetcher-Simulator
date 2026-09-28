#ifndef OPENMW_MWMECHANICS_ACTORSUPPORT_H
#define OPENMW_MWMECHANICS_ACTORSUPPORT_H
#include <cmath>
namespace MWMechanics
{
    // This says nothing about physical grounding or accumulated fall height.
    inline bool hasStableActorSupport(bool walkable, bool flying, float verticalInput,
        float verticalInertia, float verticalDisplacement, float dt)
    {
        return walkable && !flying && verticalInput <= 0.f && verticalInertia <= 0.f
            && dt > 0.f && std::abs(verticalDisplacement) <= 20.f * dt;
    }
}
#endif
