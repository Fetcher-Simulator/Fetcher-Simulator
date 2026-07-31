#include "vehiclebody.hpp"

#include "collisiontype.hpp"
#include "mtphysics.hpp"

#include <components/misc/constants.hpp>
#include <components/misc/convert.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>

#include <BulletCollision/CollisionShapes/btCompoundShape.h>
#include <BulletCollision/CollisionShapes/btConvexHullShape.h>
#include <BulletCollision/CollisionShapes/btSphereShape.h>
#include <BulletDynamics/Dynamics/btDynamicsWorld.h>
#include <BulletDynamics/Dynamics/btRigidBody.h>
#include <LinearMath/btMatrix3x3.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace MWPhysics
{
    namespace
    {
        bool isFinite(const osg::Vec3f& value)
        {
            return std::isfinite(value.x()) && std::isfinite(value.y()) && std::isfinite(value.z());
        }
    }

    VehicleBody::VehicleBody(
        const MWWorld::Ptr& ptr, const VehicleBodyConfig& config, PhysicsTaskScheduler* scheduler)
        : PtrHolder(ptr, ptr.getRefData().getPosition().asVec3())
        , mConfig(config)
        , mTaskScheduler(scheduler)
    {
        if (!scheduler || config.mMass <= 0.f || !std::isfinite(config.mMass)
            || !isFinite(config.mCollisionHalfExtents) || !isFinite(config.mCollisionCenterFromRoot)
            || !isFinite(config.mCenterOfMassFromRoot) || !isFinite(config.mInertiaScale)
            || config.mCollisionHalfExtents.x() <= 0.f || config.mCollisionHalfExtents.y() <= 0.f
            || config.mCollisionHalfExtents.z() <= 0.f
            || !std::isfinite(config.mChassisLowerInsetX)
            || !std::isfinite(config.mChassisLowerInsetY)
            || !std::isfinite(config.mChassisLowerChamferHeight)
            || config.mChassisLowerInsetX < 0.f || config.mChassisLowerInsetY < 0.f
            || config.mChassisLowerChamferHeight < 0.f
            || config.mChassisLowerInsetX >= config.mCollisionHalfExtents.x()
            || config.mChassisLowerInsetY >= config.mCollisionHalfExtents.y()
            || config.mChassisLowerChamferHeight > config.mCollisionHalfExtents.z() * 2.f
            || config.mWheelRadius <= 0.f
            || !std::isfinite(config.mWheelRadius) || config.mSuspensionRestLength < 0.f
            || !std::isfinite(config.mSuspensionRestLength) || config.mSuspensionMaxCompression < 0.f
            || !std::isfinite(config.mSuspensionMaxCompression) || config.mSuspensionMaxDroop < 0.f
            || !std::isfinite(config.mSuspensionMaxDroop) || config.mSuspensionSpringRate < 0.f
            || !std::isfinite(config.mSuspensionSpringRate) || config.mSuspensionDampingRate < 0.f
            || !std::isfinite(config.mSuspensionDampingRate)
            || config.mWheelbase <= 0.f || !std::isfinite(config.mWheelbase)
            || config.mMaxForwardSpeed <= 0.f || !std::isfinite(config.mMaxForwardSpeed)
            || config.mMaxReverseSpeed <= 0.f || !std::isfinite(config.mMaxReverseSpeed)
            || !std::all_of(config.mWheelMountPositions.begin(), config.mWheelMountPositions.end(), isFinite))
        {
            throw std::invalid_argument("Invalid vehicle rigid-body configuration");
        }

        float scale = 1.f;
        osg::Quat orientation;
        if (const SceneUtil::PositionAttitudeTransform* baseNode = ptr.getRefData().getBaseNode())
        {
            scale = std::max(std::abs(baseNode->getScale().y()), 0.001f);
            orientation = baseNode->getAttitude();
        }
        else
        {
            orientation = osg::Quat(
                ptr.getRefData().getPosition().rot[2], osg::Vec3f(0.f, 0.f, -1.f));
        }

        const osg::Vec3f halfExtents = config.mCollisionHalfExtents * scale;
        const osg::Vec3f collisionCenter = config.mCollisionCenterFromRoot * scale;
        mScaledCenterOfMassFromRoot = config.mCenterOfMassFromRoot * scale;
        mScaledWheelRadius = config.mWheelRadius * scale;
        mScaledSuspensionRestLength = config.mSuspensionRestLength * scale;
        mScaledSuspensionMaxCompression = config.mSuspensionMaxCompression * scale;
        mScaledSuspensionMaxDroop = config.mSuspensionMaxDroop * scale;
        mSuspensionSpringRate = config.mSuspensionSpringRate;
        mSuspensionDampingRate = config.mSuspensionDampingRate;
        mMass = config.mMass;
        for (std::size_t index = 0; index < mScaledWheelMounts.size(); ++index)
        {
            mScaledWheelMounts[index] = config.mWheelMountPositions[index] * scale;
            mSuspensionLength[index] = mScaledSuspensionRestLength + mScaledSuspensionMaxDroop;
        }

        // A long rectangular lower edge can hook individual heightfield
        // triangles even with very low friction. Use a convex chassis hull with
        // an inset lower footprint, producing ramps around the underside instead
        // of sharp front, rear, and side corners.
        const float lowerHalfX = halfExtents.x() - config.mChassisLowerInsetX * scale;
        const float lowerHalfY = halfExtents.y() - config.mChassisLowerInsetY * scale;
        const float shoulderZ = -halfExtents.z()
            + config.mChassisLowerChamferHeight * scale;
        mChassisShape = std::make_unique<btConvexHullShape>();
        auto addRectangle = [&](float halfX, float halfY, float z) {
            mChassisShape->addPoint(btVector3(-halfX, -halfY, z), false);
            mChassisShape->addPoint(btVector3(halfX, -halfY, z), false);
            mChassisShape->addPoint(btVector3(halfX, halfY, z), false);
            mChassisShape->addPoint(btVector3(-halfX, halfY, z), false);
        };
        addRectangle(halfExtents.x(), halfExtents.y(), halfExtents.z());
        addRectangle(halfExtents.x(), halfExtents.y(), shoulderZ);
        addRectangle(lowerHalfX, lowerHalfY, -halfExtents.z());
        mChassisShape->recalcLocalAabb();
        mChassisShape->setMargin(0.5f);
        mCompoundShape = std::make_unique<btCompoundShape>();

        btTransform childTransform;
        childTransform.setIdentity();
        childTransform.setOrigin(
            Misc::Convert::toBullet(collisionCenter - mScaledCenterOfMassFromRoot));
        mCompoundShape->addChildShape(childTransform, mChassisShape.get());

        btVector3 localInertia(0, 0, 0);
        mCompoundShape->calculateLocalInertia(config.mMass, localInertia);
        localInertia.setX(localInertia.x() * std::max(config.mInertiaScale.x(), 0.001f));
        localInertia.setY(localInertia.y() * std::max(config.mInertiaScale.y(), 0.001f));
        localInertia.setZ(localInertia.z() * std::max(config.mInertiaScale.z(), 0.001f));

        btRigidBody::btRigidBodyConstructionInfo bodyInfo(
            config.mMass, nullptr, mCompoundShape.get(), localInertia);
        bodyInfo.m_friction = std::max(config.mFriction, 0.f);
        bodyInfo.m_restitution = std::clamp(config.mRestitution, 0.f, 1.f);
        bodyInfo.m_linearDamping = std::clamp(config.mLinearDamping, 0.f, 1.f);
        bodyInfo.m_angularDamping = std::clamp(config.mAngularDamping, 0.f, 1.f);

        auto body = std::make_unique<btRigidBody>(bodyInfo);
        body->setUserPointer(this);
        body->setSleepingThresholds(2.f, 0.05f);

        btTransform initialTransform;
        initialTransform.setIdentity();
        initialTransform.setRotation(Misc::Convert::toBullet(orientation));
        initialTransform.setOrigin(Misc::Convert::toBullet(
            ptr.getRefData().getPosition().asVec3() + orientation * mScaledCenterOfMassFromRoot));
        body->setWorldTransform(initialTransform);
        body->setInterpolationWorldTransform(initialTransform);

        mCollisionObject = std::move(body);
        captureState();
        mTaskScheduler->addVehicleBody(this, CollisionType_Actor,
            CollisionType_World | CollisionType_HeightMap | CollisionType_Actor | CollisionType_Door
                | CollisionType_Projectile);
    }

    VehicleBody::~VehicleBody()
    {
        mTaskScheduler->removeVehicleBody(this);
    }

    btRigidBody* VehicleBody::getRigidBody() const
    {
        return static_cast<btRigidBody*>(mCollisionObject.get());
    }

    VehicleBodyState VehicleBody::getState() const
    {
        std::scoped_lock lock(mStateMutex);
        return mState;
    }

    void VehicleBody::setInput(const VehicleBodyInput& input)
    {
        std::scoped_lock lock(mInputMutex);
        mInput.mThrottle = std::clamp(input.mThrottle, 0.f, 1.f);
        mInput.mBrake = std::clamp(input.mBrake, 0.f, 1.f);
        mInput.mSteering = std::clamp(input.mSteering, -1.f, 1.f);
        mInput.mHandbrake = std::clamp(input.mHandbrake, 0.f, 1.f);
    }

    void VehicleBody::applyForces(btDynamicsWorld* world, float dt)
    {
        btRigidBody* body = getRigidBody();
        if (!world || !body || dt <= 0.f)
            return;

        const btTransform& transform = body->getWorldTransform();
        const btVector3 centerOfMass = transform.getOrigin();
        const btVector3 downDirection = transform.getBasis() * btVector3(0, 0, -1);
        const btVector3 upDirection = -downDirection;
        const btScalar minimumLength
            = mScaledSuspensionRestLength - mScaledSuspensionMaxCompression;
        const btScalar maximumLength
            = mScaledSuspensionRestLength + mScaledSuspensionMaxDroop;
        // Begin the wheel-volume sweep one radius above the mechanical bump stop.
        // A hit in this probe range represents tire/chassis overtravel and is fed
        // into a stiff bump-stop force below instead of allowing visible burial.
        const btScalar sweepStartLength = minimumLength - mScaledWheelRadius;
        const btScalar sweepDistance = maximumLength - sweepStartLength;
        btSphereShape wheelSweepShape(mScaledWheelRadius);
        const btScalar quarterMass = mMass * 0.25f;
        const btScalar gravityAcceleration = Constants::GravityConst * Constants::UnitsPerMeter;
        const btScalar maxSuspensionForce = quarterMass * gravityAcceleration * 3.f;
        const btScalar maxBumpStopForce = quarterMass * gravityAcceleration * 12.f;
        unsigned int groundedWheels = 0;
        btVector3 contactNormalSum(0, 0, 0);

        for (std::size_t index = 0; index < mScaledWheelMounts.size(); ++index)
        {
            const btVector3 localMount
                = Misc::Convert::toBullet(mScaledWheelMounts[index] - mScaledCenterOfMassFromRoot);
            const btVector3 mountPosition = transform * localMount;

            mWheelGrounded[index] = false;
            mSuspensionCompression[index] = 0.f;
            mSuspensionLength[index] = static_cast<float>(maximumLength);

            const btVector3 sweepFromPosition
                = mountPosition + downDirection * sweepStartLength;
            const btVector3 sweepToPosition
                = mountPosition + downDirection * maximumLength;
            const btQuaternion sweepRotation = btQuaternion::getIdentity();
            const btTransform sweepFrom(sweepRotation, sweepFromPosition);
            const btTransform sweepTo(sweepRotation, sweepToPosition);

            btCollisionWorld::ClosestConvexResultCallback callback(
                sweepFromPosition, sweepToPosition);
            callback.m_collisionFilterGroup = CollisionType_Actor;
            callback.m_collisionFilterMask
                = CollisionType_World | CollisionType_HeightMap | CollisionType_Door;
            world->convexSweepTest(&wheelSweepShape, sweepFrom, sweepTo, callback);
            if (!callback.hasHit())
                continue;

            btVector3 hitNormal = callback.m_hitNormalWorld;
            hitNormal.normalize();
            const btScalar normalAlignment = hitNormal.dot(upDirection);
            if (normalAlignment <= 0.1f)
                continue;

            // Sweeping the entire tire volume detects the leading arc before a
            // center ray reaches an uphill transition. The raw length may lie
            // above the mechanical bump stop; preserve that overtravel for the
            // stiff chassis-support term below, then clamp visual suspension.
            const btScalar rawSuspensionLength
                = sweepStartLength + sweepDistance * callback.m_closestHitFraction;

            const btScalar clampedLength
                = std::clamp(rawSuspensionLength, minimumLength, maximumLength);
            const btScalar compression
                = std::max(mScaledSuspensionRestLength - clampedLength, btScalar(0));
            const btScalar bumpStopOvertravel
                = std::max(minimumLength - rawSuspensionLength, btScalar(0));
            const btVector3 pointVelocity = body->getVelocityInLocalPoint(mountPosition - centerOfMass);
            const btScalar compressionVelocity = pointVelocity.dot(downDirection);
            const btScalar bumpStopRate = mSuspensionSpringRate * btScalar(8.f);
            const btScalar supportAcceleration
                = mSuspensionSpringRate * compression + mSuspensionDampingRate * compressionVelocity
                + bumpStopRate * bumpStopOvertravel;
            const btScalar supportForceLimit
                = bumpStopOvertravel > 0.f ? maxBumpStopForce : maxSuspensionForce;
            const btScalar supportForce
                = std::clamp(quarterMass * supportAcceleration, btScalar(0), supportForceLimit);

            mWheelGrounded[index] = true;
            ++groundedWheels;
            contactNormalSum += hitNormal;
            mSuspensionCompression[index] = static_cast<float>(compression);
            mSuspensionLength[index] = static_cast<float>(clampedLength);
            if (supportForce > 0.f)
                body->applyForce(upDirection * supportForce, mountPosition - centerOfMass);
        }

        VehicleBodyInput input;
        {
            std::scoped_lock lock(mInputMutex);
            input = mInput;
        }

        const bool hasInput = input.mThrottle > 0.001f || input.mBrake > 0.001f
            || std::abs(input.mSteering) > 0.001f || input.mHandbrake > 0.001f;
        if (hasInput)
            body->activate(true);

        const btMatrix3x3& basis = transform.getBasis();
        const btVector3 linearVelocity = body->getLinearVelocity();
        const btVector3 localVelocity = basis.transpose() * linearVelocity;
        const btScalar forwardSpeed = localVelocity.y();
        const btScalar speedRatio
            = std::clamp(std::abs(forwardSpeed) / mConfig.mMaxForwardSpeed, btScalar(0), btScalar(1));
        const btScalar steeringDegrees = mConfig.mLowSpeedSteeringDegrees
            + (mConfig.mHighSpeedSteeringDegrees - mConfig.mLowSpeedSteeringDegrees) * speedRatio;
        const btScalar steeringAngle
            = input.mSteering * steeringDegrees * static_cast<btScalar>(osg::PI / 180.0);
        mSteeringAngle = static_cast<float>(steeringAngle);

        if (groundedWheels == 0)
            return;

        const btScalar traction = static_cast<btScalar>(groundedWheels) * 0.25f;
        btVector3 averageContactNormal = contactNormalSum;
        if (averageContactNormal.length2() > SIMD_EPSILON)
            averageContactNormal.normalize();
        else
            averageContactNormal = upDirection;

        const btVector3 forward = basis * btVector3(0, 1, 0);
        const btVector3 right = basis * btVector3(1, 0, 0);
        const btVector3 tangentVelocity
            = linearVelocity - averageContactNormal * linearVelocity.dot(averageContactNormal);
        const btScalar tangentSpeed = tangentVelocity.length();
        const btScalar lateralSpeed = localVelocity.x();
        const bool parkingBrakeHold = input.mHandbrake > 0.f
            && input.mThrottle <= 0.001f && input.mBrake <= 0.001f
            && tangentSpeed < mConfig.mDirectionChangeSpeed;

        btScalar longitudinalAcceleration = 0.f;
        if (input.mThrottle > 0.f && forwardSpeed < mConfig.mMaxForwardSpeed)
            longitudinalAcceleration += mConfig.mEngineAcceleration * input.mThrottle;

        if (input.mBrake > 0.f)
        {
            if (forwardSpeed > mConfig.mDirectionChangeSpeed)
                longitudinalAcceleration -= mConfig.mServiceBrakeStrength * input.mBrake;
            else if (forwardSpeed > -mConfig.mMaxReverseSpeed)
                longitudinalAcceleration -= mConfig.mReverseAcceleration * input.mBrake;
        }

        if (!parkingBrakeHold && input.mHandbrake > 0.f && std::abs(forwardSpeed) > 0.01f)
        {
            const btScalar braking = std::min<btScalar>(
                mConfig.mHandbrakeStrength * input.mHandbrake, std::abs(forwardSpeed) / dt);
            longitudinalAcceleration -= std::copysign(braking, forwardSpeed);
        }

        if (!parkingBrakeHold && std::abs(forwardSpeed) > 0.01f)
        {
            const btScalar resistance = std::min<btScalar>(
                mConfig.mRollingResistance
                    + mConfig.mAerodynamicDrag * forwardSpeed * forwardSpeed,
                std::abs(forwardSpeed) / dt);
            longitudinalAcceleration -= std::copysign(resistance, forwardSpeed);
        }

        body->applyCentralForce(forward * (mMass * longitudinalAcceleration * traction));

        if (parkingBrakeHold)
        {
            // Static tire friction must oppose both existing motion and the
            // component of gravity that lies along the supporting surface. Pure
            // velocity damping repeatedly slows a parked vehicle but allows slope
            // gravity to recreate the slide on the following substep.
            const btScalar maxAcceleration
                = mConfig.mHandbrakeStrength * input.mHandbrake;
            btVector3 holdAcceleration
                = -(world->getGravity()
                    - averageContactNormal * world->getGravity().dot(averageContactNormal));
            if (tangentSpeed > 0.01f)
            {
                const btScalar stopAcceleration
                    = std::min<btScalar>(maxAcceleration, tangentSpeed / dt);
                holdAcceleration -= tangentVelocity * (stopAcceleration / tangentSpeed);
            }

            const btScalar holdAcceleration2 = holdAcceleration.length2();
            if (maxAcceleration > 0.f
                && holdAcceleration2 > maxAcceleration * maxAcceleration)
            {
                holdAcceleration *= maxAcceleration / std::sqrt(holdAcceleration2);
            }
            body->applyCentralForce(holdAcceleration * (mMass * traction));
        }
        else
        {
            const btScalar grip = std::max<btScalar>(0.f,
                mConfig.mLateralGrip
                    + (mConfig.mHandbrakeLateralGrip - mConfig.mLateralGrip) * input.mHandbrake);
            const btScalar lateralAcceleration
                = -std::copysign(std::min<btScalar>(std::abs(lateralSpeed) / dt,
                                     std::abs(lateralSpeed) * grip),
                    lateralSpeed);
            body->applyCentralForce(right * (mMass * lateralAcceleration * traction));
        }

        // OpenMW actor yaw increases around -Z, while Bullet torque uses +Z.
        // Negate the bicycle-model yaw rate so negative steering (A/left)
        // turns the vehicle left in OpenMW world space.
        const btScalar targetYawRate
            = -forwardSpeed / mConfig.mWheelbase * std::tan(steeringAngle);
        const btVector3 localAngularVelocity = basis.transpose() * body->getAngularVelocity();
        const btScalar yawAcceleration = (targetYawRate - localAngularVelocity.z()) * 8.f * traction;
        const btScalar inverseYawInertia = body->getInvInertiaDiagLocal().z();
        if (inverseYawInertia > SIMD_EPSILON)
            body->applyTorque(basis * btVector3(0, 0, yawAcceleration / inverseYawInertia));
    }

    void VehicleBody::captureState()
    {
        const btRigidBody* body = getRigidBody();
        const btTransform& transform = body->getWorldTransform();
        const osg::Quat orientation = Misc::Convert::toOsg(transform.getRotation());

        VehicleBodyState state;
        state.mOrientation = orientation;
        state.mRootPosition
            = Misc::Convert::toOsg(transform.getOrigin()) - orientation * mScaledCenterOfMassFromRoot;
        state.mLinearVelocity = Misc::Convert::toOsg(body->getLinearVelocity());
        state.mAngularVelocity = Misc::Convert::toOsg(body->getAngularVelocity());
        state.mSteeringAngle = mSteeringAngle;
        state.mSuspensionCompression = mSuspensionCompression;
        state.mSuspensionLength = mSuspensionLength;
        state.mWheelGrounded = mWheelGrounded;

        std::scoped_lock lock(mStateMutex);
        mState = state;
    }
}
