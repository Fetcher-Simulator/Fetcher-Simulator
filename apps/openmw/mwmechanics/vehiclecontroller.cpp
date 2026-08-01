#include "vehiclecontroller.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <osg/Math>
#include <osg/Quat>
#include <osg/Vec3f>

#include <components/debug/debuglog.hpp>
#include <components/openmw-mp/Base/VehicleProfiles.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>
#include <components/vfs/pathutil.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwphysics/collisiontype.hpp"
#include "../mwphysics/raycasting.hpp"
#include "../mwphysics/vehiclebody.hpp"

#include "../mwrender/animation.hpp"
#include "../mwsound/sound.hpp"

#include "../mwworld/player.hpp"
#include "../mwworld/ptr.hpp"

namespace
{
    constexpr float sMaxSolverDuration = 0.1f;
    constexpr float sSolverStep = 1.f / 60.f;
    constexpr float sMotionSampleTimeout = 0.1f;
    constexpr float sMotionSampleDistance2 = 0.01f;
    constexpr float sRestSpeedThreshold = 15.f;
    constexpr float sTakeoffSupportLossHold = 0.05f;
    constexpr float sLandingSupportHold = 0.05f;
    constexpr float sTakeoffDirectionSpeed = 5.f;
    constexpr float sGroundPitchVelocityResponse = 8.f;
    constexpr float sAirbornePitchAccelerationBase = 20.f;
    constexpr float sAirbornePitchAccelerationSpeed = 55.f;
    constexpr float sMotionFeedbackImpulseFraction = 0.02f;
    constexpr float sVisualSuspensionResponse = 18.f;
    constexpr std::string_view sVehicleVisualEffectId = "mwmp_vehicle_visual";

    MWMechanics::VehicleSuspensionState sLocalSuspensionState;

    struct LocalVehicleAudioState
    {
        std::array<float, 4> mPreviousSuspensionTravel{};
        MWBase::Sound* mEngineSound = nullptr;
        MWBase::Sound* mTireSound = nullptr;
        float mSkidCooldown = 0.f;
        float mSuspensionImpactCooldown = 0.f;
        bool mInitialized = false;
    };

    LocalVehicleAudioState sLocalVehicleAudioState;

    float moveTowards(float value, float target, float maxDelta)
    {
        if (value < target)
            return std::min(value + maxDelta, target);
        return std::max(value - maxDelta, target);
    }

    float updateVisualSteeringAngle(float current, float target,
        const mwmp::VehicleHandlingProfile& handling, float duration)
    {
        const bool increasingAwayFromCenter
            = current * target >= 0.f && std::abs(target) > std::abs(current);
        const float rateDegrees = increasingAwayFromCenter
            ? handling.steeringResponseDegrees
            : handling.steeringReturnDegrees;
        const float maxDelta = osg::DegreesToRadians(rateDegrees) * std::max(duration, 0.f);
        return moveTowards(current, target, maxDelta);
    }

    float limitFeedbackMagnitudeIncrease(float measured, float current, float maxIncrease)
    {
        if (measured * current <= 0.f)
            return std::clamp(measured, -maxIncrease, maxIncrease);
        if (std::abs(measured) <= std::abs(current))
            return measured;
        return std::copysign(std::min(std::abs(measured), std::abs(current) + maxIncrease), measured);
    }

    void stopVehicleAudio()
    {
        MWBase::SoundManager* soundManager = MWBase::Environment::get().getSoundManager();
        if (soundManager)
        {
            if (sLocalVehicleAudioState.mEngineSound)
                soundManager->stopSound(sLocalVehicleAudioState.mEngineSound);
            if (sLocalVehicleAudioState.mTireSound)
                soundManager->stopSound(sLocalVehicleAudioState.mTireSound);
        }
        sLocalVehicleAudioState = {};
    }

    void updateVehicleAudio(const MWWorld::Ptr& player, MWWorld::VehicleRuntimeState& state,
        const mwmp::VehicleProfile& profile, const MWPhysics::VehicleBodyState& bodyState,
        const std::array<float, 4>& suspensionTravel, float duration)
    {
        MWBase::SoundManager* soundManager = MWBase::Environment::get().getSoundManager();
        if (!soundManager)
            return;

        const osg::Vec3f localVelocity = bodyState.mOrientation.inverse() * bodyState.mLinearVelocity;
        const float forwardSpeed = std::abs(localVelocity.y());
        const float lateralSpeed = std::abs(localVelocity.x());
        const float speedRatio = std::clamp(
            forwardSpeed / std::max(profile.handling.maxForwardSpeed, 1.f), 0.f, 1.f);
        const float engineLoad = std::clamp(
            std::max(state.mInput.mThrottle, state.mInput.mBrake), 0.f, 1.f);
        const float engineResponse = std::clamp(speedRatio * 0.65f + engineLoad * 0.35f, 0.f, 1.f);
        const float enginePitch = profile.audio.engineIdlePitch
            + (profile.audio.engineMaximumPitch - profile.audio.engineIdlePitch) * engineResponse;
        const float engineVolume = profile.audio.engineIdleVolume
            + profile.audio.engineLoadVolume * std::clamp(engineLoad * 0.75f + speedRatio * 0.25f, 0.f, 1.f);

        if (!sLocalVehicleAudioState.mEngineSound && !profile.audio.engineLoop.empty())
        {
            sLocalVehicleAudioState.mEngineSound = soundManager->playSound3D(player,
                VFS::Path::Normalized(profile.audio.engineLoop), engineVolume, enginePitch,
                MWSound::Type::Sfx, MWSound::PlayMode::Loop);
        }
        if (sLocalVehicleAudioState.mEngineSound)
        {
            sLocalVehicleAudioState.mEngineSound->setVolume(engineVolume);
            sLocalVehicleAudioState.mEngineSound->setPitch(enginePitch);
            sLocalVehicleAudioState.mEngineSound->setVelocity(bodyState.mLinearVelocity);
        }

        const float tireAudibility = std::clamp((forwardSpeed - 20.f) / 520.f, 0.f, 1.f);
        const float tirePitch = profile.audio.tireMinimumPitch
            + (profile.audio.tireMaximumPitch - profile.audio.tireMinimumPitch) * speedRatio;
        const float tireVolume = profile.audio.tireMaximumVolume * tireAudibility;
        if (!sLocalVehicleAudioState.mTireSound && !profile.audio.tireRollLoop.empty())
        {
            sLocalVehicleAudioState.mTireSound = soundManager->playSound3D(player,
                VFS::Path::Normalized(profile.audio.tireRollLoop), tireVolume, tirePitch,
                MWSound::Type::Sfx, MWSound::PlayMode::Loop);
        }
        if (sLocalVehicleAudioState.mTireSound)
        {
            sLocalVehicleAudioState.mTireSound->setVolume(tireVolume);
            sLocalVehicleAudioState.mTireSound->setPitch(tirePitch);
            sLocalVehicleAudioState.mTireSound->setVelocity(bodyState.mLinearVelocity);
        }

        const float safeDt = std::max(duration, 0.f);
        sLocalVehicleAudioState.mSkidCooldown = std::max(sLocalVehicleAudioState.mSkidCooldown - safeDt, 0.f);
        sLocalVehicleAudioState.mSuspensionImpactCooldown = std::max(sLocalVehicleAudioState.mSuspensionImpactCooldown - safeDt, 0.f);

        const float skidIntensity = std::clamp(
            (lateralSpeed - 55.f) / 260.f
                + state.mInput.mHandbrake * std::clamp(forwardSpeed / 300.f, 0.f, 1.f),
            0.f, 1.f);
        if (skidIntensity > 0.22f && sLocalVehicleAudioState.mSkidCooldown <= 0.f
            && !profile.audio.skidSound.empty())
        {
            soundManager->playSound3D(player, VFS::Path::Normalized(profile.audio.skidSound),
                profile.audio.skidVolume * skidIntensity, 0.85f + skidIntensity * 0.25f);
            sLocalVehicleAudioState.mSkidCooldown = 0.32f;
        }

        if (!sLocalVehicleAudioState.mInitialized)
        {
            sLocalVehicleAudioState.mPreviousSuspensionTravel = suspensionTravel;
            sLocalVehicleAudioState.mInitialized = true;
            return;
        }

        float maximumCompressionSpeed = 0.f;
        const float derivativeDt = std::max(safeDt, 0.001f);
        for (std::size_t index = 0; index < suspensionTravel.size(); ++index)
        {
            maximumCompressionSpeed = std::max(maximumCompressionSpeed,
                (suspensionTravel[index] - sLocalVehicleAudioState.mPreviousSuspensionTravel[index]) / derivativeDt);
        }
        sLocalVehicleAudioState.mPreviousSuspensionTravel = suspensionTravel;
        if (maximumCompressionSpeed > 170.f && sLocalVehicleAudioState.mSuspensionImpactCooldown <= 0.f
            && !profile.audio.suspensionImpactSound.empty())
        {
            const float impact = std::clamp((maximumCompressionSpeed - 170.f) / 500.f, 0.f, 1.f);
            soundManager->playSound3D(player,
                VFS::Path::Normalized(profile.audio.suspensionImpactSound),
                profile.audio.suspensionImpactVolume * impact, 0.9f + impact * 0.18f);
            sLocalVehicleAudioState.mSuspensionImpactCooldown = 0.18f;
        }
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
                const float sampleTime = std::min(state.mMotionSampleTime, sMotionSampleTimeout);
                const float controlledAcceleration = std::max(
                    handling.engineAcceleration * state.mInput.mThrottle,
                    handling.reverseAcceleration * state.mInput.mBrake);
                const float impulseAllowance
                    = std::max(handling.maxForwardSpeed * sMotionFeedbackImpulseFraction, 1.f);
                state.mForwardSpeed = limitFeedbackMagnitudeIncrease(localVelocity.y(),
                    state.mForwardSpeed, controlledAcceleration * sampleTime + impulseAllowance);
                state.mLateralSpeed = limitFeedbackMagnitudeIncrease(
                    localVelocity.x(), state.mLateralSpeed, impulseAllowance);
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
        const mwmp::VehicleHandlingProfile& handling, float tractionFactor, float step)
    {
        const MWWorld::VehicleInputState& input = state.mInput;
        const bool changingFromForward = input.mBrake > 0.f && state.mForwardSpeed > handling.directionChangeSpeed;
        const bool changingFromReverse = input.mThrottle > 0.f && state.mForwardSpeed < -handling.directionChangeSpeed;

        if (input.mThrottle > 0.f && input.mBrake > 0.f)
        {
            state.mForwardSpeed
                = moveTowards(state.mForwardSpeed, 0.f, handling.serviceBrakeStrength * tractionFactor * step);
        }
        else if (changingFromReverse)
        {
            state.mForwardSpeed = moveTowards(
                state.mForwardSpeed, 0.f, handling.serviceBrakeStrength * tractionFactor * input.mThrottle * step);
        }
        else if (changingFromForward)
        {
            state.mForwardSpeed
                = moveTowards(state.mForwardSpeed, 0.f,
                    handling.serviceBrakeStrength * tractionFactor * input.mBrake * step);
        }
        else
        {
            state.mForwardSpeed += handling.engineAcceleration * tractionFactor * input.mThrottle * step;
            state.mForwardSpeed -= handling.reverseAcceleration * tractionFactor * input.mBrake * step;
        }

        if (input.mThrottle == 0.f && input.mBrake == 0.f)
        {
            state.mForwardSpeed
                = moveTowards(state.mForwardSpeed, 0.f, handling.rollingResistance * step);
        }

        const float drag = handling.aerodynamicDrag * state.mForwardSpeed * state.mForwardSpeed * step;
        state.mForwardSpeed = moveTowards(state.mForwardSpeed, 0.f, drag);
        state.mForwardSpeed = moveTowards(
            state.mForwardSpeed, 0.f, handling.handbrakeStrength * tractionFactor * input.mHandbrake * step);
        state.mForwardSpeed
            = std::clamp(state.mForwardSpeed, -handling.maxReverseSpeed, handling.maxForwardSpeed);
    }

    float integrateSteering(MWWorld::VehicleRuntimeState& state, const mwmp::VehicleHandlingProfile& handling,
        float effectiveWheelbase, float steeringSupport, float step)
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

        state.mYawRate
            = state.mForwardSpeed / effectiveWheelbase * std::tan(state.mSteeringAngle) * steeringSupport;
        return state.mYawRate * step;
    }
}

namespace MWMechanics
{
    void updateVehicleSuspension(const MWWorld::Ptr& actor, const mwmp::VehicleProfile& profile,
        float duration, float longitudinalSpeed, VehicleSuspensionState& state)
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        const MWPhysics::RayCastingInterface* rayCasting = world->getRayCasting();
        if (!rayCasting || actor.isEmpty())
            return;

        const bool initialize = !state.mInitialized || state.mProfileId != profile.id;
        if (state.mProfileId != profile.id)
        {
            state = {};
            state.mProfileId.assign(profile.id);
        }

        float scale = 1.f;
        if (const SceneUtil::PositionAttitudeTransform* baseNode = actor.getRefData().getBaseNode())
            scale = std::max(baseNode->getScale().y(), 0.01f);

        const mwmp::VehicleSuspensionProfile& suspension = profile.suspension;
        const ESM::Position& refPosition = actor.getRefData().getPosition();
        const osg::Vec3f rootPosition = refPosition.asVec3();
        const osg::Quat yawRotation(refPosition.rot[2], osg::Vec3f(0.f, 0.f, -1.f));
        const osg::Quat probeAttitude(state.mPitch, osg::Vec3f(1.f, 0.f, 0.f),
            state.mRoll, osg::Vec3f(0.f, 1.f, 0.f), 0.f, osg::Vec3f(0.f, 0.f, 1.f));
        const float wheelRadius = suspension.wheelRadius * scale;
        const float restLength = suspension.restLength * scale;
        const float maxCompression = suspension.maxCompression * scale;
        const float maxDroop = suspension.maxDroop * scale;
        const float probeAboveMount = suspension.probeAboveMount * scale;
        const float probeBelowMount = suspension.probeBelowMount * scale;
        const float contactPatchOffset
            = wheelRadius * std::clamp(suspension.tireContactPatchFraction, 0.f, 0.95f);
        const float supportSlack = suspension.supportProbeSlack * scale;
        const float terrainMinimumLength
            = restLength - maxCompression - supportSlack;
        const osg::Vec3f chassisOffset(0.f, 0.f, state.mVerticalOffset);
        osg::Vec3f wheelMounts[4];
        state.mSupportedWheels = 0;

        constexpr int collisionMask = MWPhysics::CollisionType_World
            | MWPhysics::CollisionType_HeightMap | MWPhysics::CollisionType_Door;
        for (std::size_t index = 0; index < state.mWheels.size(); ++index)
        {
            const std::array<float, 3>& mount = suspension.wheels[index].mountPosition;
            const osg::Vec3f localMount(mount[0] * scale, mount[1] * scale, mount[2] * scale);
            wheelMounts[index]
                = rootPosition + chassisOffset + yawRotation * (probeAttitude * localMount);

            VehicleWheelContactState& wheel = state.mWheels[index];
            wheel.mTerrainHit = false;
            wheel.mGrounded = false;
            wheel.mRequiredSuspensionLength = probeBelowMount - wheelRadius;
            wheel.mContactPoint = wheelMounts[index] - osg::Vec3f(0.f, 0.f, probeBelowMount);
            wheel.mContactNormal = osg::Vec3f(0.f, 0.f, 1.f);
            wheel.mSurfaceVelocity = osg::Vec3f();

            float bestEquivalentGround = -std::numeric_limits<float>::max();
            for (float sampleDirection : { -1.f, 0.f, 1.f })
            {
                const float longitudinalOffset = contactPatchOffset * sampleDirection;
                const osg::Vec3f samplePosition
                    = wheelMounts[index]
                    + yawRotation * (probeAttitude * osg::Vec3f(0.f, longitudinalOffset, 0.f));
                const osg::Vec3f rayStart
                    = samplePosition + osg::Vec3f(0.f, 0.f, probeAboveMount);
                const osg::Vec3f rayEnd
                    = samplePosition - osg::Vec3f(0.f, 0.f, probeBelowMount);
                const MWPhysics::RayCastingResult hit
                    = rayCasting->castRay(rayStart, rayEnd, { actor }, {}, collisionMask);
                if (!hit.mHit)
                    continue;

                const float arcHeight = std::sqrt(
                    std::max(wheelRadius * wheelRadius - longitudinalOffset * longitudinalOffset, 0.f));
                const float equivalentGround = hit.mHitPos.z() + arcHeight - wheelRadius;
                if (equivalentGround <= bestEquivalentGround)
                    continue;

                bestEquivalentGround = equivalentGround;
                wheel.mTerrainHit = true;
                wheel.mContactPoint = hit.mHitPos;
                wheel.mContactPoint.z() = equivalentGround;
                wheel.mContactNormal = hit.mHitNormal;
            }

            if (wheel.mTerrainHit)
            {
                wheel.mRequiredSuspensionLength
                    = wheelMounts[index].z() - wheel.mContactPoint.z() - wheelRadius;
                wheel.mGrounded = wheel.mRequiredSuspensionLength >= terrainMinimumLength
                    && wheel.mRequiredSuspensionLength <= restLength + maxDroop + supportSlack;
            }
            if (wheel.mGrounded)
                ++state.mSupportedWheels;
        }

        auto supportedAverage = [&](std::size_t first, std::size_t second, float fallback) {
            float total = 0.f;
            unsigned int count = 0;
            if (state.mWheels[first].mGrounded)
            {
                total += state.mWheels[first].mContactPoint.z();
                ++count;
            }
            if (state.mWheels[second].mGrounded)
            {
                total += state.mWheels[second].mContactPoint.z();
                ++count;
            }
            return count != 0 ? total / static_cast<float>(count) : fallback;
        };

        const float frontHeight = supportedAverage(0, 1, rootPosition.z());
        const float rearHeight = supportedAverage(2, 3, rootPosition.z());
        const float leftHeight = supportedAverage(0, 2, rootPosition.z());
        const float rightHeight = supportedAverage(1, 3, rootPosition.z());
        const bool hasFrontSupport = state.mWheels[0].mGrounded || state.mWheels[1].mGrounded;
        const bool hasRearSupport = state.mWheels[2].mGrounded || state.mWheels[3].mGrounded;
        const bool hasLeftSupport = state.mWheels[0].mGrounded || state.mWheels[2].mGrounded;
        const bool hasRightSupport = state.mWheels[1].mGrounded || state.mWheels[3].mGrounded;
        const float effectiveWheelbase = std::max(
            std::abs(suspension.wheels[0].mountPosition[1] - suspension.wheels[2].mountPosition[1]) * scale, 1.f);
        const float effectiveTrack = std::max(
            std::abs(suspension.wheels[1].mountPosition[0] - suspension.wheels[0].mountPosition[0]) * scale, 1.f);
        const float maxAirbornePitch
            = osg::DegreesToRadians(suspension.maxAirbornePitchDegrees);
        const float terrainPitchLimit = osg::DegreesToRadians(suspension.maxPitchDegrees);
        const float terrainRollLimit = osg::DegreesToRadians(suspension.maxRollDegrees);
        auto projectedTerrainAverage = [&](std::size_t first, std::size_t second,
                                           float fallback, bool& hasProjectedTerrain) {
            float total = 0.f;
            unsigned int count = 0;
            for (const std::size_t index : { first, second })
            {
                const VehicleWheelContactState& wheel = state.mWheels[index];
                if (wheel.mTerrainHit)
                {
                    total += wheel.mContactPoint.z();
                    ++count;
                }
            }
            hasProjectedTerrain = count != 0;
            return count != 0 ? total / static_cast<float>(count) : fallback;
        };
        bool hasProjectedFrontTerrain = false;
        bool hasProjectedRearTerrain = false;
        bool hasProjectedLeftTerrain = false;
        bool hasProjectedRightTerrain = false;
        const float projectedFrontHeight = projectedTerrainAverage(
            0, 1, rootPosition.z(), hasProjectedFrontTerrain);
        const float projectedRearHeight = projectedTerrainAverage(
            2, 3, rootPosition.z(), hasProjectedRearTerrain);
        const float projectedLeftHeight = projectedTerrainAverage(
            0, 2, rootPosition.z(), hasProjectedLeftTerrain);
        const float projectedRightHeight = projectedTerrainAverage(
            1, 3, rootPosition.z(), hasProjectedRightTerrain);
        const float projectedTerrainDelta = projectedFrontHeight - projectedRearHeight;
        const float projectedCrossTerrainDelta = projectedLeftHeight - projectedRightHeight;
        const bool previousTerrainBridge = state.mTerrainBridge;
        const bool terrainBridge = hasProjectedFrontTerrain && hasProjectedRearTerrain;
        const bool terrainCrossBridge = hasProjectedLeftTerrain && hasProjectedRightTerrain;
        state.mTerrainBridge = terrainBridge;
        state.mTerrainCrossBridge = terrainCrossBridge;
        osg::Vec3f terrainNormal(0.f, 0.f, 0.f);
        unsigned int terrainNormalCount = 0;
        for (const VehicleWheelContactState& wheel : state.mWheels)
        {
            if (!wheel.mTerrainHit || wheel.mContactNormal.length2() <= 0.0001f)
                continue;

            osg::Vec3f normal = wheel.mContactNormal;
            normal.normalize();
            if (normal.z() < 0.f)
                normal = -normal;
            terrainNormal += normal;
            ++terrainNormalCount;
        }
        const bool hasTerrainNormal
            = terrainNormalCount != 0 && terrainNormal.length2() > 0.0001f;
        if (hasTerrainNormal)
            terrainNormal.normalize();
        else
            terrainNormal.set(0.f, 0.f, 1.f);
        const osg::Vec3f localTerrainNormal = yawRotation.inverse() * terrainNormal;
        const float safeDuration = std::clamp(duration, 0.f, sMaxSolverDuration);
        state.mTerrainNormalPitch = std::clamp(
            std::atan2(-localTerrainNormal.y(), std::max(localTerrainNormal.z(), 0.1f)),
            -terrainPitchLimit, terrainPitchLimit);
        state.mTerrainNormalRoll = std::clamp(
            std::atan2(localTerrainNormal.x(), std::max(localTerrainNormal.z(), 0.1f)),
            -terrainRollLimit, terrainRollLimit);
        float targetPitch = hasFrontSupport && hasRearSupport
            ? std::clamp(std::atan2(frontHeight - rearHeight, effectiveWheelbase),
                -terrainPitchLimit, terrainPitchLimit)
            : terrainBridge
                ? std::clamp(std::atan2(projectedTerrainDelta, effectiveWheelbase),
                    -terrainPitchLimit, terrainPitchLimit)
                : hasTerrainNormal
                    ? state.mTerrainNormalPitch
                    : state.mPitch;
        float targetRoll = hasLeftSupport && hasRightSupport
            ? std::clamp(std::atan2(leftHeight - rightHeight, effectiveTrack),
                -terrainRollLimit, terrainRollLimit)
            : terrainCrossBridge
                ? std::clamp(std::atan2(projectedCrossTerrainDelta, effectiveTrack),
                    -terrainRollLimit, terrainRollLimit)
                : hasTerrainNormal
                    ? state.mTerrainNormalRoll
                    : state.mRoll;
        const float groundPitchTarget = targetPitch;
        const float speedReference = std::max(profile.handling.maxForwardSpeed * 0.35f, 1.f);
        const float normalizedAirborneSpeed
            = std::clamp(std::abs(longitudinalSpeed) / speedReference, 0.f, 1.f);
        const float maxAirbornePitchRate = osg::DegreesToRadians(
            suspension.edgeTipBaseRateDegrees
            + suspension.edgeTipSpeedRateDegrees * normalizedAirborneSpeed);
        const bool stableGroundContact
            = terrainBridge && state.mSupportedWheels >= 2;
        const bool supportLost = state.mSupportedWheels == 0
            && !hasProjectedFrontTerrain && !hasProjectedRearTerrain;

        if (!state.mAirborne && stableGroundContact)
        {
            if (state.mHadGroundContact && safeDuration > 0.0001f)
            {
                const float measuredPitchVelocity = std::clamp(
                    (groundPitchTarget - state.mLastGroundPitch) / safeDuration,
                    -maxAirbornePitchRate, maxAirbornePitchRate);
                const float response = std::clamp(
                    safeDuration * sGroundPitchVelocityResponse, 0.f, 1.f);
                state.mGroundPitchVelocity +=
                    (measuredPitchVelocity - state.mGroundPitchVelocity) * response;
            }
            else
                state.mGroundPitchVelocity = 0.f;

            state.mLastGroundPitch = groundPitchTarget;
            state.mHadGroundContact = true;
            state.mTakeoffDirection = 0.f;
        }

        if (!state.mAirborne && previousTerrainBridge && !terrainBridge
            && std::abs(longitudinalSpeed) >= sTakeoffDirectionSpeed)
        {
            state.mTakeoffDirection = longitudinalSpeed >= 0.f ? 1.f : -1.f;
        }

        if (!state.mAirborne)
        {
            state.mUnsupportedTime = supportLost
                ? state.mUnsupportedTime + safeDuration
                : 0.f;

            if (state.mUnsupportedTime >= sTakeoffSupportLossHold)
            {
                state.mAirborne = true;
                state.mAirborneTime = 0.f;
                state.mLandingSupportTime = 0.f;
                state.mPitchVelocity = state.mHadGroundContact
                    ? state.mGroundPitchVelocity
                    : 0.f;
                if (state.mTakeoffDirection == 0.f
                    && std::abs(longitudinalSpeed) >= sTakeoffDirectionSpeed)
                {
                    state.mTakeoffDirection = longitudinalSpeed >= 0.f ? 1.f : -1.f;
                }
            }
        }

        if (state.mAirborne)
        {
            const bool penetratingFrontAxle = state.mWheels[0].mTerrainHit
                && state.mWheels[1].mTerrainHit
                && state.mWheels[0].mRequiredSuspensionLength < terrainMinimumLength
                && state.mWheels[1].mRequiredSuspensionLength < terrainMinimumLength;
            const bool penetratingRearAxle = state.mWheels[2].mTerrainHit
                && state.mWheels[3].mTerrainHit
                && state.mWheels[2].mRequiredSuspensionLength < terrainMinimumLength
                && state.mWheels[3].mRequiredSuspensionLength < terrainMinimumLength;
            const bool landingContact = terrainBridge || state.mSupportedWheels >= 2
                || penetratingFrontAxle || penetratingRearAxle;
            state.mLandingSupportTime = landingContact
                ? state.mLandingSupportTime + safeDuration
                : 0.f;

            if (state.mLandingSupportTime >= sLandingSupportHold)
            {
                state.mAirborne = false;
                state.mPitchVelocity = 0.f;
                state.mGroundPitchVelocity = 0.f;
                state.mAirborneTime = 0.f;
                state.mTakeoffDirection = 0.f;
                state.mLandingSupportTime = 0.f;
                state.mUnsupportedTime = 0.f;
                state.mHadGroundContact = stableGroundContact;
                state.mLastGroundPitch = groundPitchTarget;
            }
        }

        if (state.mAirborne)
        {
            if (state.mTakeoffDirection != 0.f)
            {
                const float noseDownDirection = state.mTakeoffDirection > 0.f ? -1.f : 1.f;
                const float pitchAcceleration = osg::DegreesToRadians(
                    sAirbornePitchAccelerationBase
                    + sAirbornePitchAccelerationSpeed * normalizedAirborneSpeed);
                state.mPitchVelocity = std::clamp(
                    state.mPitchVelocity + noseDownDirection * pitchAcceleration * safeDuration,
                    -maxAirbornePitchRate, maxAirbornePitchRate);
            }
            else
            {
                const float pitchDamping
                    = osg::DegreesToRadians(suspension.edgeTipResponseDegrees) * safeDuration;
                state.mPitchVelocity = moveTowards(state.mPitchVelocity, 0.f, pitchDamping);
            }
            targetPitch = std::clamp(
                state.mPitch + state.mPitchVelocity * safeDuration,
                -maxAirbornePitch, maxAirbornePitch);
            targetRoll = state.mRoll;
            state.mAirborneTime += safeDuration;
        }
        else
        {
            state.mAirborneTime = 0.f;
        }

        const osg::Quat targetAttitude(targetPitch, osg::Vec3f(1.f, 0.f, 0.f),
            targetRoll, osg::Vec3f(0.f, 1.f, 0.f), 0.f, osg::Vec3f(0.f, 0.f, 1.f));
        float supportedHeightTotal = 0.f;
        unsigned int supportedHeightCount = 0;
        for (std::size_t index = 0; index < state.mWheels.size(); ++index)
        {
            const bool terrainPoseAnchor
                = !state.mAirborne && state.mWheels[index].mTerrainHit;
            if ((!state.mAirborne && state.mWheels[index].mGrounded) || terrainPoseAnchor)
            {
                const std::array<float, 3>& mount = suspension.wheels[index].mountPosition;
                const osg::Vec3f nominalContact(mount[0] * scale, mount[1] * scale,
                    (mount[2] - suspension.restLength - suspension.wheelRadius) * scale);
                supportedHeightTotal += state.mWheels[index].mContactPoint.z() - rootPosition.z()
                    - (targetAttitude * nominalContact).z();
                ++supportedHeightCount;
            }
        }
        const float targetVerticalOffset = supportedHeightCount != 0
            ? supportedHeightTotal / static_cast<float>(supportedHeightCount)
            : state.mVerticalOffset;

        if (initialize)
        {
            state.mPitch = targetPitch;
            state.mRoll = targetRoll;
            state.mVerticalOffset = targetVerticalOffset;
        }
        else
        {
            const float maxPoseDelta
                = osg::DegreesToRadians(suspension.maxPoseRateDegrees) * safeDuration;
            state.mPitch = moveTowards(state.mPitch, targetPitch, maxPoseDelta);
            state.mRoll = moveTowards(state.mRoll, targetRoll, maxPoseDelta);
            state.mVerticalOffset = moveTowards(state.mVerticalOffset, targetVerticalOffset,
                suspension.maxVerticalPoseSpeed * scale * safeDuration);
        }

        const osg::Quat chassisAttitude(state.mPitch, osg::Vec3f(1.f, 0.f, 0.f),
            state.mRoll, osg::Vec3f(0.f, 1.f, 0.f), 0.f, osg::Vec3f(0.f, 0.f, 1.f));
        world->setActorCollisionTransform(
            actor, yawRotation * chassisAttitude, osg::Vec3f(0.f, 0.f, state.mVerticalOffset));
        const float chassisUpZ
            = std::max((chassisAttitude * osg::Vec3f(0.f, 0.f, 1.f)).z(), 0.25f);
        float targetCompression[4] = {};
        for (std::size_t index = 0; index < state.mWheels.size(); ++index)
        {
            const std::array<float, 3>& mount = suspension.wheels[index].mountPosition;
            const osg::Vec3f nominalCenter(
                mount[0] * scale, mount[1] * scale, (mount[2] - suspension.restLength) * scale);
            const float transformedCenterZ = rootPosition.z() + state.mVerticalOffset
                + (chassisAttitude * nominalCenter).z();
            targetCompression[index] = state.mWheels[index].mGrounded
                ? std::clamp(
                    (state.mWheels[index].mContactPoint.z() + wheelRadius - transformedCenterZ)
                        / chassisUpZ,
                    -maxDroop, maxCompression)
                : -maxDroop;

            if (initialize)
            {
                state.mWheels[index].mCompression = targetCompression[index];
                state.mWheels[index].mCompressionVelocity = 0.f;
            }
        }

        if (!initialize)
        {
            float remaining = std::clamp(duration, 0.f, sMaxSolverDuration);
            while (remaining > 0.f)
            {
                const float step = std::min(remaining, sSolverStep);
                for (std::size_t index = 0; index < state.mWheels.size(); ++index)
                {
                    VehicleWheelContactState& wheel = state.mWheels[index];
                    const float acceleration = suspension.springRate
                            * (targetCompression[index] - wheel.mCompression)
                        - suspension.dampingRate * wheel.mCompressionVelocity;
                    wheel.mCompressionVelocity += acceleration * step;
                    wheel.mCompressionVelocity = std::clamp(wheel.mCompressionVelocity,
                        -suspension.maxCompressionSpeed * scale, suspension.maxCompressionSpeed * scale);
                    wheel.mCompression += wheel.mCompressionVelocity * step;
                    wheel.mCompression = std::clamp(wheel.mCompression, -maxDroop, maxCompression);
                }
                remaining -= step;
            }
        }

        for (VehicleWheelContactState& wheel : state.mWheels)
        {
            wheel.mSuspensionLength = restLength - wheel.mCompression;
            wheel.mVisualOffset = wheel.mCompression;
        }
        state.mInitialized = true;

        if (MWRender::Animation* animation = world->getAnimation(actor))
        {
            const osg::Vec3f chassisPosition(0.f, 0.f, state.mVerticalOffset / scale);
            animation->setEffectTransform(sVehicleVisualEffectId,
                chassisPosition, chassisAttitude);
            animation->setVehicleDriverSuspensionTransform(chassisPosition, chassisAttitude);

            for (std::size_t index = 0; index < state.mWheels.size(); ++index)
            {
                animation->setEffectNodeOffset(sVehicleVisualEffectId,
                    suspension.wheels[index].visualNode,
                    osg::Vec3f(0.f, 0.f, state.mWheels[index].mVisualOffset / scale));
            }
        }
    }

    void updateLocalVehicle(const MWWorld::Ptr& player, float duration)
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Player& playerState = world->getPlayer();
        MWWorld::VehicleRuntimeState& state = playerState.getVehicleRuntimeState();
        if (state.mMode != MWWorld::VehicleModeState::Active)
        {
            sLocalSuspensionState = {};
            state.mVisualSuspensionInitialized = false;
            state.mVisualSuspensionTravel.fill(0.f);
            state.mWheelRollAngles.fill(0.f);
            state.mVisualSteeringAngle = 0.f;
            stopVehicleAudio();
            return;
        }

        const mwmp::VehicleProfile* profile = mwmp::findVehicleProfile(state.mProfileId);
        if (!profile)
        {
            sLocalSuspensionState = {};
            state.mVisualSuspensionInitialized = false;
            state.mVisualSuspensionTravel.fill(0.f);
            state.mWheelRollAngles.fill(0.f);
            state.mVisualSteeringAngle = 0.f;
            stopVehicleAudio();
            world->queueMovement(player, osg::Vec3f());
            return;
        }

        MWPhysics::VehicleBodyState bodyState;
        world->setVehicleRigidBodyInput(player,
            MWPhysics::VehicleBodyInput{ state.mInput.mThrottle, state.mInput.mBrake,
                state.mInput.mSteering, state.mInput.mHandbrake });
        if (!world->getVehicleRigidBodyState(player, bodyState))
        {
            world->queueMovement(player, osg::Vec3f());
            return;
        }

        float presentationScale = 1.f;
        if (const SceneUtil::PositionAttitudeTransform* baseNode = player.getRefData().getBaseNode())
            presentationScale = std::max(std::abs(baseNode->getScale().y()), 0.001f);

        std::array<float, 4> suspensionTravel{};
        const float scaledRestLength = profile->suspension.restLength * presentationScale;
        for (std::size_t index = 0; index < suspensionTravel.size(); ++index)
            suspensionTravel[index] = scaledRestLength - bodyState.mSuspensionLength[index];

        // A wheel moving upward must follow the contact immediately; delaying
        // compression lets the visible tire enter rising terrain. Smooth only
        // extension/droop so contact loss cannot flicker the tire downward.
        if (!state.mVisualSuspensionInitialized)
        {
            state.mVisualSuspensionTravel = suspensionTravel;
            state.mVisualSuspensionInitialized = true;
        }
        else
        {
            const float visualDt = std::clamp(duration, 0.f, sMaxSolverDuration);
            const float extensionAlpha = 1.f - std::exp(-sVisualSuspensionResponse * visualDt);
            for (std::size_t index = 0; index < state.mVisualSuspensionTravel.size(); ++index)
            {
                if (suspensionTravel[index] >= state.mVisualSuspensionTravel[index])
                    state.mVisualSuspensionTravel[index] = suspensionTravel[index];
                else
                {
                    state.mVisualSuspensionTravel[index] +=
                        (suspensionTravel[index] - state.mVisualSuspensionTravel[index]) * extensionAlpha;
                }
            }
        }

        const float visualDt = std::clamp(duration, 0.f, sMaxSolverDuration);
        const osg::Vec3f localVelocity = bodyState.mOrientation.inverse() * bodyState.mLinearVelocity;
        const osg::Vec3f localAngularVelocity = bodyState.mOrientation.inverse() * bodyState.mAngularVelocity;
        const float scaledWheelRadius = std::max(profile->suspension.wheelRadius * presentationScale, 0.001f);
        for (std::size_t index = 0; index < state.mWheelRollAngles.size(); ++index)
        {
            const float wheelX = profile->suspension.wheels[index].mountPosition[0] * presentationScale;
            const float wheelLongitudinalSpeed = localVelocity.y() + localAngularVelocity.z() * wheelX;
            state.mWheelRollAngles[index] = std::remainder(
                state.mWheelRollAngles[index] - wheelLongitudinalSpeed / scaledWheelRadius * visualDt,
                static_cast<float>(osg::PI * 2.0));
        }
        state.mSteeringAngle = bodyState.mSteeringAngle;
        state.mVisualSteeringAngle = updateVisualSteeringAngle(
            state.mVisualSteeringAngle, state.mSteeringAngle, profile->handling, visualDt);
        updateVehicleAudio(player, state, *profile, bodyState, suspensionTravel, visualDt);

        world->moveObject(player, bodyState.mRootPosition, false);

        const osg::Vec3f forward = bodyState.mOrientation * osg::Vec3f(0.f, 1.f, 0.f);
        const float yaw = std::atan2(forward.x(), forward.y());
        const float currentYaw = player.getRefData().getPosition().rot[2];
        const float yawDelta
            = std::atan2(std::sin(yaw - currentYaw), std::cos(yaw - currentYaw));
        if (std::abs(yawDelta) > std::numeric_limits<float>::epsilon())
            world->rotateObject(player, osg::Vec3f(0.f, 0.f, yawDelta), true);

        const osg::Quat yawRotation(yaw, osg::Vec3f(0.f, 0.f, -1.f));
        // The actor scene node already applies vehicle yaw. OSG quaternion
        // multiplication composes left-to-right, so remove that parent yaw on
        // the right. This preserves yaw * (relative * v) == body * v for the
        // truck visual, seated driver, and camera anchor.
        const osg::Quat relativeAttitude = bodyState.mOrientation * yawRotation.inverse();
        std::array<bool, 4> visualNodeUpdated{};
        if (MWRender::Animation* animation = world->getAnimation(player))
        {
            animation->setEffectTransform(
                sVehicleVisualEffectId, osg::Vec3f(), relativeAttitude);
            animation->setVehicleDriverSuspensionTransform(osg::Vec3f(), relativeAttitude);
            const osg::Vec3f wheelRollAxis(1.f, 0.f, 0.f);
            const osg::Vec3f steeringAxis(0.f, 0.f, 1.f);
            for (std::size_t index = 0; index < profile->suspension.wheels.size(); ++index)
            {
                const mwmp::VehicleWheelProfile& wheel = profile->suspension.wheels[index];
                const osg::Vec3f pivot(wheel.mountPosition[0], wheel.mountPosition[1],
                    wheel.mountPosition[2] - profile->suspension.restLength);
                const float visualSteeringAngle = index < 2 ? -state.mVisualSteeringAngle : 0.f;
                // OSG quaternion multiplication composes left-to-right. Roll the wheel
                // around its own axle first, then steer that rotating wheel around Z.
                // Steering first would leave rolling on the truck's fixed X axis and
                // make the tire tumble diagonally at steering lock.
                const osg::Quat wheelAttitude
                    = osg::Quat(state.mWheelRollAngles[index], wheelRollAxis)
                    * osg::Quat(visualSteeringAngle, steeringAxis);
                visualNodeUpdated[index] = animation->setEffectNodeTransform(
                    sVehicleVisualEffectId, wheel.visualNode, pivot,
                    osg::Vec3f(0.f, 0.f,
                        (wheel.visualContactPlaneOffset + state.mVisualSuspensionTravel[index])
                            / presentationScale),
                    wheelAttitude);
            }
        }

        state.mLateralSpeed = localVelocity.x();
        state.mForwardSpeed = localVelocity.y();
        state.mYawRate = bodyState.mAngularVelocity.z();
        state.mGrounded = false;
        state.mMotionInitialized = false;
        world->queueMovement(player, osg::Vec3f());

        state.mLogTimer += std::max(duration, 0.f);
        if (state.mLogTimer >= 1.f)
        {
            state.mLogTimer = 0.f;
            Log(Debug::Info) << "[VehicleBody] state root=(" << bodyState.mRootPosition.x() << ", "
                             << bodyState.mRootPosition.y() << ", " << bodyState.mRootPosition.z()
                             << ") linear=(" << bodyState.mLinearVelocity.x() << ", "
                             << bodyState.mLinearVelocity.y() << ", " << bodyState.mLinearVelocity.z()
                             << ") angular=(" << bodyState.mAngularVelocity.x() << ", "
                             << bodyState.mAngularVelocity.y() << ", " << bodyState.mAngularVelocity.z()
                             << ") grounded=(" << bodyState.mWheelGrounded[0] << ", "
                             << bodyState.mWheelGrounded[1] << ", " << bodyState.mWheelGrounded[2] << ", "
                             << bodyState.mWheelGrounded[3] << ") compression=("
                             << bodyState.mSuspensionCompression[0] << ", "
                             << bodyState.mSuspensionCompression[1] << ", "
                             << bodyState.mSuspensionCompression[2] << ", "
                             << bodyState.mSuspensionCompression[3]
                             << ") travel=(" << suspensionTravel[0] << ", " << suspensionTravel[1] << ", "
                             << suspensionTravel[2] << ", " << suspensionTravel[3]
                             << ") visualTravel=(" << state.mVisualSuspensionTravel[0] << ", "
                             << state.mVisualSuspensionTravel[1] << ", "
                             << state.mVisualSuspensionTravel[2] << ", "
                             << state.mVisualSuspensionTravel[3]
                             << ") visualNodes=(" << visualNodeUpdated[0] << ", " << visualNodeUpdated[1] << ", "
                             << visualNodeUpdated[2] << ", " << visualNodeUpdated[3]
                             << ") input=(" << state.mInput.mThrottle << ", " << state.mInput.mBrake << ", "
                             << state.mInput.mSteering << ", " << state.mInput.mHandbrake << ")"
                             << " parkingHold=" << bodyState.mParkingBrakeHolding
                             << " slopeDeg=" << bodyState.mGroundSlopeDegrees
                             << " handbrakeSlip=" << bodyState.mHandbrakeSlipFactor;
        }
    }
}
