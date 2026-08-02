#ifndef OPENMW_MWPHYSICS_VEHICLEBODY_H
#define OPENMW_MWPHYSICS_VEHICLEBODY_H

#include "ptrholder.hpp"

#include <array>
#include <memory>
#include <mutex>

#include <osg/Quat>
#include <osg/Vec3f>

class btCompoundShape;
class btConvexHullShape;
class btDynamicsWorld;
class btRigidBody;

namespace MWPhysics
{
    class PhysicsTaskScheduler;

    struct VehicleBodyConfig
    {
        osg::Vec3f mCollisionHalfExtents;
        osg::Vec3f mCollisionCenterFromRoot;
        osg::Vec3f mCenterOfMassFromRoot;
        float mChassisLowerInsetX = 0.f;
        float mChassisLowerInsetY = 0.f;
        float mChassisLowerChamferHeight = 0.f;
        float mChassisUpperInsetX = 0.f;
        float mChassisUpperInsetY = 0.f;
        float mChassisUpperChamferHeight = 0.f;
        std::array<osg::Vec3f, 4> mWheelMountPositions;
        osg::Vec3f mInertiaScale = osg::Vec3f(1.f, 1.f, 1.f);
        float mWheelRadius = 1.f;
        float mSuspensionRestLength = 0.f;
        float mSuspensionMaxCompression = 0.f;
        float mSuspensionMaxDroop = 0.f;
        float mSuspensionSpringRate = 0.f;
        float mSuspensionDampingRate = 0.f;
        float mMaximumSupportSlopeDegrees = 0.f;
        float mMass = 1.f;
        float mFriction = 0.9f;
        float mRestitution = 0.f;
        float mLinearDamping = 0.f;
        float mAngularDamping = 0.f;
        float mWheelbase = 1.f;
        float mMaxForwardSpeed = 1.f;
        float mMaxReverseSpeed = 1.f;
        float mEngineAcceleration = 0.f;
        float mReverseAcceleration = 0.f;
        float mServiceBrakeStrength = 0.f;
        float mHandbrakeStrength = 0.f;
        float mParkingBrakeCaptureSpeed = 0.f;
        float mParkingBrakeMaxSlopeDegrees = 0.f;
        float mRollingResistance = 0.f;
        float mAerodynamicDrag = 0.f;
        float mLowSpeedSteeringDegrees = 0.f;
        float mHighSpeedSteeringDegrees = 0.f;
        float mLateralGrip = 0.f;
        float mStaticLateralFriction = 0.f;
        float mStaticLateralCaptureSpeed = 0.f;
        float mHandbrakeLateralGrip = 0.f;
        float mHandbrakeSlipStartSpeed = 0.f;
        float mHandbrakeSlipFullSpeed = 0.f;
        float mDirectionChangeSpeed = 0.f;
    };

    struct VehicleBodyInput
    {
        float mThrottle = 0.f;
        float mBrake = 0.f;
        float mSteering = 0.f;
        float mHandbrake = 0.f;
    };

    struct VehicleBodyState
    {
        osg::Vec3f mRootPosition;
        osg::Quat mOrientation;
        osg::Vec3f mLinearVelocity;
        osg::Vec3f mAngularVelocity;
        float mSteeringAngle = 0.f;
        float mHandbrakeSlipFactor = 0.f;
        float mStaticLateralFrictionUsage = 0.f;
        float mGroundSlopeDegrees = 0.f;
        bool mParkingBrakeHolding = false;
        std::array<float, 4> mSuspensionCompression{};
        std::array<float, 4> mSuspensionLength{};
        std::array<bool, 4> mWheelGrounded{};
    };

    class VehicleBody final : public PtrHolder
    {
    public:
        VehicleBody(
            const MWWorld::Ptr& ptr, const VehicleBodyConfig& config, PhysicsTaskScheduler* scheduler);
        ~VehicleBody() override;

        VehicleBody(const VehicleBody&) = delete;
        VehicleBody& operator=(const VehicleBody&) = delete;

        btRigidBody* getRigidBody() const;
        VehicleBodyState getState() const;
        void setInput(const VehicleBodyInput& input);

        // Called only while the scheduler owns the dynamics-world lock.
        void applyForces(btDynamicsWorld* world, float dt);
        void captureState();

    private:
        std::unique_ptr<btConvexHullShape> mChassisShape;
        std::unique_ptr<btCompoundShape> mCompoundShape;
        std::array<osg::Vec3f, 4> mScaledWheelMounts;
        osg::Vec3f mScaledCenterOfMassFromRoot;
        float mScaledWheelRadius = 0.f;
        float mScaledSuspensionRestLength = 0.f;
        float mScaledSuspensionMaxCompression = 0.f;
        float mScaledSuspensionMaxDroop = 0.f;
        float mSuspensionSpringRate = 0.f;
        float mSuspensionDampingRate = 0.f;
        float mMass = 1.f;
        VehicleBodyConfig mConfig;
        PhysicsTaskScheduler* mTaskScheduler;

        std::mutex mInputMutex;
        VehicleBodyInput mInput;
        float mSteeringAngle = 0.f;
        float mHandbrakeSlipFactor = 0.f;
        float mStaticLateralFrictionUsage = 0.f;
        float mGroundSlopeDegrees = 0.f;
        bool mParkingBrakeHolding = false;
        std::array<float, 4> mSuspensionCompression{};
        std::array<float, 4> mSuspensionLength{};
        std::array<bool, 4> mWheelGrounded{};
        mutable std::mutex mStateMutex;
        VehicleBodyState mState;
    };
}

#endif
