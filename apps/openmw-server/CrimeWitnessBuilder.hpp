#ifndef OPENMW_SERVER_CRIMEWITNESSBUILDER_HPP
#define OPENMW_SERVER_CRIMEWITNESSBUILDER_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <components/openmw-mp/Base/BaseStructs.hpp>

#include "CrimeSemanticService.hpp"
#include "MechanicsSnapshotRegistry.hpp"

namespace mwmp
{
    enum class CrimeAlarmProvenance : std::uint8_t
    {
        Unavailable = 0,
        StaticContentBase,
        ValidatedActorAuthorityDelegated,
    };

    enum class CrimeFightProvenance : std::uint8_t
    {
        Unavailable = 0,
        StaticContentBase,
        ValidatedActorAuthorityDelegated,
    };

    enum class CrimeRelationshipProvenance : std::uint8_t
    {
        Unavailable = 0,
        ServerAuthoritative,
        ValidatedActorAuthorityDelegated,
    };

    /// Canonical server-world metadata for one possible witness. Mechanics
    /// values are resolved separately from MechanicsSnapshotRegistry.
    struct LiveCrimeWitnessActor
    {
        ObservationActorIdentity identity;
        std::string refId;
        std::string cellId;
        std::uint32_t migrationGeneration = 0;
        std::uint32_t authorityGeneration = 0;
        float bootWeight = 0.f;
        std::optional<std::int32_t> alarm;
        CrimeAlarmProvenance alarmProvenance = CrimeAlarmProvenance::Unavailable;
        std::optional<std::int32_t> fight;
        CrimeFightProvenance fightProvenance = CrimeFightProvenance::Unavailable;
        bool guard = false;
        CrimeWitnessRelationship relationship = CrimeWitnessRelationship::Unknown;
        CrimeRelationshipProvenance relationshipProvenance = CrimeRelationshipProvenance::Unavailable;
    };

    class LiveCrimeWitnessSource
    {
    public:
        virtual ~LiveCrimeWitnessSource() = default;

        virtual std::vector<LiveCrimeWitnessActor> actorsInCell(std::string_view cellId) const = 0;
        virtual std::optional<LiveCrimeWitnessActor> findActor(
            const ObservationActorIdentity& identity) const = 0;
    };

    struct CrimeWitnessBuildRequest
    {
        CellId eventCell;
        ObservationActorSnapshot offender;
        std::optional<ObservationActorIdentity> victim;
        float alarmRadius = 0.f;
        std::uint64_t observedAtMs = 0;
        std::uint64_t maximumSnapshotAgeMs = 1000;
    };

    enum class CrimeWitnessBuildReason : std::uint8_t
    {
        Included = 0,
        DuplicateIdentity,
        CanonicalKindRejected,
        OutsideAlarmRadius,
        MechanicsSnapshotMissing,
        MechanicsSnapshotStale,
        WrongCell,
        WrongMigrationGeneration,
        WrongAuthorityGeneration,
        ActorIneligible,
        AlarmUnavailable,
        AlarmInvalid,
        FightUnavailable,
        FightInvalid,
        InCombatWithVictim,
        PlayerFollower,
        RelationshipUnknown,
    };

    struct CrimeWitnessBuildDecision
    {
        ObservationActorIdentity identity;
        std::string refId;
        std::string cellId;
        CrimeWitnessBuildReason reason = CrimeWitnessBuildReason::CanonicalKindRejected;
        std::optional<std::int32_t> alarm;
        CrimeAlarmProvenance alarmProvenance = CrimeAlarmProvenance::Unavailable;
        std::optional<std::int32_t> fight;
        CrimeFightProvenance fightProvenance = CrimeFightProvenance::Unavailable;
        bool guard = false;
        CrimeWitnessRelationship relationship = CrimeWitnessRelationship::Unknown;
        CrimeRelationshipProvenance relationshipProvenance = CrimeRelationshipProvenance::Unavailable;
        std::uint32_t migrationGeneration = 0;
        std::uint32_t authorityGeneration = 0;
        std::optional<std::uint64_t> snapshotAgeMs;
        std::optional<float> distance;
    };

    struct CrimeWitnessBuildResult
    {
        std::vector<std::string> candidateCellIds;
        std::vector<CrimeWitnessCandidate> witnesses;
        std::vector<CrimeWitnessBuildDecision> decisions;
    };

    class CrimeWitnessBuilder
    {
    public:
        explicit CrimeWitnessBuilder(const MechanicsSnapshotRegistry& mechanicsSnapshots);

        CrimeWitnessBuildResult build(
            const CrimeWitnessBuildRequest& request, const LiveCrimeWitnessSource& source) const;

    private:
        const MechanicsSnapshotRegistry& mMechanicsSnapshots;
    };
}

#endif
