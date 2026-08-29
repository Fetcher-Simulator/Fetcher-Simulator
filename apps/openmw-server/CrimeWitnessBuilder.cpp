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

    ObservationActorIdentity combatTargetIdentity(const MechanicsSnapshot& snapshot)
    {
        ObservationActorIdentity result;
        switch (snapshot.combatTargetKind)
        {
            case MechanicsSubjectKind::Player:
                result.kind = ObservationActorKind::Player;
                result.playerGuid = snapshot.combatTargetPlayerGuid;
                break;
            case MechanicsSubjectKind::Npc:
                result.kind = ObservationActorKind::Npc;
                result.actorInstanceId = snapshot.combatTargetActorInstanceId;
                break;
            case MechanicsSubjectKind::Creature:
                result.kind = ObservationActorKind::Creature;
                result.actorInstanceId = snapshot.combatTargetActorInstanceId;
                break;
        }
        return result;
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
            decision.refId = actor.refId;
            decision.cellId = actor.cellId;
            decision.alarm = actor.alarm;
            decision.alarmProvenance = actor.alarmProvenance;
            decision.fight = actor.fight;
            decision.fightProvenance = actor.fightProvenance;
            decision.guard = actor.guard;
            decision.relationship = actor.relationship;
            decision.relationshipProvenance = actor.relationshipProvenance;
            decision.migrationGeneration = actor.migrationGeneration;
            decision.authorityGeneration = actor.authorityGeneration;

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
                    decision.snapshotAgeMs = request.observedAtMs - accepted->receivedAtMs;
                    const bool victim = request.victim && actor.identity == *request.victim;
                    const float actorDistanceSquared = distanceSquared(request.offender.position, snapshot.position);
                    if (std::isfinite(actorDistanceSquared) && actorDistanceSquared >= 0.f)
                        decision.distance = std::sqrt(actorDistanceSquared);

                    std::optional<std::int32_t> alarm = actor.alarm;
                    CrimeAlarmProvenance alarmProvenance = actor.alarmProvenance;
                    std::optional<std::int32_t> fight = actor.fight;
                    CrimeFightProvenance fightProvenance = actor.fightProvenance;
                    CrimeWitnessRelationship relationship = actor.relationship;
                    CrimeRelationshipProvenance relationshipProvenance = actor.relationshipProvenance;
                    const MechanicsSnapshot& mechanics = accepted->snapshot;
                    if ((mechanics.witnessStateFlags & MechanicsWitnessEffectiveAlarmKnown) != 0)
                    {
                        alarm = mechanics.effectiveAlarm;
                        alarmProvenance = CrimeAlarmProvenance::ValidatedActorAuthorityDelegated;
                    }
                    if ((mechanics.witnessStateFlags & MechanicsWitnessEffectiveFightKnown) != 0)
                    {
                        fight = mechanics.effectiveFight;
                        fightProvenance = CrimeFightProvenance::ValidatedActorAuthorityDelegated;
                    }
                    if ((mechanics.witnessStateFlags & MechanicsWitnessRelationshipKnown) != 0)
                    {
                        relationshipProvenance
                            = CrimeRelationshipProvenance::ValidatedActorAuthorityDelegated;
                        if ((mechanics.witnessStateFlags & MechanicsWitnessPlayerFollower) != 0)
                            relationship = CrimeWitnessRelationship::PlayerFollower;
                        else if (request.victim
                            && (mechanics.witnessStateFlags & MechanicsWitnessHasCombatTarget) != 0
                            && combatTargetIdentity(mechanics) == *request.victim)
                            relationship = CrimeWitnessRelationship::InCombatWithVictim;
                        else
                            relationship = CrimeWitnessRelationship::Eligible;
                    }
                    decision.alarm = alarm;
                    decision.alarmProvenance = alarmProvenance;
                    decision.fight = fight;
                    decision.fightProvenance = fightProvenance;
                    decision.relationship = relationship;
                    decision.relationshipProvenance = relationshipProvenance;

                    if (!finitePosition(snapshot.position) || !std::isfinite(actorDistanceSquared)
                        || (!victim && actorDistanceSquared > radiusSquared))
                        decision.reason = CrimeWitnessBuildReason::OutsideAlarmRadius;
                    else if (!snapshot.eligibilityKnown || !snapshot.enabled || !snapshot.alive
                        || !snapshot.conscious)
                        decision.reason = CrimeWitnessBuildReason::ActorIneligible;
                    else if (!alarm || alarmProvenance == CrimeAlarmProvenance::Unavailable)
                        decision.reason = CrimeWitnessBuildReason::AlarmUnavailable;
                    else if (*alarm < 0 || *alarm > 100)
                        decision.reason = CrimeWitnessBuildReason::AlarmInvalid;
                    else if (!fight || fightProvenance == CrimeFightProvenance::Unavailable)
                        decision.reason = CrimeWitnessBuildReason::FightUnavailable;
                    else if (*fight < 0 || *fight > 100)
                        decision.reason = CrimeWitnessBuildReason::FightInvalid;
                    else if (relationship == CrimeWitnessRelationship::InCombatWithVictim)
                        decision.reason = CrimeWitnessBuildReason::InCombatWithVictim;
                    else if (relationship == CrimeWitnessRelationship::PlayerFollower)
                        decision.reason = CrimeWitnessBuildReason::PlayerFollower;
                    else if (relationship != CrimeWitnessRelationship::Eligible
                        || relationshipProvenance == CrimeRelationshipProvenance::Unavailable)
                        decision.reason = CrimeWitnessBuildReason::RelationshipUnknown;
                    else
                    {
                        CrimeWitnessCandidate witness;
                        witness.actor = std::move(snapshot);
                        witness.alarm = *alarm;
                        witness.fight = *fight;
                        witness.guard = actor.guard;
                        witness.relationship = relationship;
                        witness.relationshipAuthority
                            = relationshipAuthority(relationshipProvenance);
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
