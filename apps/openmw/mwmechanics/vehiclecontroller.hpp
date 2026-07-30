#ifndef OPENMW_MWMECHANICS_VEHICLECONTROLLER_H
#define OPENMW_MWMECHANICS_VEHICLECONTROLLER_H

#include <array>
#include <string>

#include <osg/Vec3f>

namespace mwmp
{
    struct VehicleProfile;
}

namespace MWWorld
{
    class Ptr;
}

namespace MWMechanics
{
    struct VehicleWheelContactState
    {
        bool mTerrainHit = false;
        bool mGrounded = false;
        float mRequiredSuspensionLength = 0.f;
        float mSuspensionLength = 0.f;
        float mCompression = 0.f;
        float mCompressionVelocity = 0.f;
        osg::Vec3f mContactPoint;
        osg::Vec3f mContactNormal = osg::Vec3f(0.f, 0.f, 1.f);
        osg::Vec3f mSurfaceVelocity;
        float mVisualOffset = 0.f;
    };

    struct VehicleSuspensionState
    {
        std::array<VehicleWheelContactState, 4> mWheels;
        std::string mProfileId;
        float mPitch = 0.f;
        float mRoll = 0.f;
        float mVerticalOffset = 0.f;
        float mPitchVelocity = 0.f;
        float mGroundPitchVelocity = 0.f;
        float mLastGroundPitch = 0.f;
        float mTerrainNormalPitch = 0.f;
        float mTerrainNormalRoll = 0.f;
        float mAirborneTime = 0.f;
        float mTakeoffDirection = 0.f;
        float mLandingSupportTime = 0.f;
        float mUnsupportedTime = 0.f;
        unsigned int mSupportedWheels = 0;
        bool mAirborne = false;
        bool mHadGroundContact = false;
        bool mTerrainBridge = false;
        bool mTerrainCrossBridge = false;
        bool mInitialized = false;
    };

    void updateLocalVehicle(const MWWorld::Ptr& player, float duration);
    void updateVehicleSuspension(const MWWorld::Ptr& actor, const mwmp::VehicleProfile& profile,
        float duration, float longitudinalSpeed, VehicleSuspensionState& state);
}

#endif
