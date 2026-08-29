#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <utility>

#include <apps/openmw-server/CrimeWitnessBuilder.hpp>

namespace
{
    using namespace mwmp;

    ObservationActorIdentity npc(std::uint32_t refNum)
    {
        ObservationActorIdentity result;
        result.kind = ObservationActorKind::Npc;
        result.actorInstanceId = packActorInstanceKey({ ActorKeyKind::VanillaRefNum, refNum });
        return result;
    }

    ObservationActorIdentity player(std::uint32_t guid)
    {
        ObservationActorIdentity result;
        result.kind = ObservationActorKind::Player;
        result.playerGuid = guid;
        return result;
    }

    struct Source final : LiveCrimeWitnessSource
    {
        std::vector<LiveCrimeWitnessActor> actorsInCell(std::string_view cellId) const override
        {
            const auto found = cells.find(std::string(cellId));
            return found == cells.end() ? std::vector<LiveCrimeWitnessActor>() : found->second;
        }

        std::optional<LiveCrimeWitnessActor> findActor(
            const ObservationActorIdentity& identity) const override
        {
            for (const auto& [cellId, actors] : cells)
            {
                (void)cellId;
                for (const LiveCrimeWitnessActor& actor : actors)
                {
                    if (actor.identity == identity)
                        return actor;
                }
            }
            return std::nullopt;
        }

        std::map<std::string, std::vector<LiveCrimeWitnessActor>> cells;
    };

    MechanicsSnapshot snapshot(const ObservationActorIdentity& identity, std::string cellId,
        float x = 0.f, float y = 0.f, std::uint8_t stateFlags = MechanicsEnabled | MechanicsAlive | MechanicsConscious)
    {
        MechanicsSnapshot result;
        result.kind = identity.kind == ObservationActorKind::Player
            ? MechanicsSubjectKind::Player : MechanicsSubjectKind::Npc;
        result.playerGuid = identity.playerGuid;
        result.actorInstanceId = identity.actorInstanceId;
        result.cellId = std::move(cellId);
        result.position.pos[0] = x;
        result.position.pos[1] = y;
        result.stateFlags = stateFlags;
        result.fatigueMaximumModified = 100.f;
        result.fatigueCurrent = 100.f;
        result.migrationGeneration = 1;
        result.authorityGeneration = 1;
        result.snapshotSequence = 1;
        return result;
    }

    void accept(MechanicsSnapshotRegistry& registry, MechanicsSnapshot value, std::uint64_t receivedAtMs = 1000)
    {
        MechanicsSnapshotExpectation expectation;
        expectation.subject = { value.kind, value.playerGuid, value.actorInstanceId };
        expectation.cellId = value.cellId;
        expectation.migrationGeneration = value.migrationGeneration;
        expectation.authorityGeneration = value.authorityGeneration;
        expectation.authenticatedPlayerGuid = value.playerGuid;
        expectation.actorSenderEntitled = value.kind != MechanicsSubjectKind::Player;
        ASSERT_EQ(registry.accept(value, expectation, receivedAtMs), MechanicsSnapshotError::None);
    }

    LiveCrimeWitnessActor actor(ObservationActorIdentity identity, std::string cellId)
    {
        LiveCrimeWitnessActor result;
        result.identity = identity;
        result.cellId = std::move(cellId);
        result.migrationGeneration = 1;
        result.authorityGeneration = 1;
        result.alarm = 100;
        result.alarmProvenance = CrimeAlarmProvenance::StaticContentBase;
        result.fight = 30;
        result.fightProvenance = CrimeFightProvenance::StaticContentBase;
        result.relationship = CrimeWitnessRelationship::Eligible;
        result.relationshipProvenance = CrimeRelationshipProvenance::ServerAuthoritative;
        return result;
    }

    CrimeWitnessBuildRequest interiorRequest()
    {
        CrimeWitnessBuildRequest request;
        request.eventCell.cellName = "Balmora";
        request.offender.identity = player(42);
        request.offender.position = { 0.f, 0.f, 0.f };
        request.alarmRadius = 100.f;
        request.observedAtMs = 1500;
        request.maximumSnapshotAgeMs = 1000;
        return request;
    }

    const CrimeWitnessBuildDecision& decisionFor(
        const CrimeWitnessBuildResult& result, const ObservationActorIdentity& identity)
    {
        const auto found = std::find_if(result.decisions.begin(), result.decisions.end(), [&](const auto& decision) {
            return decision.identity == identity;
        });
        EXPECT_NE(found, result.decisions.end());
        return *found;
    }
}

TEST(CrimeWitnessBuilder, IncludesFreshEligibleNpcInSameInteriorAndExcludesOutsideRadius)
{
    MechanicsSnapshotRegistry registry;
    Source source;
    const auto nearNpc = npc(1);
    const auto farNpc = npc(2);
    source.cells["Balmora"] = { actor(nearNpc, "Balmora"), actor(farNpc, "Balmora") };
    accept(registry, snapshot(nearNpc, "Balmora", 0.f, 50.f));
    accept(registry, snapshot(farNpc, "Balmora", 0.f, 101.f));

    const auto result = CrimeWitnessBuilder(registry).build(interiorRequest(), source);

    ASSERT_EQ(result.candidateCellIds, std::vector<std::string>{ "Balmora" });
    ASSERT_EQ(result.witnesses.size(), 1u);
    EXPECT_EQ(result.witnesses[0].actor.identity, nearNpc);
    EXPECT_EQ(decisionFor(result, farNpc).reason, CrimeWitnessBuildReason::OutsideAlarmRadius);
}

TEST(CrimeWitnessBuilder, ExteriorNeighborIsEnumeratedOnlyWhenRadiusCrossesBoundary)
{
    MechanicsSnapshotRegistry registry;
    Source source;
    const auto observer = npc(1);
    source.cells["EXT:1,0"] = { actor(observer, "EXT:1,0") };
    accept(registry, snapshot(observer, "EXT:1,0", 8200.f, 100.f));

    CrimeWitnessBuildRequest request = interiorRequest();
    request.eventCell = {};
    request.eventCell.isExterior = true;
    request.eventCell.gridX = 0;
    request.eventCell.gridY = 0;
    request.offender.position = { 8180.f, 100.f, 0.f };
    request.alarmRadius = 20.f;
    const auto crossing = CrimeWitnessBuilder(registry).build(request, source);
    EXPECT_EQ(crossing.witnesses.size(), 1u);
    EXPECT_NE(std::find(crossing.candidateCellIds.begin(), crossing.candidateCellIds.end(), "EXT:1,0"),
        crossing.candidateCellIds.end());

    request.alarmRadius = 10.f;
    const auto notCrossing = CrimeWitnessBuilder(registry).build(request, source);
    EXPECT_TRUE(notCrossing.witnesses.empty());
    EXPECT_EQ(notCrossing.candidateCellIds, std::vector<std::string>{ "EXT:0,0" });
}

TEST(CrimeWitnessBuilder, DeduplicatesCanonicalActorIdentity)
{
    MechanicsSnapshotRegistry registry;
    Source source;
    const auto observer = npc(1);
    source.cells["Balmora"] = { actor(observer, "Balmora"), actor(observer, "Balmora") };
    accept(registry, snapshot(observer, "Balmora", 0.f, 10.f));

    const auto result = CrimeWitnessBuilder(registry).build(interiorRequest(), source);
    EXPECT_TRUE(result.witnesses.empty());
    EXPECT_EQ(std::count_if(result.decisions.begin(), result.decisions.end(), [](const auto& decision) {
        return decision.reason == CrimeWitnessBuildReason::DuplicateIdentity;
    }), 2);
}

TEST(CrimeWitnessBuilder, RejectsStaleAndGenerationMismatchedMechanics)
{
    MechanicsSnapshotRegistry registry;
    Source source;
    const auto stale = npc(1);
    const auto migrated = npc(2);
    const auto authorityChanged = npc(3);
    source.cells["Balmora"] = {
        actor(stale, "Balmora"), actor(migrated, "Balmora"), actor(authorityChanged, "Balmora")
    };
    source.cells["Balmora"][1].migrationGeneration = 2;
    source.cells["Balmora"][2].authorityGeneration = 2;
    accept(registry, snapshot(stale, "Balmora", 0.f, 10.f), 1);
    accept(registry, snapshot(migrated, "Balmora", 0.f, 10.f));
    accept(registry, snapshot(authorityChanged, "Balmora", 0.f, 10.f));

    const auto result = CrimeWitnessBuilder(registry).build(interiorRequest(), source);
    EXPECT_TRUE(result.witnesses.empty());
    EXPECT_EQ(decisionFor(result, stale).reason, CrimeWitnessBuildReason::MechanicsSnapshotStale);
    EXPECT_EQ(decisionFor(result, migrated).reason, CrimeWitnessBuildReason::WrongMigrationGeneration);
    EXPECT_EQ(decisionFor(result, authorityChanged).reason, CrimeWitnessBuildReason::WrongAuthorityGeneration);
}

TEST(CrimeWitnessBuilder, RejectsWrongCellAndCanonicalPlayerProxy)
{
    MechanicsSnapshotRegistry registry;
    Source source;
    const auto wrongCell = npc(1);
    const auto remoteHuman = player(77);
    source.cells["Balmora"] = { actor(wrongCell, "Ald-ruhn"), actor(remoteHuman, "Balmora") };
    accept(registry, snapshot(wrongCell, "Ald-ruhn", 0.f, 10.f));

    const auto result = CrimeWitnessBuilder(registry).build(interiorRequest(), source);
    EXPECT_TRUE(result.witnesses.empty());
    EXPECT_EQ(decisionFor(result, wrongCell).reason, CrimeWitnessBuildReason::WrongCell);
    EXPECT_EQ(decisionFor(result, remoteHuman).reason, CrimeWitnessBuildReason::CanonicalKindRejected);
}

TEST(CrimeWitnessBuilder, RejectsDeadDisabledAndUnconsciousActors)
{
    MechanicsSnapshotRegistry registry;
    Source source;
    const auto dead = npc(1);
    const auto disabled = npc(2);
    const auto unconscious = npc(3);
    source.cells["Balmora"] = { actor(dead, "Balmora"), actor(disabled, "Balmora"), actor(unconscious, "Balmora") };
    accept(registry, snapshot(dead, "Balmora", 0.f, 10.f, MechanicsEnabled | MechanicsConscious));
    accept(registry, snapshot(disabled, "Balmora", 0.f, 10.f, MechanicsAlive | MechanicsConscious));
    accept(registry, snapshot(unconscious, "Balmora", 0.f, 10.f, MechanicsEnabled | MechanicsAlive));

    const auto result = CrimeWitnessBuilder(registry).build(interiorRequest(), source);
    EXPECT_TRUE(result.witnesses.empty());
    for (const auto& decision : result.decisions)
        EXPECT_EQ(decision.reason, CrimeWitnessBuildReason::ActorIneligible);
}

TEST(CrimeWitnessBuilder, RelationshipProvenanceFailsClosed)
{
    MechanicsSnapshotRegistry registry;
    Source source;
    const auto combat = npc(1);
    const auto follower = npc(2);
    const auto unknown = npc(3);
    source.cells["Balmora"] = { actor(combat, "Balmora"), actor(follower, "Balmora"), actor(unknown, "Balmora") };
    source.cells["Balmora"][0].relationship = CrimeWitnessRelationship::InCombatWithVictim;
    source.cells["Balmora"][1].relationship = CrimeWitnessRelationship::PlayerFollower;
    source.cells["Balmora"][2].relationship = CrimeWitnessRelationship::Unknown;
    source.cells["Balmora"][2].relationshipProvenance = CrimeRelationshipProvenance::Unavailable;
    accept(registry, snapshot(combat, "Balmora", 0.f, 10.f));
    accept(registry, snapshot(follower, "Balmora", 0.f, 10.f));
    accept(registry, snapshot(unknown, "Balmora", 0.f, 10.f));

    const auto result = CrimeWitnessBuilder(registry).build(interiorRequest(), source);
    EXPECT_TRUE(result.witnesses.empty());
    EXPECT_EQ(decisionFor(result, combat).reason, CrimeWitnessBuildReason::InCombatWithVictim);
    EXPECT_EQ(decisionFor(result, follower).reason, CrimeWitnessBuildReason::PlayerFollower);
    EXPECT_EQ(decisionFor(result, unknown).reason, CrimeWitnessBuildReason::RelationshipUnknown);
}

TEST(CrimeWitnessBuilder, ValidatedAtomicWitnessStateOverridesStaticFallback)
{
    MechanicsSnapshotRegistry registry;
    Source source;
    const auto safe = npc(1);
    const auto follower = npc(2);
    const auto combat = npc(3);
    const auto victim = npc(4);
    source.cells["Balmora"] = {
        actor(safe, "Balmora"), actor(follower, "Balmora"), actor(combat, "Balmora"), actor(victim, "Balmora")
    };
    for (LiveCrimeWitnessActor& candidate : source.cells["Balmora"])
    {
        candidate.alarm = 5;
        candidate.relationship = CrimeWitnessRelationship::Unknown;
        candidate.relationshipProvenance = CrimeRelationshipProvenance::Unavailable;
    }

    auto safeSnapshot = snapshot(safe, "Balmora", 0.f, 10.f);
    safeSnapshot.witnessStateFlags = MechanicsWitnessRelationshipKnown
        | MechanicsWitnessEffectiveAlarmKnown | MechanicsWitnessEffectiveFightKnown;
    safeSnapshot.effectiveAlarm = 100;
    safeSnapshot.effectiveFight = 75;
    accept(registry, safeSnapshot);

    auto followerSnapshot = snapshot(follower, "Balmora", 0.f, 10.f);
    followerSnapshot.witnessStateFlags = MechanicsWitnessRelationshipKnown
        | MechanicsWitnessPlayerFollower | MechanicsWitnessEffectiveAlarmKnown;
    followerSnapshot.effectiveAlarm = 100;
    accept(registry, followerSnapshot);

    auto combatSnapshot = snapshot(combat, "Balmora", 0.f, 10.f);
    combatSnapshot.witnessStateFlags = MechanicsWitnessRelationshipKnown
        | MechanicsWitnessHasCombatTarget | MechanicsWitnessEffectiveAlarmKnown;
    combatSnapshot.effectiveAlarm = 100;
    combatSnapshot.combatTargetKind = MechanicsSubjectKind::Npc;
    combatSnapshot.combatTargetActorInstanceId = victim.actorInstanceId;
    accept(registry, combatSnapshot);

    auto victimSnapshot = snapshot(victim, "Balmora", 0.f, 10.f);
    victimSnapshot.witnessStateFlags = MechanicsWitnessRelationshipKnown
        | MechanicsWitnessEffectiveAlarmKnown;
    victimSnapshot.effectiveAlarm = 50;
    accept(registry, victimSnapshot);

    CrimeWitnessBuildRequest request = interiorRequest();
    request.victim = victim;
    const auto result = CrimeWitnessBuilder(registry).build(request, source);

    ASSERT_EQ(result.witnesses.size(), 2u);
    EXPECT_EQ(result.witnesses[0].actor.identity, safe);
    EXPECT_EQ(result.witnesses[0].alarm, 100);
    EXPECT_EQ(result.witnesses[0].fight, 75);
    EXPECT_EQ(decisionFor(result, safe).alarmProvenance,
        CrimeAlarmProvenance::ValidatedActorAuthorityDelegated);
    EXPECT_EQ(decisionFor(result, safe).relationshipProvenance,
        CrimeRelationshipProvenance::ValidatedActorAuthorityDelegated);
    EXPECT_EQ(decisionFor(result, safe).fightProvenance,
        CrimeFightProvenance::ValidatedActorAuthorityDelegated);
    EXPECT_EQ(decisionFor(result, follower).reason, CrimeWitnessBuildReason::PlayerFollower);
    EXPECT_EQ(decisionFor(result, combat).reason, CrimeWitnessBuildReason::InCombatWithVictim);
}

TEST(CrimeWitnessBuilder, AlarmRequiresExplicitValidProvenance)
{
    MechanicsSnapshotRegistry registry;
    Source source;
    const auto unavailable = npc(1);
    const auto invalid = npc(2);
    const auto baseRecord = npc(3);
    source.cells["Balmora"]
        = { actor(unavailable, "Balmora"), actor(invalid, "Balmora"), actor(baseRecord, "Balmora") };
    source.cells["Balmora"][0].alarm.reset();
    source.cells["Balmora"][0].alarmProvenance = CrimeAlarmProvenance::Unavailable;
    source.cells["Balmora"][1].alarm = 101;
    source.cells["Balmora"][2].alarm = 75;
    accept(registry, snapshot(unavailable, "Balmora", 0.f, 10.f));
    accept(registry, snapshot(invalid, "Balmora", 0.f, 10.f));
    accept(registry, snapshot(baseRecord, "Balmora", 0.f, 10.f));

    const auto result = CrimeWitnessBuilder(registry).build(interiorRequest(), source);
    ASSERT_EQ(result.witnesses.size(), 1u);
    EXPECT_EQ(result.witnesses[0].actor.identity, baseRecord);
    EXPECT_EQ(result.witnesses[0].alarm, 75);
    EXPECT_EQ(decisionFor(result, unavailable).reason, CrimeWitnessBuildReason::AlarmUnavailable);
    EXPECT_EQ(decisionFor(result, invalid).reason, CrimeWitnessBuildReason::AlarmInvalid);
}

TEST(CrimeWitnessBuilder, FightRequiresExplicitValidProvenance)
{
    MechanicsSnapshotRegistry registry;
    Source source;
    const auto unavailable = npc(1);
    const auto invalid = npc(2);
    const auto baseRecord = npc(3);
    source.cells["Balmora"]
        = { actor(unavailable, "Balmora"), actor(invalid, "Balmora"), actor(baseRecord, "Balmora") };
    source.cells["Balmora"][0].fight.reset();
    source.cells["Balmora"][0].fightProvenance = CrimeFightProvenance::Unavailable;
    source.cells["Balmora"][1].fight = 101;
    source.cells["Balmora"][2].fight = 30;
    accept(registry, snapshot(unavailable, "Balmora", 0.f, 10.f));
    accept(registry, snapshot(invalid, "Balmora", 0.f, 10.f));
    accept(registry, snapshot(baseRecord, "Balmora", 0.f, 10.f));

    const auto result = CrimeWitnessBuilder(registry).build(interiorRequest(), source);
    ASSERT_EQ(result.witnesses.size(), 1u);
    EXPECT_EQ(result.witnesses[0].actor.identity, baseRecord);
    EXPECT_EQ(result.witnesses[0].fight, 30);
    EXPECT_EQ(decisionFor(result, unavailable).reason, CrimeWitnessBuildReason::FightUnavailable);
    EXPECT_EQ(decisionFor(result, invalid).reason, CrimeWitnessBuildReason::FightInvalid);
}

TEST(CrimeWitnessBuilder, CanonicalVictimCanBeIncludedOutsideOrdinaryRadius)
{
    MechanicsSnapshotRegistry registry;
    Source source;
    const auto victim = npc(1);
    source.cells["Ald-ruhn"] = { actor(victim, "Ald-ruhn") };
    accept(registry, snapshot(victim, "Ald-ruhn", 1000.f, 0.f));
    CrimeWitnessBuildRequest request = interiorRequest();
    request.victim = victim;

    const auto result = CrimeWitnessBuilder(registry).build(request, source);
    ASSERT_EQ(result.witnesses.size(), 1u);
    EXPECT_EQ(result.witnesses[0].actor.identity, victim);
}
