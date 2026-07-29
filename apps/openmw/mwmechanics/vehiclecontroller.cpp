#include "vehiclecontroller.hpp"

#include <algorithm>
#include <cmath>

#include <osg/Math>
#include <osg/Quat>
#include <osg/Vec3f>

#include <components/debug/debuglog.hpp>
#include <components/openmw-mp/Base/VehicleProfiles.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/player.hpp"
#include "../mwworld/ptr.hpp"

namespace
{
    constexpr float sMaxSolverDuration = 0.1f;
    constexpr float sSolverStep = 1.f / 60.f;
    constexpr float sMotionSampleTimeout = 0.1f;
    constexpr float sMotionSampleDistance2 = 0.01f;
    constexpr float sRestSpeedThreshold = 15.f;

    float moveTowards(float value, float target, float maxDelta)
    {
        if (value < target)
            return std::min(value + maxDelta, target);
        return std::max(value - maxDelta, target);
    }

    void updateMotionFeedback(MWWorld::VehicleRuntimeState& state, const osg::Vec3f& position, float yaw,
        float duration, const mwmp::VehicleHandlingProfile& handling)
    {
        if (!state.mMotionInitialized)
        {
            state.mLastPosition = position;
            state.mMotionInitialized = true;
            return;
        }

        state.mMotionSampleTime += std::max(duration, 0.f);
        const osg::Vec3f displacement = position - state.mLastPosition;
        if (displacement.length2() <= sMotionSampleDistance2 && state.mMotionSampleTime < sMotionSampleTimeout)
            return;

        if (state.mMotionSampleTime > 0.0001f)
        {
            const float maxExpectedDistance
                = handling.maxForwardSpeed * state.mMotionSampleTime * 2.f + handling.wheelbase;
            if (displacement.length2() <= maxExpectedDistance * maxExpectedDistance)
            {
                const osg::Vec3f worldVelocity = displacement / state.mMotionSampleTime;
                const osg::Quat vehicleRotation(yaw, osg::Vec3f(0.f, 0.f, -1.f));
                const osg::Vec3f localVelocity = vehicleRotation.inverse() * worldVelocity;
                state.mForwardSpeed = localVelocity.y();
                state.mLateralSpeed = localVelocity.x();
            }
            else
            {
                state.mForwardSpeed = 0.f;
                state.mLateralSpeed = 0.f;
            }
        }

        state.mLastPosition = position;
        state.mMotionSampleTime = 0.f;
    }

    void integrateLongitudinalSpeed(MWWorld::VehicleRuntimeState& state,
        const mwmp::VehicleHandlingProfile& handling, float step)
    {
        const MWWorld::VehicleInputState& input = state.mInput;
        const bool changingFromForward = input.mBrake > 0.f && state.mForwardSpeed > handling.directionChangeSpeed;
        const bool changingFromReverse = input.mThrottle > 0.f && state.mForwardSpeed < -handling.directionChangeSpeed;

        if (input.mThrottle > 0.f && input.mBrake > 0.f)
        {
            state.mForwardSpeed
                = moveTowards(state.mForwardSpeed, 0.f, handling.serviceBrakeStrength * step);
        }
        else if (changingFromReverse)
        {
            state.mForwardSpeed = moveTowards(
                state.mForwardSpeed, 0.f, handling.serviceBrakeStrength * input.mThrottle * step);
        }
        else if (changingFromForward)
        {
            state.mForwardSpeed
                = moveTowards(state.mForwardSpeed, 0.f, handling.serviceBrakeStrength * input.mBrake * step);
        }
        else
        {
            state.mForwardSpeed += handling.engineAcceleration * input.mThrottle * step;
            state.mForwardSpeed -= handling.reverseAcceleration * input.mBrake * step;
        }

        if (input.mThrottle == 0.f && input.mBrake == 0.f)
        {
            state.mForwardSpeed
                = moveTowards(state.mForwardSpeed, 0.f, handling.rollingResistance * step);
        }

        const float drag = handling.aerodynamicDrag * state.mForwardSpeed * state.mForwardSpeed * step;
        state.mForwardSpeed = moveTowards(state.mForwardSpeed, 0.f, drag);
        state.mForwardSpeed
            = moveTowards(state.mForwardSpeed, 0.f, handling.handbrakeStrength * input.mHandbrake * step);
        state.mForwardSpeed
            = std::clamp(state.mForwardSpeed, -handling.maxReverseSpeed, handling.maxForwardSpeed);
    }

    float integrateSteering(MWWorld::VehicleRuntimeState& state, const mwmp::VehicleHandlingProfile& handling,
        float effectiveWheelbase, float step)
    {
        const float normalizedSpeed = std::clamp(
            std::abs(state.mForwardSpeed) / std::max(handling.maxForwardSpeed, 1.f), 0.f, 1.f);
        const float steeringLimitDegrees = handling.lowSpeedSteeringDegrees
            + (handling.highSpeedSteeringDegrees - handling.lowSpeedSteeringDegrees) * normalizedSpeed;
        const float steeringTarget
            = state.mInput.mSteering * osg::DegreesToRadians(steeringLimitDegrees);
        const float responseDegrees = std::abs(steeringTarget) < std::abs(state.mSteeringAngle)
            ? handling.steeringReturnDegrees
            : handling.steeringResponseDegrees;
        state.mSteeringAngle = moveTowards(
            state.mSteeringAngle, steeringTarget, osg::DegreesToRadians(responseDegrees) * step);

        if (std::abs(state.mForwardSpeed) <= 0.5f || effectiveWheelbase <= 0.f)
        {
            state.mYawRate = 0.f;
            return 0.f;
        }

        state.mYawRate = state.mForwardSpeed / effectiveWheelbase * std::tan(state.mSteeringAngle);
        return state.mYawRate * step;
    }
}

namespace MWMechanics
{
    void updateLocalVehicle(const MWWorld::Ptr& player, float duration)
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Player& playerState = world->getPlayer();
        MWWorld::VehicleRuntimeState& state = playerState.getVehicleRuntimeState();
        if (state.mMode != MWWorld::VehicleModeState::Active)
            return;

        const mwmp::VehicleProfile* profile = mwmp::findVehicleProfile(state.mProfileId);
        if (!profile)
        {
            world->queueMovement(player, osg::Vec3f());
            return;
        }

        const ESM::Position& refPosition = player.getRefData().getPosition();
        const osg::Vec3f position = refPosition.asVec3();
        const float yaw = refPosition.rot[2];
        updateMotionFeedback(state, position, yaw, duration, profile->handling);
        state.mGrounded = world->isOnGround(player);

        float scale = 1.f;
        if (const SceneUtil::PositionAttitudeTransform* baseNode = player.getRefData().getBaseNode())
            scale = baseNode->getScale().y();
        const float effectiveWheelbase = profile->handling.wheelbase * scale;

        float remaining = std::clamp(duration, 0.f, sMaxSolverDuration);
        float yawDelta = 0.f;
        while (remaining > 0.f)
        {
            const float step = std::min(remaining, sSolverStep);
            integrateLongitudinalSpeed(state, profile->handling, step);
            yawDelta += integrateSteering(state, profile->handling, effectiveWheelbase, step);

            const float grip = state.mInput.mHandbrake > 0.f ? profile->handling.handbrakeLateralGrip
                                                             : profile->handling.lateralGrip;
            state.mLateralSpeed *= std::exp(-grip * step);
            remaining -= step;
        }

        // Ground-contact correction can appear in displacement feedback as a few
        // units per second of planar motion. Without a rest dead zone that noise
        // is re-queued indefinitely and makes a parked vehicle creep.
        if (state.mInput.mThrottle == 0.f && state.mInput.mBrake == 0.f
            && std::abs(state.mForwardSpeed) < sRestSpeedThreshold
            && std::abs(state.mLateralSpeed) < sRestSpeedThreshold)
        {
            state.mForwardSpeed = 0.f;
            state.mLateralSpeed = 0.f;
        }

        if (yawDelta != 0.f)
            world->rotateObject(player, osg::Vec3f(0.f, 0.f, yawDelta), true);
        world->queueMovement(player, osg::Vec3f(state.mLateralSpeed, state.mForwardSpeed, 0.f));

        state.mLogTimer += std::max(duration, 0.f);
        if (state.mLogTimer >= 1.f)
        {
            state.mLogTimer = 0.f;
            Log(Debug::Info) << "[Vehicle] solver speed=" << state.mForwardSpeed
                             << " lateral=" << state.mLateralSpeed
                             << " steeringDeg=" << osg::RadiansToDegrees(state.mSteeringAngle)
                             << " yawRate=" << state.mYawRate << " grounded=" << state.mGrounded
                             << " input=(" << state.mInput.mThrottle << ", " << state.mInput.mBrake << ", "
                             << state.mInput.mSteering << ", " << state.mInput.mHandbrake << ")";
        }
    }
}
