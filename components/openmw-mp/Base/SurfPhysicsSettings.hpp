#ifndef OPENMW_MP_SURFPHYSICSSETTINGS_HPP
#define OPENMW_MP_SURFPHYSICSSETTINGS_HPP

#include <cmath>
#include <string>

namespace mwmp
{
    struct SurfPhysicsSettings
    {
        std::string cellId;
        bool enabled = false;
        float airAcceleration = 70.f;
        float maxAirSpeed = 2000.f;
        float groundFriction = 5.f;
        float groundAcceleration = 10.f;
        float jumpSpeed = 268.f;
        float gravityMultiplier = 1.f;
        float overbounce = 1.1f;
        float rampAngle = 0.8f;
        float impactOverbounce = 1.1f;
        float impactVelocityThreshold = 200.f;
        // Reference values used only by movement, never written to character stats.
        bool normalizeMovement = false;
        float referenceSpeed = 100.f;
        float referenceAthletics = 100.f;
        float referenceAcrobatics = 50.f;

        bool hasNormalizedMovement() const { return enabled && normalizeMovement; }

        bool validMovementProfile() const
        {
            const auto valid = [](float value) { return std::isfinite(value) && value >= 0.f && value <= 100.f; };
            return valid(referenceSpeed) && valid(referenceAthletics) && valid(referenceAcrobatics);
        }
    };

    inline SurfPhysicsSettings resolveSurfPhysicsSettings(const SurfPhysicsSettings& global,
        const SurfPhysicsSettings* cell, const SurfPhysicsSettings* player, const std::string& cellId)
    {
        SurfPhysicsSettings result = cell ? *cell : global;
        if (player && !result.hasNormalizedMovement())
            result = *player;
        result.cellId = cellId;
        return result;
    }
}

#endif
