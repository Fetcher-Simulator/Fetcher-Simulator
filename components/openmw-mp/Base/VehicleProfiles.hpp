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
        float visualContactPlaneOffset;
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

    struct VehicleRigidBodyProfile
    {
        float mass;
        std::array<float, 3> centerOfMassFromVehicleRoot;
        std::array<float, 3> inertiaScale;
        std::array<float, 3> chassisHalfExtents;
        std::array<float, 3> chassisCenterFromVehicleRoot;
        std::array<float, 2> chassisLowerInset;
        float chassisLowerChamferHeight;
        float friction;
        float restitution;
        float linearDamping;
        float angularDamping;
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
        VehicleRigidBodyProfile rigidBody;
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
                1900.f,
                { 0.f, -8.f, 42.f },
                { 1.f, 1.f, 1.f },
                // Keep the physical chassis above the suspension-supported tire
                // contact plane. The larger profile collision box remains the
                // remote targeting proxy and includes the visual undercarriage.
                { 65.1f, 159.6f, 36.f },
                { 0.f, 0.f, 66.f },
                // Chamfer the long lower perimeter so box corners do not hook
                // heightfield triangle seams or small terrain ridges.
                { 8.f, 28.f },
                12.f,
                // Tire forces own vehicle grip. Low chassis friction prevents
                // the body from sticking to terrain when the underside contacts.
                0.05f,
                0.05f,
                0.04f,
                0.12f,
            },
            {
                { {
                    // Measured directly from the attached NIF. The suspension
                    // mount is one rest length above each authored wheel center,
                    // so the physical ray and visible tire sample the same terrain.
                    { "FV_Pickup85_WheelStock_FL.001", { -54.63913f, 110.99693f, 51.39135f }, 0.f },
                    { "FV_Pickup85_WheelStock_FR.001", { 54.56343f, 110.99697f, 51.39135f }, 0.f },
                    { "FV_Pickup85_WheelStock_RL.001", { -54.63913f, -92.05150f, 51.39135f }, 0.f },
                    { "FV_Pickup85_WheelStock_RR.001", { 54.56341f, -92.05148f, 51.39135f }, 0.f },
                } },
                24.39135f,
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
