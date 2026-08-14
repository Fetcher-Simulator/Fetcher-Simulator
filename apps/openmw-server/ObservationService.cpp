#include "ObservationService.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>

namespace
{
    float lengthSquared(const mwmp::ObservationVector& value)
    {
        return value.x * value.x + value.y * value.y + value.z * value.z;
    }

    mwmp::ObservationVector subtract(const mwmp::ObservationVector& left, const mwmp::ObservationVector& right)
    {
        return { left.x - right.x, left.y - right.y, left.z - right.z };
    }

    float dot(const mwmp::ObservationVector& left, const mwmp::ObservationVector& right)
    {
        return left.x * right.x + left.y * right.y + left.z * right.z;
    }

    bool isFresh(const mwmp::ObservationActorSnapshot& snapshot, std::uint64_t observedAtMs, std::uint64_t maximumAgeMs)
    {
        return snapshot.snapshotGeneration != 0 && observedAtMs >= snapshot.sampledAtMs
            && observedAtMs - snapshot.sampledAtMs <= maximumAgeMs;
    }

    bool isFinite(const mwmp::ObservationVector& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    bool hasFiniteAwarenessInputs(const mwmp::ObservationActorSnapshot& value)
    {
        return isFinite(value.position) && isFinite(value.forward) && std::isfinite(value.sneakSkill)
            && std::isfinite(value.agility) && std::isfinite(value.luck) && std::isfinite(value.fatigueCurrent)
            && std::isfinite(value.fatigueMaximumModified) && std::isfinite(value.bootWeight)
            && std::isfinite(value.chameleon) && std::isfinite(value.invisibility) && std::isfinite(value.blind);
    }

    bool hasCanonicalCollisionGenerations(const std::vector<mwmp::CollisionCellGeneration>& generations)
    {
        if (generations.empty())
            return false;
        for (std::size_t i = 0; i < generations.size(); ++i)
        {
            if (generations[i].cellId.empty() || generations[i].generation == 0)
                return false;
            if (i != 0 && generations[i - 1].cellId >= generations[i].cellId)
                return false;
        }
        return true;
    }

    mwmp::ObservationAuthority mergeAuthority(mwmp::ObservationAuthority left, mwmp::ObservationAuthority right)
    {
        using Authority = mwmp::ObservationAuthority;
        if (left == right)
            return left;
        if (left == Authority::MixedDelegated || right == Authority::MixedDelegated)
            return Authority::MixedDelegated;
        if (left == Authority::ServerAuthoritative)
            return right;
        if (right == Authority::ServerAuthoritative)
            return left;
        return Authority::MixedDelegated;
    }
}

namespace mwmp
{
    bool ObservationActorIdentity::isValid() const
    {
        if (kind == ObservationActorKind::Player)
            return playerGuid != 0 && actorInstanceId == 0;
        return playerGuid == 0 && isValidActorInstanceId(actorInstanceId);
    }

    std::size_t ObservationActorIdentityHash::operator()(const ObservationActorIdentity& value) const noexcept
    {
        std::size_t result = std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(value.kind));
        result ^= std::hash<std::uint32_t>{}(value.playerGuid) + 0x9e3779b9u + (result << 6) + (result >> 2);
        result ^= std::hash<ActorInstanceId>{}(value.actorInstanceId) + 0x9e3779b9u + (result << 6) + (result >> 2);
        return result;
    }

    ObservationService::ObservationService(
        AwarenessSettings settings, const ObservationCollisionBackend& collision, AwarenessRollSource& rollSource)
        : mSettings(settings)
        , mCollision(collision)
        , mRollSource(rollSource)
    {
        if (!std::isfinite(settings.sneakSkillMultiplier) || !std::isfinite(settings.sneakBootMultiplier)
            || !std::isfinite(settings.sneakDistanceBase) || !std::isfinite(settings.sneakDistanceMultiplier)
            || !std::isfinite(settings.sneakNoViewMultiplier) || !std::isfinite(settings.sneakViewMultiplier)
            || !std::isfinite(settings.fatigueBase) || !std::isfinite(settings.fatigueMultiplier))
            throw std::invalid_argument("ObservationService settings must be finite");
    }

    float ObservationService::fatigueTerm(float current, float modifiedMaximum, const AwarenessSettings& settings)
    {
        const float normalised = std::floor(modifiedMaximum) == 0.f ? 1.f : std::max(0.f, current / modifiedMaximum);
        return settings.fatigueBase - settings.fatigueMultiplier * (1.f - normalised);
    }

    AwarenessCalculation ObservationService::calculateAwareness(const ObservationActorSnapshot& target,
        const ObservationActorSnapshot& observer, const AwarenessSettings& settings, int roll)
    {
        AwarenessCalculation result;

        float sneakTerm = 0.f;
        if (target.sneaking)
        {
            const bool targetCanWearBoots = target.identity.kind == ObservationActorKind::Player
                || target.identity.kind == ObservationActorKind::Npc;
            const float bootWeight = targetCanWearBoots && target.onGround ? target.bootWeight : 0.f;
            sneakTerm = settings.sneakSkillMultiplier * target.sneakSkill + 0.2f * target.agility + 0.1f * target.luck
                + bootWeight * settings.sneakBootMultiplier;
        }

        const ObservationVector relative = subtract(target.position, observer.position);
        const float distance = std::sqrt(lengthSquared(relative));
        const float distanceTerm = settings.sneakDistanceBase + settings.sneakDistanceMultiplier * distance;
        result.targetConcealment
            = sneakTerm * distanceTerm * fatigueTerm(target.fatigueCurrent, target.fatigueMaximumModified, settings)
            + target.chameleon;
        if (target.invisibility > 0.f)
            result.targetConcealment += 100.f;

        const float observerTerm
            = observer.sneakSkill + 0.2f * observer.agility + 0.1f * observer.luck - observer.blind;
        const float forwardLengthSquared = lengthSquared(observer.forward);
        if (observer.hasFacing && forwardLengthSquared > std::numeric_limits<float>::epsilon())
        {
            result.targetBehindObserver = dot(observer.forward, relative) < 0.f;
            const float viewMultiplier
                = result.targetBehindObserver ? settings.sneakNoViewMultiplier : settings.sneakViewMultiplier;
            result.observerDetection = observerTerm
                * fatigueTerm(observer.fatigueCurrent, observer.fatigueMaximumModified, settings) * viewMultiplier;
        }

        result.threshold = result.targetConcealment - result.observerDetection;
        result.detected = static_cast<float>(roll) >= result.threshold;
        return result;
    }

    ObservationResult ObservationService::observe(const ObservationQuery& query)
    {
        ObservationResult result;
        result.path = query.path;
        result.observerMigrationGeneration = query.observer.migrationGeneration;
        result.observerAuthorityGeneration = query.observer.authorityGeneration;
        result.observerSnapshotGeneration = query.observer.snapshotGeneration;
        result.targetSnapshotGeneration = query.target.snapshotGeneration;
        result.collisionGenerations = query.collisionGenerations;
        result.authority = mergeAuthority(query.eventAuthority, query.observer.authority);

        if (query.eventId.empty() || query.cellId.empty() || !query.observer.identity.isValid()
            || !query.target.identity.isValid() || !hasFiniteAwarenessInputs(query.observer)
            || static_cast<std::uint8_t>(query.path) > static_cast<std::uint8_t>(ObservationPath::MurderHearing)
            || static_cast<std::uint8_t>(query.observerPolicy)
                > static_cast<std::uint8_t>(ObservationObserverPolicy::VanillaCrimeWitness))
            return result;
        if (query.observerPolicy == ObservationObserverPolicy::VanillaCrimeWitness
            && query.observer.identity.kind != ObservationActorKind::Npc)
        {
            result.reason = ObservationReason::ObserverKindRejected;
            return result;
        }
        if (!query.observer.eligibilityKnown || !query.observer.enabled || !query.observer.alive
            || !query.observer.conscious)
        {
            result.reason = ObservationReason::ObserverIneligible;
            return result;
        }
        if (!isFresh(query.observer, query.observedAtMs, query.maximumSnapshotAgeMs))
        {
            result.reason = ObservationReason::StaleActorSnapshot;
            return result;
        }

        if (query.path == ObservationPath::VictimAware)
        {
            if (!query.victim || !query.victim->isValid() || *query.victim != query.observer.identity)
                return result;
            result.observable = true;
            result.reason = ObservationReason::Observed;
            return result;
        }
        if (query.path == ObservationPath::MurderHearing)
        {
            if (!query.victim || !query.victim->isValid() || *query.victim == query.observer.identity)
                return result;
            result.observable = true;
            result.reason = ObservationReason::Observed;
            return result;
        }

        result.authority = mergeAuthority(result.authority, query.target.authority);
        if (!query.observer.awarenessInputsKnown || !query.target.awarenessInputsKnown || !query.observer.hasFacing
            || !isFresh(query.target, query.observedAtMs, query.maximumSnapshotAgeMs))
        {
            result.reason = ObservationReason::StaleActorSnapshot;
            return result;
        }

        if (!hasFiniteAwarenessInputs(query.target) || !hasCanonicalCollisionGenerations(query.collisionGenerations)
            || std::none_of(query.collisionGenerations.begin(), query.collisionGenerations.end(),
                [&](const CollisionCellGeneration& generation) { return generation.cellId == query.cellId; }))
        {
            result.reason = ObservationReason::InvalidQuery;
            return result;
        }

        std::vector<std::string> collisionCellIds;
        collisionCellIds.reserve(query.collisionGenerations.size());
        for (const CollisionCellGeneration& generation : query.collisionGenerations)
            collisionCellIds.push_back(generation.cellId);
        const CollisionObservation collision
            = mCollision.lineOfSight(collisionCellIds, query.target.position, query.observer.position);
        result.collisionGenerations = collision.generations;
        if (!collision.available)
        {
            result.reason = ObservationReason::CollisionUnavailable;
            return result;
        }
        if (!hasCanonicalCollisionGenerations(collision.generations)
            || collision.generations != query.collisionGenerations)
        {
            result.reason = ObservationReason::CollisionGenerationMismatch;
            return result;
        }
        result.lineOfSight = collision.clear;
        if (!collision.clear)
        {
            result.reason = ObservationReason::BlockedLineOfSight;
            return result;
        }

        const int roll = awarenessRoll(query.observer, query.observedAtMs);
        const AwarenessCalculation awareness = calculateAwareness(query.target, query.observer, mSettings, roll);
        result.awarenessRoll = roll;
        result.awarenessThreshold = awareness.threshold;
        result.awareness = awareness.detected;
        result.observable = awareness.detected;
        result.reason = awareness.detected ? ObservationReason::Observed : ObservationReason::AwarenessFailed;
        return result;
    }

    void ObservationService::invalidateObserver(const ObservationActorIdentity& observer)
    {
        mAwarenessRolls.erase(observer);
    }

    void ObservationService::clearAwarenessRolls()
    {
        mAwarenessRolls.clear();
    }

    int ObservationService::awarenessRoll(const ObservationActorSnapshot& observer, std::uint64_t nowMs)
    {
        auto [it, inserted] = mAwarenessRolls.try_emplace(observer.identity);
        CachedRoll& cached = it->second;
        const bool generationChanged = !inserted
            && (cached.migrationGeneration != observer.migrationGeneration
                || cached.authorityGeneration != observer.authorityGeneration);
        const bool expired
            = !inserted && (nowMs < cached.acquiredAtMs || nowMs - cached.acquiredAtMs >= AwarenessRollLifetimeMs);
        if (inserted || generationChanged || expired)
        {
            cached.value = std::clamp(mRollSource.nextRoll0To99(), 0, 99);
            cached.acquiredAtMs = nowMs;
            cached.migrationGeneration = observer.migrationGeneration;
            cached.authorityGeneration = observer.authorityGeneration;
        }
        return cached.value;
    }
}
