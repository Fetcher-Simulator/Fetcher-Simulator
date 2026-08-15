#include <gtest/gtest.h>

#include <chrono>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

#include <apps/openmw-server/CrimeSemanticService.hpp>

namespace
{
    using namespace mwmp;

    struct TemporaryDatabase
    {
        std::filesystem::path path = std::filesystem::temp_directory_path()
            / ("openmw-crime-semantic-test-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".db");

        ~TemporaryDatabase()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
            std::filesystem::remove(path.string() + "-wal", error);
            std::filesystem::remove(path.string() + "-shm", error);
        }
    };

    struct FakeCollision final : ObservationCollisionBackend
    {
        CollisionObservation lineOfSight(
            const std::vector<std::string>&, const ObservationVector&, const ObservationVector&) const override
        {
            ++calls;
            return { true, clear, { { "Balmora", 7 } } };
        }

        bool clear = true;
        mutable int calls = 0;
    };

    struct Rolls final : AwarenessRollSource
    {
        int nextRoll0To99() override
        {
            ++calls;
            if (values.empty())
                return 99;
            const int value = values.front();
            values.pop_front();
            return value;
        }

        std::deque<int> values{ 99 };
        int calls = 0;
    };

    ObservationActorIdentity npc(std::uint32_t refNum)
    {
        ObservationActorIdentity identity;
        identity.kind = ObservationActorKind::Npc;
        identity.actorInstanceId = packActorInstanceKey({ ActorKeyKind::VanillaRefNum, refNum });
        return identity;
    }

    ObservationActorIdentity player(std::uint32_t guid)
    {
        ObservationActorIdentity identity;
        identity.kind = ObservationActorKind::Player;
        identity.playerGuid = guid;
        return identity;
    }

    ObservationActorSnapshot snapshot(ObservationActorIdentity identity, ObservationVector position = {})
    {
        ObservationActorSnapshot value;
        value.identity = identity;
        value.position = position;
        value.forward = { 0.f, 1.f, 0.f };
        value.hasFacing = true;
        value.eligibilityKnown = true;
        value.awarenessInputsKnown = true;
        value.enabled = true;
        value.alive = true;
        value.conscious = true;
        value.fatigueCurrent = 100.f;
        value.fatigueMaximumModified = 100.f;
        value.migrationGeneration = 1;
        value.authorityGeneration = 1;
        value.snapshotGeneration = 1;
        value.sampledAtMs = 1000;
        value.authority = identity.kind == ObservationActorKind::Player ? ObservationAuthority::PlayerClientDelegated
                                                                        : ObservationAuthority::ActorAuthorityDelegated;
        return value;
    }

    CrimeWitnessCandidate witness(
        ObservationActorIdentity identity, std::int32_t alarm = 100, ObservationVector position = { 0.f, 10.f, 0.f })
    {
        CrimeWitnessCandidate value;
        value.actor = snapshot(identity, position);
        value.alarm = alarm;
        value.relationship = CrimeWitnessRelationship::Eligible;
        value.relationshipAuthority = ObservationAuthority::ServerAuthoritative;
        return value;
    }

    CrimeIntent intent(std::string eventId = "crime-event-1", CrimeType type = CrimeType::Theft)
    {
        CrimeIntent value;
        value.eventId = std::move(eventId);
        value.source = "test:crime-semantic";
        value.type = type;
        value.cellId = "Balmora";
        value.offender = snapshot(player(42));
        value.value = type == CrimeType::Theft ? 50 : 0;
        value.observedAtMs = 1000;
        value.collisionGenerations = { { "Balmora", 7 } };
        return value;
    }

    CrimePolicy policy()
    {
        CrimePolicy value;
        value.alarmRadius = 100.f;
        value.theftBountyMultiplier = 1.f;
        value.pickpocketBounty = 25;
        value.trespassBounty = 5;
        value.assaultBounty = 40;
        value.murderBounty = 1000;
        value.werewolfBounty = 250;
        return value;
    }

    struct Fixture
    {
        Fixture()
            : database(temporary.path.string())
            , crime(database)
            , observation({}, collision, rolls)
            , semantic(database, crime, observation, policy())
        {
            context.accountId = database.createAccount("crime-semantic-account");
            context.characterId = database.createCharacter(context.accountId, "Crime Semantic Tester").characterId;
            context.playerGuid = 42;
        }

        TemporaryDatabase temporary;
        PlayerDatabase database;
        FakeCollision collision;
        Rolls rolls;
        CrimeService crime;
        ObservationService observation;
        CrimeSemanticService semantic;
        CrimeSemanticService::Context context;
    };
}

TEST(CrimeSemanticService, PerceivedLowAlarmRunsReportingAndAdvancesCrimeIdWithoutBounty)
{
    Fixture fixture;
    const auto outcome = fixture.semantic.evaluate(intent(), { witness(npc(1), 50) }, fixture.context);

    ASSERT_TRUE(outcome.result.accepted);
    ASSERT_EQ(outcome.result.witnesses.size(), 1u);
    EXPECT_TRUE(outcome.result.witnesses[0].perceived);
    EXPECT_FALSE(outcome.result.witnesses[0].reportCapable);
    EXPECT_FALSE(outcome.result.witnesses[0].reported);
    EXPECT_TRUE(outcome.result.crimeSeen);
    EXPECT_TRUE(outcome.result.reportingStageRun);
    EXPECT_FALSE(outcome.result.bountyApplied);
    EXPECT_EQ(outcome.result.bountyDelta, 0);
    EXPECT_TRUE(outcome.result.currentCrimeIdAdvanced);
    EXPECT_EQ(outcome.result.state.bounty, 0);
    EXPECT_EQ(outcome.result.state.currentCrimeId, 0);
    EXPECT_EQ(outcome.result.state.revision, 1u);
}

TEST(CrimeSemanticService, AlarmOneHundredReportsAndAppliesBountyOnce)
{
    Fixture fixture;
    const auto outcome = fixture.semantic.evaluate(intent(), { witness(npc(1)) }, fixture.context);

    ASSERT_TRUE(outcome.result.accepted);
    EXPECT_TRUE(outcome.result.witnesses[0].reported);
    EXPECT_TRUE(outcome.result.bountyApplied);
    EXPECT_EQ(outcome.result.bountyDelta, 50);
    EXPECT_EQ(outcome.result.state.bounty, 50);
    EXPECT_EQ(outcome.result.state.currentCrimeId, 0);
}

TEST(CrimeSemanticService, DeferredPreparationPreservesObservationUntilOuterCommit)
{
    Fixture fixture;
    fixture.context.deferCommit = true;
    const CrimeIntent request = intent("deferred-crime", CrimeType::Theft);
    const auto prepared = fixture.semantic.evaluate(request, { witness(npc(1), 100) }, fixture.context);

    ASSERT_TRUE(prepared.result.accepted);
    EXPECT_FALSE(prepared.committed);
    EXPECT_FALSE(prepared.replayed);
    ASSERT_TRUE(prepared.pendingCommit);
    EXPECT_TRUE(prepared.result.crimeSeen);
    EXPECT_EQ(prepared.result.bountyDelta, 50);
    EXPECT_EQ(prepared.result.state.bounty, 50);
    EXPECT_EQ(fixture.database.loadPlayerCrimeState(fixture.context.characterId), PlayerCrimeState{});
    EXPECT_FALSE(fixture.database.loadSemanticRequest(
        "crime-event", fixture.context.accountId, fixture.context.characterId, request.eventId).has_value());

    const CrimeCommitResult committed = fixture.database.commitPlayerCrimeMutation(*prepared.pendingCommit);
    EXPECT_EQ(committed.status, CrimeCommitStatus::Committed);
    EXPECT_EQ(fixture.database.loadPlayerCrimeState(fixture.context.characterId), prepared.result.state);
    EXPECT_TRUE(fixture.database.loadSemanticRequest(
        "crime-event", fixture.context.accountId, fixture.context.characterId, request.eventId).has_value());
}

TEST(CrimeSemanticService, WerewolfExposureReportsWithoutAdvancingCrimeIdAndReplays)
{
    Fixture fixture;
    CrimeIntent exposure = intent("werewolf:1", CrimeType::WerewolfExposure);
    const auto first = fixture.semantic.evaluate(exposure, { witness(npc(1), 100) }, fixture.context);
    ASSERT_TRUE(first.result.accepted);
    EXPECT_TRUE(first.result.reportingStageRun);
    EXPECT_TRUE(first.result.bountyApplied);
    EXPECT_EQ(first.result.bountyDelta, 250);
    EXPECT_FALSE(first.result.currentCrimeIdAdvanced);
    EXPECT_EQ(first.result.state.bounty, 250);
    EXPECT_EQ(first.result.state.currentCrimeId, -1);
    EXPECT_EQ(first.result.state.revision, 1u);

    const auto replay = fixture.semantic.evaluate(exposure, { witness(npc(1), 100) }, fixture.context);
    EXPECT_TRUE(replay.replayed);
    EXPECT_EQ(replay.result.state, first.result.state);
    EXPECT_EQ(fixture.database.loadPlayerCrimeState(fixture.context.characterId), first.result.state);
}

TEST(CrimeSemanticService, BlockedAuthoritativeLosDoesNotReachReporting)
{
    Fixture fixture;
    fixture.collision.clear = false;
    const auto outcome = fixture.semantic.evaluate(intent(), { witness(npc(1)) }, fixture.context);

    ASSERT_TRUE(outcome.result.accepted);
    ASSERT_TRUE(outcome.result.witnesses[0].observation);
    EXPECT_EQ(outcome.result.witnesses[0].observation->reason, ObservationReason::BlockedLineOfSight);
    EXPECT_FALSE(outcome.result.crimeSeen);
    EXPECT_FALSE(outcome.result.reportingStageRun);
    EXPECT_EQ(outcome.result.state.revision, 0u);
    EXPECT_EQ(outcome.result.state.currentCrimeId, -1);
}

TEST(CrimeSemanticService, AwarenessFailureDoesNotReachReporting)
{
    Fixture fixture;
    fixture.rolls.values = { 0 };
    CrimeIntent cause = intent();
    cause.offender.invisibility = 1.f;
    const auto outcome = fixture.semantic.evaluate(cause, { witness(npc(1)) }, fixture.context);

    ASSERT_TRUE(outcome.result.witnesses[0].observation);
    EXPECT_EQ(outcome.result.witnesses[0].observation->reason, ObservationReason::AwarenessFailed);
    EXPECT_FALSE(outcome.result.crimeSeen);
    EXPECT_EQ(outcome.result.state.revision, 0u);
}

TEST(CrimeSemanticService, VictimAwarePathBypassesRangeLosAndAwareness)
{
    Fixture fixture;
    fixture.collision.clear = false;
    CrimeIntent cause = intent("pickpocket", CrimeType::Pickpocket);
    cause.victim = npc(7);
    cause.victimAware = true;
    const auto outcome
        = fixture.semantic.evaluate(cause, { witness(*cause.victim, 50, { 1000.f, 0.f, 0.f }) }, fixture.context);

    ASSERT_TRUE(outcome.result.witnesses[0].observation);
    EXPECT_TRUE(outcome.result.witnesses[0].candidate);
    EXPECT_TRUE(outcome.result.witnesses[0].perceived);
    EXPECT_EQ(outcome.result.witnesses[0].observation->path, ObservationPath::VictimAware);
    EXPECT_EQ(fixture.collision.calls, 0);
    EXPECT_EQ(fixture.rolls.calls, 0);
    EXPECT_EQ(outcome.result.state.currentCrimeId, 0);
    EXPECT_EQ(outcome.result.state.bounty, 0);
}

TEST(CrimeSemanticService, MurderHearingIsExplicitAndLosIndependent)
{
    Fixture fixture;
    fixture.collision.clear = false;
    CrimeIntent cause = intent("murder", CrimeType::Murder);
    cause.victim = npc(8);
    const auto outcome = fixture.semantic.evaluate(cause, { witness(npc(9)) }, fixture.context);

    ASSERT_TRUE(outcome.result.witnesses[0].observation);
    EXPECT_EQ(outcome.result.witnesses[0].observation->path, ObservationPath::MurderHearing);
    EXPECT_TRUE(outcome.result.witnesses[0].perceived);
    EXPECT_EQ(fixture.collision.calls, 0);
    EXPECT_EQ(outcome.result.state.bounty, 1000);
}

TEST(CrimeSemanticService, TrespassAndAssaultUseTypedPolicyAndAssaultDerivesVictimAwareness)
{
    Fixture fixture;
    CrimeIntent trespass = intent("trespass", CrimeType::Trespass);
    const auto trespassResult = fixture.semantic.evaluate(trespass, { witness(npc(1)) }, fixture.context);
    ASSERT_TRUE(trespassResult.result.accepted);
    EXPECT_EQ(trespassResult.result.bountyDelta, 5);

    fixture.collision.clear = false;
    CrimeIntent assault = intent("assault", CrimeType::Assault);
    assault.victim = npc(2);
    ASSERT_FALSE(assault.victimAware);
    const auto assaultResult = fixture.semantic.evaluate(assault, { witness(*assault.victim) }, fixture.context);
    ASSERT_TRUE(assaultResult.result.witnesses[0].observation);
    EXPECT_EQ(assaultResult.result.witnesses[0].observation->path, ObservationPath::VictimAware);
    EXPECT_TRUE(assaultResult.result.witnesses[0].reported);
    EXPECT_EQ(assaultResult.result.bountyDelta, 40);
    EXPECT_EQ(assaultResult.result.state.bounty, 45);
    EXPECT_EQ(fixture.collision.calls, 1);
}

TEST(CrimeSemanticService, RemoteHumanNpcShapedProxyIsExcludedByCanonicalIdentity)
{
    Fixture fixture;
    const auto outcome = fixture.semantic.evaluate(intent(), { witness(player(77)) }, fixture.context);

    ASSERT_EQ(outcome.result.witnesses.size(), 1u);
    EXPECT_EQ(outcome.result.witnesses[0].eligibility, CrimeWitnessEligibility::CanonicalKindRejected);
    EXPECT_FALSE(outcome.result.witnesses[0].observation);
    EXPECT_FALSE(outcome.result.crimeSeen);
    EXPECT_EQ(fixture.collision.calls, 0);
}

TEST(CrimeSemanticService, DeadDisabledAndUnconsciousActorsAreIneligible)
{
    Fixture fixture;
    auto dead = witness(npc(1));
    dead.actor.alive = false;
    auto disabled = witness(npc(2));
    disabled.actor.enabled = false;
    auto unconscious = witness(npc(3));
    unconscious.actor.conscious = false;
    const auto outcome = fixture.semantic.evaluate(intent(), { dead, disabled, unconscious }, fixture.context);

    ASSERT_EQ(outcome.result.witnesses.size(), 3u);
    for (const auto& result : outcome.result.witnesses)
    {
        EXPECT_EQ(result.eligibility, CrimeWitnessEligibility::ActorIneligible);
        EXPECT_FALSE(result.perceived);
        EXPECT_FALSE(result.reportCapable);
    }
    EXPECT_EQ(fixture.collision.calls, 0);
}

TEST(CrimeSemanticService, UnknownCombatOrFollowerProvenanceFailsClosed)
{
    Fixture fixture;
    auto unknown = witness(npc(1));
    unknown.relationship = CrimeWitnessRelationship::Unknown;
    auto combat = witness(npc(2));
    combat.relationship = CrimeWitnessRelationship::InCombatWithVictim;
    auto follower = witness(npc(3));
    follower.relationship = CrimeWitnessRelationship::PlayerFollower;
    const auto outcome = fixture.semantic.evaluate(intent(), { unknown, combat, follower }, fixture.context);

    EXPECT_EQ(outcome.result.witnesses[0].eligibility, CrimeWitnessEligibility::RelationshipUnknown);
    EXPECT_EQ(outcome.result.witnesses[1].eligibility, CrimeWitnessEligibility::InCombatWithVictim);
    EXPECT_EQ(outcome.result.witnesses[2].eligibility, CrimeWitnessEligibility::PlayerFollower);
    EXPECT_FALSE(outcome.result.crimeSeen);
}

TEST(CrimeSemanticService, DuplicateEventReplaysDurableResultWithoutRerollOrReapply)
{
    Fixture fixture;
    const CrimeIntent cause = intent("duplicate");
    const auto first = fixture.semantic.evaluate(cause, { witness(npc(1)) }, fixture.context);
    ASSERT_TRUE(first.result.accepted);
    ASSERT_EQ(fixture.rolls.calls, 1);

    fixture.collision.clear = false;
    const auto replay = fixture.semantic.evaluate(cause, { witness(npc(2), 0) }, fixture.context);
    EXPECT_TRUE(replay.replayed);
    EXPECT_FALSE(replay.committed);
    EXPECT_EQ(replay.result.state.bounty, 50);
    EXPECT_EQ(replay.result.state.currentCrimeId, 0);
    EXPECT_EQ(replay.result.state.revision, 1u);
    EXPECT_EQ(fixture.rolls.calls, 1);
    EXPECT_EQ(fixture.collision.calls, 1);
}

TEST(CrimeSemanticService, RestartReplaysDurableSemanticResultWithoutObservation)
{
    TemporaryDatabase temporary;
    std::int64_t accountId = 0;
    std::int64_t characterId = 0;
    const CrimeIntent cause = intent("restart-replay");
    {
        PlayerDatabase database(temporary.path.string());
        accountId = database.createAccount("crime-restart-account");
        characterId = database.createCharacter(accountId, "Crime Restart Tester").characterId;
        FakeCollision collision;
        Rolls rolls;
        CrimeService crime(database);
        ObservationService observation({}, collision, rolls);
        CrimeSemanticService semantic(database, crime, observation, policy());
        CrimeSemanticService::Context context{ accountId, characterId, 42 };
        ASSERT_TRUE(semantic.evaluate(cause, { witness(npc(1)) }, context).result.accepted);
    }

    PlayerDatabase reopened(temporary.path.string());
    FakeCollision collision;
    collision.clear = false;
    Rolls rolls;
    CrimeService crime(reopened);
    ObservationService observation({}, collision, rolls);
    CrimeSemanticService semantic(reopened, crime, observation, policy());
    CrimeSemanticService::Context context{ accountId, characterId, 42 };
    const auto replay = semantic.evaluate(cause, { witness(npc(2), 0) }, context);

    EXPECT_TRUE(replay.replayed);
    EXPECT_EQ(replay.result.state.bounty, 50);
    EXPECT_EQ(replay.result.state.currentCrimeId, 0);
    EXPECT_EQ(replay.result.state.revision, 1u);
    EXPECT_EQ(collision.calls, 0);
    EXPECT_EQ(rolls.calls, 0);
}

TEST(CrimeSemanticService, ReusedEventIdWithDifferentCanonicalCauseIsRejected)
{
    Fixture fixture;
    CrimeIntent first = intent("conflict");
    ASSERT_TRUE(fixture.semantic.evaluate(first, { witness(npc(1)) }, fixture.context).result.accepted);

    CrimeIntent different = first;
    different.value = 75;
    const auto conflict = fixture.semantic.evaluate(different, { witness(npc(1)) }, fixture.context);
    EXPECT_FALSE(conflict.result.accepted);
    EXPECT_EQ(conflict.result.error, CrimeSemanticError::DuplicateConflict);
    EXPECT_EQ(conflict.result.state.bounty, 50);
    EXPECT_EQ(conflict.result.state.currentCrimeId, 0);
    EXPECT_EQ(conflict.result.state.revision, 1u);
}

TEST(CrimeSemanticService, ADifferentEligibleWitnessCanReportAfterAnotherWitnessPerceives)
{
    Fixture fixture;
    auto seer = witness(npc(1), 50);
    auto reporter = witness(npc(2), 100);
    reporter.actor.invisibility = 1.f;
    reporter.actor.blind = 1000.f;
    const auto outcome = fixture.semantic.evaluate(intent(), { seer, reporter }, fixture.context);

    ASSERT_EQ(outcome.result.witnesses.size(), 2u);
    EXPECT_TRUE(outcome.result.witnesses[0].perceived);
    EXPECT_FALSE(outcome.result.witnesses[0].reported);
    EXPECT_FALSE(outcome.result.witnesses[1].perceived);
    EXPECT_TRUE(outcome.result.witnesses[1].reportCapable);
    EXPECT_TRUE(outcome.result.witnesses[1].reported);
    EXPECT_EQ(outcome.result.state.bounty, 50);
}
