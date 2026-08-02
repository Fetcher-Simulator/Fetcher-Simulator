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
            || !std::isfinite(config.mChassisUpperInsetX)
            || !std::isfinite(config.mChassisUpperInsetY)
            || !std::isfinite(config.mChassisUpperChamferHeight)
            || config.mChassisLowerInsetX < 0.f || config.mChassisLowerInsetY < 0.f
            || config.mChassisLowerChamferHeight < 0.f
            || config.mChassisUpperInsetX < 0.f || config.mChassisUpperInsetY < 0.f
            || config.mChassisUpperChamferHeight < 0.f
            || config.mChassisLowerInsetX >= config.mCollisionHalfExtents.x()
            || config.mChassisLowerInsetY >= config.mCollisionHalfExtents.y()
            || config.mChassisUpperInsetX >= config.mCollisionHalfExtents.x()
            || config.mChassisUpperInsetY >= config.mCollisionHalfExtents.y()
            || config.mChassisLowerChamferHeight > config.mCollisionHalfExtents.z() * 2.f
            || config.mChassisUpperChamferHeight > config.mCollisionHalfExtents.z() * 2.f
            || config.mChassisLowerChamferHeight + config.mChassisUpperChamferHeight
                > config.mCollisionHalfExtents.z() * 2.f
            || config.mWheelRadius <= 0.f
            || !std::isfinite(config.mWheelRadius) || config.mSuspensionRestLength < 0.f
            || !std::isfinite(config.mSuspensionRestLength) || config.mSuspensionMaxCompression < 0.f
            || !std::isfinite(config.mSuspensionMaxCompression) || config.mSuspensionMaxDroop < 0.f
            || !std::isfinite(config.mSuspensionMaxDroop) || config.mSuspensionSpringRate < 0.f
            || !std::isfinite(config.mSuspensionSpringRate) || config.mSuspensionDampingRate < 0.f
            || !std::isfinite(config.mSuspensionDampingRate)
            || config.mMaximumSupportSlopeDegrees <= 0.f
            || config.mMaximumSupportSlopeDegrees >= 89.f
            || !std::isfinite(config.mMaximumSupportSlopeDegrees)
            || config.mWheelbase <= 0.f || !std::isfinite(config.mWheelbase)
            || config.mMaxForwardSpeed <= 0.f || !std::isfinite(config.mMaxForwardSpeed)
            || config.mMaxReverseSpeed <= 0.f || !std::isfinite(config.mMaxReverseSpeed)
            || config.mParkingBrakeCaptureSpeed < 0.f || !std::isfinite(config.mParkingBrakeCaptureSpeed)
            || config.mParkingBrakeMaxSlopeDegrees <= 0.f
            || config.mParkingBrakeMaxSlopeDegrees >= 89.f
            || !std::isfinite(config.mParkingBrakeMaxSlopeDegrees)
            || config.mStaticLateralFriction < 0.f
            || !std::isfinite(config.mStaticLateralFriction)
            || config.mStaticLateralCaptureSpeed <= 0.f
            || !std::isfinite(config.mStaticLateralCaptureSpeed)
            || config.mHandbrakeSlipStartSpeed < 0.f || !std::isfinite(config.mHandbrakeSlipStartSpeed)
            || config.mHandbrakeSlipFullSpeed <= config.mHandbrakeSlipStartSpeed
            || !std::isfinite(config.mHandbrakeSlipFullSpeed)
            || !std::all_of(config.mWheelMountPositions.begin(), config.mWheelMountPositions.end(), isFinite))
        {
            throw std::invalid_argument("Invalid vehicle rigid-body configuration");
        }

        osg::Quat orientation;
        if (const SceneUtil::PositionAttitudeTransform* baseNode = ptr.getRefData().getBaseNode())
            orientation = baseNode->getAttitude();
        else
        {
            orientation = osg::Quat(
                ptr.getRefData().getPosition().rot[2], osg::Vec3f(0.f, 0.f, -1.f));
        }

        // Vehicle dimensions are profile-owned world units. Never inherit the
        // driver's race scale: doing so changes chassis size, suspension travel,
        // wheelbase, mass distribution, and multiplayer collision by race.
        constexpr float scale = 1.f;
        const osg::Vec3f halfExtents = config.mCollisionHalfExtents;
        const osg::Vec3f collisionCenter = config.mCollisionCenterFromRoot;
        mScaledCenterOfMassFromRoot = config.mCenterOfMassFromRoot;
        mScaledWheelRadius = config.mWheelRadius;
        mScaledSuspensionRestLength = config.mSuspensionRestLength;
        mScaledSuspensionMaxCompression = config.mSuspensionMaxCompression;
        mScaledSuspensionMaxDroop = config.mSuspensionMaxDroop;
        mSuspensionSpringRate = config.mSuspensionSpringRate;
        mSuspensionDampingRate = config.mSuspensionDampingRate;
        mMass = config.mMass;
        for (std::size_t index = 0; index < mScaledWheelMounts.size(); ++index)
        {
            mScaledWheelMounts[index] = config.mWheelMountPositions[index] * scale;
            mSuspensionLength[index] = mScaledSuspensionRestLength + mScaledSuspensionMaxDroop;
        }

        // A full rectangular slab catches terrain seams and arrests rollovers
        // abruptly on its roof edges. Chamfer both lower and upper footprints so
        // underside contacts ramp away cleanly and roof/bed contacts transition
        // into natural continued rolling instead of a hard box-corner stop.
        const float lowerHalfX = halfExtents.x() - config.mChassisLowerInsetX * scale;
        const float lowerHalfY = halfExtents.y() - config.mChassisLowerInsetY * scale;
        const float upperHalfX = halfExtents.x() - config.mChassisUpperInsetX * scale;
        const float upperHalfY = halfExtents.y() - config.mChassisUpperInsetY * scale;
        const float lowerShoulderZ = -halfExtents.z()
            + config.mChassisLowerChamferHeight * scale;
        const float upperShoulderZ = halfExtents.z()
            - config.mChassisUpperChamferHeight * scale;
        mChassisShape = std::make_unique<btConvexHullShape>();
        auto addRectangle = [&](float halfX, float halfY, float z) {
            mChassisShape->addPoint(btVector3(-halfX, -halfY, z), false);
            mChassisShape->addPoint(btVector3(halfX, -halfY, z), false);
            mChassisShape->addPoint(btVector3(halfX, halfY, z), false);
            mChassisShape->addPoint(btVector3(-halfX, halfY, z), false);
        };
        addRectangle(upperHalfX, upperHalfY, halfExtents.z());
        addRectangle(halfExtents.x(), halfExtents.y(), upperShoulderZ);
        addRectangle(halfExtents.x(), halfExtents.y(), lowerShoulderZ);
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
        // Keep low-energy roof/side contacts active long enough to finish a
        // natural roll instead of freezing the chassis as soon as angular speed
        // dips briefly after an impact.
        body->setSleepingThresholds(2.f, 0.02f);

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
        const btVector3 gravity = world->getGravity();
        const btScalar gravityMagnitude = gravity.length();
        const btVector3 gravityUp = gravityMagnitude > SIMD_EPSILON
            ? -gravity / gravityMagnitude : btVector3(0, 0, 1);
        const btScalar minimumSupportNormalAlignment = std::cos(
            mConfig.mMaximumSupportSlopeDegrees * static_cast<btScalar>(osg::PI / 180.0));
        const btScalar gravityAcceleration = Constants::GravityConst * Constants::UnitsPerMeter;
        const btScalar maxSuspensionForce = quarterMass * gravityAcceleration * 3.f;
        const btScalar maxBumpStopForce = quarterMass * gravityAcceleration * 6.f;
        unsigned int groundedWheels = 0;
        std::array<btScalar, 4> wheelSupportFactors{};
        btVector3 contactNormalSum(0, 0, 0);
        mStaticLateralFrictionUsage = 0.f;

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
            const btScalar suspensionAlignment = hitNormal.dot(upDirection);
            const btScalar gravityAlignment = hitNormal.dot(gravityUp);
            if (suspensionAlignment <= 0.1f
                || gravityAlignment < minimumSupportNormalAlignment)
            {
                continue;
            }

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
            const btScalar bumpStopRate = mSuspensionSpringRate * btScalar(5.f);
            const btScalar supportAcceleration
                = mSuspensionSpringRate * compression + mSuspensionDampingRate * compressionVelocity
                + bumpStopRate * bumpStopOvertravel;
            const btScalar supportForceLimit
                = bumpStopOvertravel > 0.f ? maxBumpStopForce : maxSuspensionForce;
            // A sphere sweep can still find the road while the truck is already
            // leaning onto its side. Fade suspension and tire authority by the
            // wheel-axis/contact-normal alignment so those contacts do not act as
            // a powerful artificial self-righting jack.
            const btScalar clampedSuspensionAlignment
                = std::clamp(suspensionAlignment, btScalar(0), btScalar(1));
            const btScalar supportGeometryFactor
                = clampedSuspensionAlignment * clampedSuspensionAlignment;
            const btScalar supportForce
                = std::clamp(quarterMass * supportAcceleration, btScalar(0), supportForceLimit)
                * supportGeometryFactor;

            mWheelGrounded[index] = true;
            wheelSupportFactors[index] = supportGeometryFactor;
            ++groundedWheels;
            contactNormalSum += hitNormal * supportGeometryFactor;
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
        {
            mParkingBrakeHolding = false;
            mHandbrakeSlipFactor = 0.f;
            mGroundSlopeDegrees = 0.f;
            return;
        }

        const btScalar traction = (wheelSupportFactors[0] + wheelSupportFactors[1]
            + wheelSupportFactors[2] + wheelSupportFactors[3]) * 0.25f;
        btVector3 averageContactNormal = contactNormalSum;
        if (averageContactNormal.length2() > SIMD_EPSILON)
            averageContactNormal.normalize();
        else
            averageContactNormal = upDirection;

        const btVector3 forward = basis * btVector3(0, 1, 0);
        const btVector3 right = basis * btVector3(1, 0, 0);
        btVector3 surfaceForward
            = forward - averageContactNormal * forward.dot(averageContactNormal);
        if (surfaceForward.length2() > SIMD_EPSILON)
            surfaceForward.normalize();
        else
            surfaceForward = forward;
        btVector3 surfaceRight = surfaceForward.cross(averageContactNormal);
        if (surfaceRight.length2() > SIMD_EPSILON)
            surfaceRight.normalize();
        else
            surfaceRight = right;
        const btVector3 tangentVelocity
            = linearVelocity - averageContactNormal * linearVelocity.dot(averageContactNormal);
        const btScalar tangentSpeed = tangentVelocity.length();
        const btScalar slopeCosine
            = std::clamp(averageContactNormal.dot(gravityUp), btScalar(0), btScalar(1));
        const btScalar slopeDegrees = std::acos(slopeCosine)
            * static_cast<btScalar>(180.0 / osg::PI);
        const btScalar maximumParkingSlopeRadians
            = mConfig.mParkingBrakeMaxSlopeDegrees * static_cast<btScalar>(osg::PI / 180.0);
        const bool rearWheelGrounded = mWheelGrounded[2] || mWheelGrounded[3];
        const bool parkingBrakeRequested = input.mHandbrake > 0.001f
            && input.mThrottle <= 0.001f && input.mBrake <= 0.001f;
        const bool parkingSlopeHoldable
            = slopeDegrees <= mConfig.mParkingBrakeMaxSlopeDegrees;

        if (!parkingBrakeRequested || !rearWheelGrounded || !parkingSlopeHoldable)
            mParkingBrakeHolding = false;
        else if (!mParkingBrakeHolding
            && tangentSpeed <= mConfig.mParkingBrakeCaptureSpeed)
            mParkingBrakeHolding = true;

        const bool parkingBrakeHold = mParkingBrakeHolding;
        const btScalar slipRange = std::max<btScalar>(
            mConfig.mHandbrakeSlipFullSpeed - mConfig.mHandbrakeSlipStartSpeed, 0.001f);
        btScalar handbrakeSpeedFactor = std::clamp(
            (std::abs(forwardSpeed) - mConfig.mHandbrakeSlipStartSpeed) / slipRange,
            btScalar(0), btScalar(1));
        handbrakeSpeedFactor
            = handbrakeSpeedFactor * handbrakeSpeedFactor * (3.f - 2.f * handbrakeSpeedFactor);
        const btScalar handbrakeSlipFactor
            = parkingBrakeHold ? 0.f : input.mHandbrake * handbrakeSpeedFactor;
        mHandbrakeSlipFactor = static_cast<float>(handbrakeSlipFactor);
        mGroundSlopeDegrees = static_cast<float>(slopeDegrees);

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
            // At road speed the handbrake should lock the rear axle and start a
            // slide, not erase nearly the entire vehicle velocity in one second.
            // Preserve stronger braking as speed falls so it still brings the
            // truck to a stop after the drift.
            btScalar maximumBraking = mConfig.mHandbrakeStrength * input.mHandbrake
                * (1.f - 0.6f * handbrakeSpeedFactor);
            const bool overLimitParkingAttempt = parkingBrakeRequested
                && !parkingSlopeHoldable
                && tangentSpeed <= mConfig.mParkingBrakeCaptureSpeed;
            if (overLimitParkingAttempt)
            {
                const btVector3 gravityTangent
                    = gravity - averageContactNormal * gravity.dot(averageContactNormal);
                maximumBraking = std::min<btScalar>(
                    maximumBraking, gravityTangent.length() * 0.85f);
            }
            const btScalar braking
                = std::min<btScalar>(maximumBraking, std::abs(forwardSpeed) / dt);
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

        body->applyCentralForce(surfaceForward * (mMass * longitudinalAcceleration * traction));

        if (parkingBrakeHold)
        {
            // Model the parking brake as static tire friction. It latches only
            // after the truck is nearly stopped, cancels surface gravity and
            // residual tangent velocity, and is capped by the configured hill
            // angle so extremely steep terrain still wins.
            const btScalar maximumHoldAcceleration = gravityMagnitude * slopeCosine
                * std::tan(maximumParkingSlopeRadians) * input.mHandbrake;
            btVector3 holdAcceleration
                = -(gravity - averageContactNormal * gravity.dot(averageContactNormal));
            if (tangentSpeed > 0.001f)
                holdAcceleration -= tangentVelocity / dt;

            const btScalar holdAcceleration2 = holdAcceleration.length2();
            if (maximumHoldAcceleration > 0.f
                && holdAcceleration2 > maximumHoldAcceleration * maximumHoldAcceleration)
            {
                holdAcceleration *= maximumHoldAcceleration / std::sqrt(holdAcceleration2);
            }
            body->applyCentralForce(holdAcceleration * mMass);
        }
        else
        {
            // Equal front/rear grip must not create a yaw moment. Distribute
            // lateral force according to axle distance from the center of mass;
            // then reduce only rear grip under a speed-dependent handbrake.
            const btScalar frontAxleY
                = (mScaledWheelMounts[0].y() + mScaledWheelMounts[1].y()) * 0.5f
                - mScaledCenterOfMassFromRoot.y();
            const btScalar rearAxleY
                = (mScaledWheelMounts[2].y() + mScaledWheelMounts[3].y()) * 0.5f
                - mScaledCenterOfMassFromRoot.y();
            const btScalar frontContactZ
                = ((mScaledWheelMounts[0].z() - mScaledCenterOfMassFromRoot.z()
                       - mSuspensionLength[0] - mScaledWheelRadius)
                      + (mScaledWheelMounts[1].z() - mScaledCenterOfMassFromRoot.z()
                          - mSuspensionLength[1] - mScaledWheelRadius))
                * 0.5f;
            const btScalar rearContactZ
                = ((mScaledWheelMounts[2].z() - mScaledCenterOfMassFromRoot.z()
                       - mSuspensionLength[2] - mScaledWheelRadius)
                      + (mScaledWheelMounts[3].z() - mScaledCenterOfMassFromRoot.z()
                          - mSuspensionLength[3] - mScaledWheelRadius))
                * 0.5f;
            const btScalar frontDistance = std::max(frontAxleY, btScalar(0));
            const btScalar rearDistance = std::max(-rearAxleY, btScalar(0));
            const btScalar axleDistanceSum
                = std::max(frontDistance + rearDistance, btScalar(0.001f));
            const btScalar frontMassFraction = rearDistance / axleDistanceSum;
            const btScalar rearMassFraction = frontDistance / axleDistanceSum;
            const btScalar frontGrounding
                = (wheelSupportFactors[0] + wheelSupportFactors[1]) * 0.5f;
            const btScalar rearGrounding
                = (wheelSupportFactors[2] + wheelSupportFactors[3]) * 0.5f;
            const btScalar frontGrip = std::max<btScalar>(mConfig.mLateralGrip, 0.f);
            const btScalar rearGrip = std::max<btScalar>(0.f,
                mConfig.mLateralGrip
                    + (mConfig.mHandbrakeLateralGrip - mConfig.mLateralGrip)
                        * handbrakeSlipFactor);
            const btScalar normalAcceleration = gravityMagnitude * slopeCosine;
            const btScalar staticFriction
                = std::max<btScalar>(mConfig.mStaticLateralFriction, 0.f);
            const btScalar dynamicFriction = staticFriction * 0.9f;
            // Preserve front steering authority during a drift, but cap the
            // front tire's peak lateral force as the rear locks. Full front
            // friction plus near-zero rear friction produced the desired yaw
            // and an excessive rollover moment at the same time.
            const btScalar frontDynamicFriction = dynamicFriction
                * (1.f - 0.28f * handbrakeSlipFactor);
            const btScalar rearDynamicFriction = dynamicFriction
                * (1.f - 0.92f * handbrakeSlipFactor);
            const btScalar staticCaptureSpeed
                = std::max<btScalar>(mConfig.mStaticLateralCaptureSpeed, 0.001f);
            const btScalar centerLateralSpeed = tangentVelocity.dot(surfaceRight);
            const btScalar normalizedStaticSpeed = std::clamp(
                tangentSpeed / staticCaptureSpeed, btScalar(0), btScalar(1));
            const btScalar staticBlend = 1.f
                - normalizedStaticSpeed * normalizedStaticSpeed
                    * (3.f - 2.f * normalizedStaticSpeed);
            // Three well-supported tires can still carry essentially the full
            // normal load. Derive hold authority from actual suspension support
            // instead of cutting all three-wheel cases to an arbitrary 50%.
            const btScalar staticSupportFactor
                = std::clamp(traction * (4.f / 3.f), btScalar(0), btScalar(1));
            const btScalar handbrakeStaticFade
                = 1.f - 0.95f * handbrakeSlipFactor;
            const btScalar maximumStaticAcceleration
                = normalAcceleration * staticFriction * staticSupportFactor;
            const btScalar requiredStaticAcceleration
                = -gravity.dot(surfaceRight) - centerLateralSpeed / dt;
            const btScalar staticLateralAcceleration = std::clamp(
                requiredStaticAcceleration, -maximumStaticAcceleration,
                maximumStaticAcceleration);
            if (maximumStaticAcceleration > SIMD_EPSILON)
            {
                mStaticLateralFrictionUsage = static_cast<float>(std::clamp(
                    staticBlend * handbrakeStaticFade
                        * std::abs(requiredStaticAcceleration) / maximumStaticAcceleration,
                    btScalar(0), btScalar(2)));
            }
            body->applyCentralForce(surfaceRight
                * (mMass * staticLateralAcceleration * staticBlend * handbrakeStaticFade));

            auto applyAxleLateralForce = [&](btScalar axleY, btScalar contactZ,
                                             btScalar massFraction, btScalar grounding,
                                             btScalar grip, btScalar frictionCoefficient)
            {
                if (grounding <= 0.f)
                    return;

                // Apply moving tire force at the approximate contact-patch height
                // so cornering still produces body roll. Cap it by a physical
                // friction coefficient; the old velocity-only correction could
                // generate an arbitrarily large one-frame lateral impulse and trip
                // the truck instead of allowing the tires to slide.
                const btVector3 relativePosition = basis * btVector3(0, axleY, contactZ);
                const btScalar axleLateralSpeed
                    = body->getVelocityInLocalPoint(relativePosition).dot(surfaceRight);
                const btScalar requestedLateralAcceleration
                    = -std::copysign(std::min<btScalar>(std::abs(axleLateralSpeed) / dt,
                                         std::abs(axleLateralSpeed) * grip),
                        axleLateralSpeed);
                const btScalar maximumDynamicAcceleration
                    = normalAcceleration * std::max<btScalar>(frictionCoefficient, 0.f);
                const btScalar dynamicLateralAcceleration = std::clamp(
                    requestedLateralAcceleration, -maximumDynamicAcceleration,
                    maximumDynamicAcceleration)
                    * (1.f - staticBlend * handbrakeStaticFade);
                body->applyForce(surfaceRight
                        * (mMass * massFraction * grounding * dynamicLateralAcceleration),
                    relativePosition);
            };

            applyAxleLateralForce(frontAxleY, frontContactZ, frontMassFraction,
                frontGrounding, frontGrip, frontDynamicFriction);
            applyAxleLateralForce(rearAxleY, rearContactZ, rearMassFraction,
                rearGrounding, rearGrip, rearDynamicFriction);
        }

        // OpenMW actor yaw increases around -Z, while Bullet torque uses +Z.
        // Negate the bicycle-model yaw rate so negative steering (A/left)
        // turns the vehicle left in OpenMW world space.
        const btScalar targetYawRate
            = -forwardSpeed / mConfig.mWheelbase * std::tan(steeringAngle);
        const btVector3 localAngularVelocity = basis.transpose() * body->getAngularVelocity();
        const btScalar roadAlignment
            = std::clamp(averageContactNormal.dot(upDirection), btScalar(0), btScalar(1));
        const btScalar yawSupportFactor = roadAlignment * roadAlignment;
        const btScalar yawControlFactor = 1.f - 0.95f * handbrakeSlipFactor;
        const btScalar yawAcceleration = (targetYawRate - localAngularVelocity.z())
            * 8.f * yawControlFactor * yawSupportFactor * traction;
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
        state.mHandbrakeSlipFactor = mHandbrakeSlipFactor;
        state.mStaticLateralFrictionUsage = mStaticLateralFrictionUsage;
        state.mGroundSlopeDegrees = mGroundSlopeDegrees;
        state.mParkingBrakeHolding = mParkingBrakeHolding;
        state.mSuspensionCompression = mSuspensionCompression;
        state.mSuspensionLength = mSuspensionLength;
        state.mWheelGrounded = mWheelGrounded;

        std::scoped_lock lock(mStateMutex);
        mState = state;
    }
}
