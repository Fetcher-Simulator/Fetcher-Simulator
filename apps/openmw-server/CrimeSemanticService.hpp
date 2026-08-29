#ifndef OPENMW_SERVER_CRIMESEMANTICSERVICE_HPP
#define OPENMW_SERVER_CRIMESEMANTICSERVICE_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "CrimeService.hpp"
#include "CrimeAggression.hpp"
#include "ObservationService.hpp"

namespace mwmp
{
    inline constexpr std::uint16_t CrimeSemanticVersion = 2;
    inline constexpr std::size_t MaximumCrimeWitnesses = 1024;

    enum class CrimeSemanticError : std::uint16_t
    {
        None = 0,
        Unauthorized,
        InvalidIntent,
        DuplicateConflict,
        StaleRevision,
        StateOverflow,
        CorruptStoredResult,
    };

    /// Canonical, already-authenticated gameplay cause. Producers are wired in
    /// later phases; this type never accepts client-authored seen/reported data.
    struct CrimeIntent
    {
        std::uint16_t version = CrimeSemanticVersion;
        std::string eventId;
        std::string source;
        CrimeType type = CrimeType::Theft;
        std::string cellId;
        ObservationActorSnapshot offender;
        std::optional<ObservationActorIdentity> victim;
        bool victimAware = false;
        std::int64_t value = 0;
        std::uint64_t observedAtMs = 0;
        std::uint64_t maximumSnapshotAgeMs = 1000;
        std::vector<CollisionCellGeneration> collisionGenerations;
    };

    enum class CrimeWitnessRelationship : std::uint8_t
    {
        Eligible = 0,
        InCombatWithVictim,
        PlayerFollower,
        Unknown,
    };

    /// Built from server state (or an explicitly validated delegated snapshot),
    /// never directly from a client-supplied witness list.
    struct CrimeWitnessCandidate
    {
        ObservationActorSnapshot actor;
        std::int32_t alarm = 0;
        std::int32_t fight = 0;
        bool guard = false;
        CrimeWitnessRelationship relationship = CrimeWitnessRelationship::Unknown;
        ObservationAuthority relationshipAuthority = ObservationAuthority::ServerAuthoritative;
    };

    enum class CrimeWitnessEligibility : std::uint8_t
    {
        Eligible = 0,
        OutsideAlarmRadius,
        CanonicalKindRejected,
        ActorIneligible,
        InCombatWithVictim,
        PlayerFollower,
        RelationshipUnknown,
    };

    struct CrimeWitnessResult
    {
        ObservationActorIdentity identity;
        bool candidate = false;
        bool victim = false;
        CrimeWitnessEligibility eligibility = CrimeWitnessEligibility::RelationshipUnknown;
        std::int32_t alarm = 0;
        std::int32_t fight = 0;
        bool guard = false;
        ObservationAuthority relationshipAuthority = ObservationAuthority::ServerAuthoritative;
        std::optional<ObservationResult> observation;
        bool perceived = false;
        bool reportCapable = false;
        bool reported = false;
        CrimeAggressionResult aggression;
    };

    struct CrimeSemanticResult
    {
        std::uint16_t version = CrimeSemanticVersion;
        std::string eventId;
        CrimeType type = CrimeType::Theft;
        bool accepted = false;
        CrimeSemanticError error = CrimeSemanticError::None;
        std::vector<CrimeWitnessResult> witnesses;
        bool crimeSeen = false;
        bool reportingStageRun = false;
        bool bountyApplied = false;
        std::int32_t bountyDelta = 0;
        bool currentCrimeIdAdvanced = false;
        PlayerCrimeState state;
    };

    struct CrimePolicy
    {
        float alarmRadius = 0.f;
        float theftBountyMultiplier = 1.f;
        std::int32_t pickpocketBounty = 0;
        std::int32_t trespassBounty = 0;
        std::int32_t assaultBounty = 0;
        std::int32_t murderBounty = 0;
        std::int32_t werewolfBounty = 0;
        std::int32_t reportingAlarmThreshold = 100;
        CrimeAggressionPolicy aggression;
    };

    class CrimeSemanticService
    {
    public:
        struct Context
        {
            std::int64_t accountId = 0;
            std::int64_t characterId = 0;
            std::uint32_t playerGuid = 0;
            CrimeCommitFailurePoint failurePoint = CrimeCommitFailurePoint::None;
            bool deferCommit = false;
            std::optional<PlayerCrimeState> startingState;
        };

        struct Outcome
        {
            CrimeSemanticResult result;
            bool replayed = false;
            bool committed = false;
            std::optional<CrimeMutationCommit> pendingCommit;
        };

        CrimeSemanticService(PlayerDatabase& database, CrimeService& crimeService,
            ObservationService& observationService, CrimePolicy policy);

        Outcome evaluate(
            const CrimeIntent& intent, std::vector<CrimeWitnessCandidate> witnesses, const Context& context);

    private:
        PlayerDatabase& mDatabase;
        CrimeService& mCrimeService;
        ObservationService& mObservationService;
        CrimePolicy mPolicy;
    };
}

#endif
