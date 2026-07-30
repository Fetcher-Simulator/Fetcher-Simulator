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

    struct VehicleWheelProfile
    {
        std::string_view visualNode;
        std::array<float, 3> mountPosition;
    };

    struct VehicleSuspensionProfile
    {
        std::array<VehicleWheelProfile, 4> wheels;
        float wheelRadius;
        float restLength;
        float maxCompression;
        float maxDroop;
        float probeAboveMount;
        float probeBelowMount;
        float tireContactPatchFraction;
        float supportProbeSlack;
        float springRate;
        float dampingRate;
        float maxCompressionSpeed;
        float maxPitchDegrees;
        float maxRollDegrees;
        float maxPoseRateDegrees;
        float maxVerticalPoseSpeed;
        float maxAirbornePitchDegrees;
        float edgeTipBaseRateDegrees;
        float edgeTipSpeedRateDegrees;
        float edgeTipResponseDegrees;
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
        VehicleSuspensionProfile suspension;
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
                { {
                    { "FV_Pickup85_WheelStock_FL.001", { -50.f, 102.f, 58.f } },
                    { "FV_Pickup85_WheelStock_FR.001", { 50.f, 102.f, 58.f } },
                    { "FV_Pickup85_WheelStock_RL.001", { -50.f, -100.f, 58.f } },
                    { "FV_Pickup85_WheelStock_RR.001", { 50.f, -100.f, 58.f } },
                } },
                27.5f,
                27.f,
                14.f,
                18.f,
                90.f,
                150.f,
                0.65f,
                25.f,
                80.f,
                18.f,
                80.f,
                28.f,
                24.f,
                60.f,
                80.f,
                60.f,
                12.f,
                55.f,
                120.f,
            },
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
