#ifndef OPENMW_MP_VEHICLEPROFILES_HPP
#define OPENMW_MP_VEHICLEPROFILES_HPP

#include <array>
#include <string_view>

namespace mwmp
{
    struct VehicleHandlingProfile
    {
        float wheelbase;
        float maxForwardSpeed;
        float maxReverseSpeed;
        float engineAcceleration;
        float reverseAcceleration;
        float serviceBrakeStrength;
        float handbrakeStrength;
        float rollingResistance;
        float aerodynamicDrag;
        float lowSpeedSteeringDegrees;
        float highSpeedSteeringDegrees;
        float steeringResponseDegrees;
        float steeringReturnDegrees;
        float lateralGrip;
        float handbrakeLateralGrip;
        float directionChangeSpeed;
    };

    struct VehicleProfile
    {
        std::string_view id;
        std::string_view displayName;
        std::string_view parkedRefId;
        std::string_view attachedModel;
        std::array<float, 3> collisionHalfExtents;
        std::array<float, 3> collisionCenterFromVehicleRoot;
        std::array<float, 3> firstPersonCameraOffset;
        VehicleHandlingProfile handling;
    };

    inline constexpr std::array<VehicleProfile, 1> sVehicleProfiles = { {
        {
            "fetcher.vehicles.pickup_85.v1",
            "Lightbody Pickup '85",
            "fv_pickup_85",
            "meshes/fetchervehicles/fv_pickup_85_attached.nif",
            { 65.1f, 159.6f, 44.8f },
            { 0.f, 0.f, 57.4f },
            { -24.5085f, 23.8851f, 101.7932f },
            {
                210.f,
                1280.f,
                360.f,
                350.f,
                220.f,
                700.f,
                850.f,
                24.f,
                0.0002f,
                30.f,
                8.f,
                105.f,
                140.f,
                7.f,
                1.4f,
                12.f,
            },
        },
    } };

    constexpr const VehicleProfile* findVehicleProfile(std::string_view id) noexcept
    {
        for (const VehicleProfile& profile : sVehicleProfiles)
        {
            if (profile.id == id)
                return &profile;
        }
        return nullptr;
    }
}

#endif // OPENMW_MP_VEHICLEPROFILES_HPP
