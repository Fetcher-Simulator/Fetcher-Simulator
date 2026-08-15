#include "CrimeWitnessBuilder.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>
#include <unordered_map>

#include "CollisionCellOwnership.hpp"
#include "LiveObservationRuntime.hpp"

namespace
{
    using namespace mwmp;

    std::tuple<std::uint8_t, std::uint32_t, ActorInstanceId> identityKey(
        const ObservationActorIdentity& identity)
    {
        return { static_cast<std::uint8_t>(identity.kind), identity.playerGuid, identity.actorInstanceId };
    }

    MechanicsSubjectKey mechanicsSubject(const ObservationActorIdentity& identity)
    {
        switch (identity.kind)
        {
            case ObservationActorKind::Player:
                return { MechanicsSubjectKind::Player, identity.playerGuid, 0 };
            case ObservationActorKind::Npc:
                return { MechanicsSubjectKind::Npc, 0, identity.actorInstanceId };
            case ObservationActorKind::Creature:
                return { MechanicsSubjectKind::Creature, 0, identity.actorInstanceId };
        }
        return {};
    }

    bool finitePosition(const ObservationVector& position)
    {
        return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z);
    }

    float distanceSquared(const ObservationVector& left, const ObservationVector& right)
    {
        const float x = left.x - right.x;
        const float y = left.y - right.y;
        const float z = left.z - right.z;
        return x * x + y * y + z * z;
    }

    ObservationAuthority relationshipAuthority(CrimeRelationshipProvenance provenance)
    {
        return provenance == CrimeRelationshipProvenance::ValidatedActorAuthorityDelegated
            ? ObservationAuthority::ActorAuthorityDelegated
            : ObservationAuthority::ServerAuthoritative;
    }
}

namespace mwmp
{
    CrimeWitnessBuilder::CrimeWitnessBuilder(const MechanicsSnapshotRegistry& mechanicsSnapshots)
        : mMechanicsSnapshots(mechanicsSnapshots)
    {
    }

    CrimeWitnessBuildResult CrimeWitnessBuilder::build(
        const CrimeWitnessBuildRequest& request, const LiveCrimeWitnessSource& source) const
    {
        CrimeWitnessBuildResult result;
        if (!request.offender.identity.isValid() || request.offender.identity.kind != ObservationActorKind::Player
            || !finitePosition(request.offender.position) || !std::isfinite(request.alarmRadius)
            || request.alarmRadius <= 0.f || request.observedAtMs == 0)
            return result;

        Position eventPosition;
        eventPosition.pos[0] = request.offender.position.x;
        eventPosition.pos[1] = request.offender.position.y;
        eventPosition.pos[2] = request.offender.position.z;
        result.candidateCellIds
            = collisionCellsForPlayer(request.eventCell, eventPosition, request.alarmRadius);

        struct SourcedActor
        {
            std::string enumeratedCellId;
            LiveCrimeWitnessActor actor;
        };
        std::vector<SourcedActor> actors;
        for (const std::string& cellId : result.candidateCellIds)
        {
            for (LiveCrimeWitnessActor actor : source.actorsInCell(cellId))
                actors.push_back({ cellId, std::move(actor) });
        }

        if (request.victim)
        {
            const bool alreadyEnumerated = std::any_of(actors.begin(), actors.end(), [&](const SourcedActor& actor) {
                return actor.actor.identity == *request.victim;
            });
            if (!alreadyEnumerated)
            {
                if (std::optional<LiveCrimeWitnessActor> victim = source.findActor(*request.victim))
                    actors.push_back({ victim->cellId, std::move(*victim) });
            }
        }

        std::sort(actors.begin(), actors.end(), [](const SourcedActor& left, const SourcedActor& right) {
            return std::tuple(identityKey(left.actor.identity), left.enumeratedCellId, left.actor.cellId)
                < std::tuple(identityKey(right.actor.identity), right.enumeratedCellId, right.actor.cellId);
        });

        std::unordered_map<ObservationActorIdentity, std::size_t, ObservationActorIdentityHash> identityCounts;
        for (const SourcedActor& sourced : actors)
            ++identityCounts[sourced.actor.identity];

        const float radiusSquared = request.alarmRadius * request.alarmRadius;
        for (const SourcedActor& sourced : actors)
        {
            const LiveCrimeWitnessActor& actor = sourced.actor;
            CrimeWitnessBuildDecision decision;
            decision.identity = actor.identity;
            decision.alarmProvenance = actor.alarmProvenance;
            decision.relationshipProvenance = actor.relationshipProvenance;

            if (identityCounts[actor.identity] != 1)
                decision.reason = CrimeWitnessBuildReason::DuplicateIdentity;
            else if (!actor.identity.isValid() || actor.identity.kind != ObservationActorKind::Npc)
                decision.reason = CrimeWitnessBuildReason::CanonicalKindRejected;
            else
            {
                const AcceptedMechanicsSnapshot* accepted
                    = mMechanicsSnapshots.find(mechanicsSubject(actor.identity));
                if (accepted == nullptr)
                    decision.reason = CrimeWitnessBuildReason::MechanicsSnapshotMissing;
                else if (request.observedAtMs < accepted->receivedAtMs
                    || request.observedAtMs - accepted->receivedAtMs > request.maximumSnapshotAgeMs)
                    decision.reason = CrimeWitnessBuildReason::MechanicsSnapshotStale;
                else if (actor.cellId != sourced.enumeratedCellId
                    || accepted->snapshot.cellId != actor.cellId)
                    decision.reason = CrimeWitnessBuildReason::WrongCell;
                else if (actor.migrationGeneration == 0
                    || accepted->snapshot.migrationGeneration != actor.migrationGeneration)
                    decision.reason = CrimeWitnessBuildReason::WrongMigrationGeneration;
                else if (actor.authorityGeneration == 0
                    || accepted->snapshot.authorityGeneration != actor.authorityGeneration)
                    decision.reason = CrimeWitnessBuildReason::WrongAuthorityGeneration;
                else
                {
                    ObservationActorSnapshot snapshot = makeLiveObservationSnapshot(*accepted, actor.bootWeight);
                    const bool victim = request.victim && actor.identity == *request.victim;
                    const float actorDistanceSquared = distanceSquared(request.offender.position, snapshot.position);
                    if (!finitePosition(snapshot.position) || !std::isfinite(actorDistanceSquared)
                        || (!victim && actorDistanceSquared > radiusSquared))
                        decision.reason = CrimeWitnessBuildReason::OutsideAlarmRadius;
                    else if (!snapshot.eligibilityKnown || !snapshot.enabled || !snapshot.alive
                        || !snapshot.conscious)
                        decision.reason = CrimeWitnessBuildReason::ActorIneligible;
                    else if (!actor.alarm || actor.alarmProvenance == CrimeAlarmProvenance::Unavailable)
                        decision.reason = CrimeWitnessBuildReason::AlarmUnavailable;
                    else if (*actor.alarm < 0 || *actor.alarm > 100)
                        decision.reason = CrimeWitnessBuildReason::AlarmInvalid;
                    else if (actor.relationship == CrimeWitnessRelationship::InCombatWithVictim)
                        decision.reason = CrimeWitnessBuildReason::InCombatWithVictim;
                    else if (actor.relationship == CrimeWitnessRelationship::PlayerFollower)
                        decision.reason = CrimeWitnessBuildReason::PlayerFollower;
                    else if (actor.relationship != CrimeWitnessRelationship::Eligible
                        || actor.relationshipProvenance == CrimeRelationshipProvenance::Unavailable)
                        decision.reason = CrimeWitnessBuildReason::RelationshipUnknown;
                    else
                    {
                        CrimeWitnessCandidate witness;
                        witness.actor = std::move(snapshot);
                        witness.alarm = *actor.alarm;
                        witness.relationship = actor.relationship;
                        witness.relationshipAuthority
                            = relationshipAuthority(actor.relationshipProvenance);
                        result.witnesses.push_back(std::move(witness));
                        decision.reason = CrimeWitnessBuildReason::Included;
                    }
                }
            }
            result.decisions.push_back(std::move(decision));
        }

        return result;
    }
}
