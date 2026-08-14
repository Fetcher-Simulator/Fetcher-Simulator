#ifndef OPENMW_SERVER_OBSERVATIONSERVICE_HPP
#define OPENMW_SERVER_OBSERVATIONSERVICE_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <components/openmw-mp/Base/ActorSyncProtocol.hpp>

namespace mwmp
{
    enum class ObservationActorKind : std::uint8_t
    {
        Player = 0,
        Npc = 1,
        Creature = 2,
    };

    struct ObservationActorIdentity
    {
        ObservationActorKind kind = ObservationActorKind::Player;
        std::uint32_t playerGuid = 0;
        ActorInstanceId actorInstanceId = 0;

        bool isValid() const;
        bool operator==(const ObservationActorIdentity&) const = default;
    };

    struct ObservationActorIdentityHash
    {
        std::size_t operator()(const ObservationActorIdentity& value) const noexcept;
    };

    enum class ObservationAuthority : std::uint8_t
    {
        ServerAuthoritative = 0,
        ActorAuthorityDelegated = 1,
        PlayerClientDelegated = 2,
        MixedDelegated = 3,
    };

    enum class ObservationPath : std::uint8_t
    {
        LineOfSightAwareness = 0,
        VictimAware = 1,
        MurderHearing = 2,
    };

    enum class ObservationObserverPolicy : std::uint8_t
    {
        AnyActor = 0,
        VanillaCrimeWitness = 1,
    };

    enum class ObservationReason : std::uint8_t
    {
        Observed = 0,
        InvalidQuery,
        ObserverKindRejected,
        ObserverIneligible,
        StaleActorSnapshot,
        CollisionUnavailable,
        CollisionGenerationMismatch,
        BlockedLineOfSight,
        AwarenessFailed,
    };

    struct ObservationVector
    {
        float x = 0.f;
        float y = 0.f;
        float z = 0.f;
    };

    struct AwarenessSettings
    {
        float sneakSkillMultiplier = 1.f;
        float sneakBootMultiplier = 1.f;
        float sneakDistanceBase = 1.f;
        float sneakDistanceMultiplier = 0.f;
        float sneakNoViewMultiplier = 1.f;
        float sneakViewMultiplier = 1.f;
        float fatigueBase = 1.f;
        float fatigueMultiplier = 0.f;
    };

    struct ObservationActorSnapshot
    {
        ObservationActorIdentity identity;
        ObservationVector position;
        ObservationVector forward;
        bool hasFacing = false;

        bool eligibilityKnown = false;
        bool awarenessInputsKnown = false;
        bool enabled = false;
        bool alive = false;
        bool conscious = false;
        bool sneaking = false;
        bool onGround = false;

        float sneakSkill = 0.f;
        float agility = 0.f;
        float luck = 0.f;
        float fatigueCurrent = 0.f;
        float fatigueMaximumModified = 0.f;
        float bootWeight = 0.f;
        float chameleon = 0.f;
        float invisibility = 0.f;
        float blind = 0.f;

        std::uint32_t migrationGeneration = 0;
        std::uint32_t authorityGeneration = 0;
        std::uint32_t snapshotGeneration = 0;
        std::uint64_t sampledAtMs = 0;
        ObservationAuthority authority = ObservationAuthority::ServerAuthoritative;
    };

    struct CollisionCellGeneration
    {
        std::string cellId;
        std::uint64_t generation = 0;

        bool operator==(const CollisionCellGeneration&) const = default;
    };

    struct CollisionObservation
    {
        bool available = false;
        bool clear = false;
        std::vector<CollisionCellGeneration> generations;
    };

    class ObservationCollisionBackend
    {
    public:
        virtual ~ObservationCollisionBackend() = default;
        virtual CollisionObservation lineOfSight(
            const std::vector<std::string>& cellIds, const ObservationVector& from, const ObservationVector& to) const
            = 0;
    };

    class AwarenessRollSource
    {
    public:
        virtual ~AwarenessRollSource() = default;
        virtual int nextRoll0To99() = 0;
    };

    struct ObservationQuery
    {
        std::string eventId;
        std::string cellId;
        ObservationActorSnapshot observer;
        ObservationActorSnapshot target;
        std::optional<ObservationActorIdentity> victim;
        ObservationPath path = ObservationPath::LineOfSightAwareness;
        ObservationObserverPolicy observerPolicy = ObservationObserverPolicy::AnyActor;
        ObservationAuthority eventAuthority = ObservationAuthority::ServerAuthoritative;
        std::uint64_t observedAtMs = 0;
        std::uint64_t maximumSnapshotAgeMs = 1000;
        std::vector<CollisionCellGeneration> collisionGenerations;
    };

    struct AwarenessCalculation
    {
        float targetConcealment = 0.f;
        float observerDetection = 0.f;
        float threshold = 0.f;
        bool targetBehindObserver = false;
        bool detected = false;
    };

    struct ObservationResult
    {
        bool observable = false;
        std::optional<bool> lineOfSight;
        std::optional<bool> awareness;
        std::optional<int> awarenessRoll;
        std::optional<float> awarenessThreshold;
        ObservationPath path = ObservationPath::LineOfSightAwareness;
        ObservationReason reason = ObservationReason::InvalidQuery;
        ObservationAuthority authority = ObservationAuthority::ServerAuthoritative;
        std::uint32_t observerMigrationGeneration = 0;
        std::uint32_t observerAuthorityGeneration = 0;
        std::uint32_t observerSnapshotGeneration = 0;
        std::uint32_t targetSnapshotGeneration = 0;
        std::vector<CollisionCellGeneration> collisionGenerations;
    };

    class ObservationService
    {
    public:
        static constexpr std::uint64_t AwarenessRollLifetimeMs = 5000;

        ObservationService(
            AwarenessSettings settings, const ObservationCollisionBackend& collision, AwarenessRollSource& rollSource);

        ObservationResult observe(const ObservationQuery& query);
        void invalidateObserver(const ObservationActorIdentity& observer);
        void clearAwarenessRolls();

        static float fatigueTerm(float current, float modifiedMaximum, const AwarenessSettings& settings);
        static AwarenessCalculation calculateAwareness(const ObservationActorSnapshot& target,
            const ObservationActorSnapshot& observer, const AwarenessSettings& settings, int roll);

    private:
        struct CachedRoll
        {
            int value = 0;
            std::uint64_t acquiredAtMs = 0;
            std::uint32_t migrationGeneration = 0;
            std::uint32_t authorityGeneration = 0;
        };

        int awarenessRoll(const ObservationActorSnapshot& observer, std::uint64_t nowMs);

        AwarenessSettings mSettings;
        const ObservationCollisionBackend& mCollision;
        AwarenessRollSource& mRollSource;
        std::unordered_map<ObservationActorIdentity, CachedRoll, ObservationActorIdentityHash> mAwarenessRolls;
    };
}

#endif
