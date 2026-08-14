#include <gtest/gtest.h>

#include <cmath>
#include <deque>
#include <limits>
#include <utility>
#include <vector>

#include <apps/openmw-server/ObservationService.hpp>

namespace
{
    using namespace mwmp;

    ObservationActorIdentity player(std::uint32_t guid)
    {
        ObservationActorIdentity result;
        result.kind = ObservationActorKind::Player;
        result.playerGuid = guid;
        return result;
    }

    ObservationActorIdentity actor(ObservationActorKind kind, std::uint32_t refNum)
    {
        ObservationActorIdentity result;
        result.kind = kind;
        result.actorInstanceId = packActorInstanceKey({ ActorKeyKind::VanillaRefNum, refNum });
        return result;
    }

    ObservationActorSnapshot snapshot(
        ObservationActorIdentity identity, ObservationAuthority authority, std::uint64_t sampledAtMs = 1000)
    {
        ObservationActorSnapshot result;
        result.identity = identity;
        result.forward = { 0.f, 1.f, 0.f };
        result.hasFacing = true;
        result.eligibilityKnown = true;
        result.awarenessInputsKnown = true;
        result.enabled = true;
        result.alive = true;
        result.conscious = true;
        result.fatigueCurrent = 100.f;
        result.fatigueMaximumModified = 100.f;
        result.migrationGeneration = 1;
        result.authorityGeneration = 1;
        result.snapshotGeneration = 1;
        result.sampledAtMs = sampledAtMs;
        result.authority = authority;
        return result;
    }

    struct FakeCollision final : ObservationCollisionBackend
    {
        mutable int calls = 0;
        mutable std::vector<std::string> requestedCells;
        CollisionObservation result{ true, true, { { "Balmora", 7 } } };

        CollisionObservation lineOfSight(
            const std::vector<std::string>& cellIds, const ObservationVector&, const ObservationVector&) const override
        {
            ++calls;
            requestedCells = cellIds;
            return result;
        }
    };

    struct SequenceRolls final : AwarenessRollSource
    {
        explicit SequenceRolls(std::vector<int> input)
            : values(input.begin(), input.end())
        {
        }

        int nextRoll0To99() override
        {
            ++calls;
            if (values.empty())
                return 0;
            const int result = values.front();
            values.pop_front();
            return result;
        }

        std::deque<int> values;
        int calls = 0;
    };

    ObservationQuery normalQuery()
    {
        ObservationQuery result;
        result.eventId = "event-1";
        result.cellId = "Balmora";
        result.observer = snapshot(actor(ObservationActorKind::Npc, 10), ObservationAuthority::ActorAuthorityDelegated);
        result.target = snapshot(player(20), ObservationAuthority::PlayerClientDelegated);
        result.target.position = { 0.f, 10.f, 0.f };
        result.observedAtMs = 1000;
        result.collisionGenerations = { { "Balmora", 7 } };
        return result;
    }
}

TEST(ObservationService, CanonicalIdentityKeepsPlayersSeparateFromActors)
{
    EXPECT_TRUE(player(1).isValid());
    EXPECT_FALSE(player(0).isValid());
    EXPECT_TRUE(actor(ObservationActorKind::Npc, 1).isValid());
    EXPECT_TRUE(actor(ObservationActorKind::Creature, 1).isValid());

    ObservationActorIdentity ambiguous = player(1);
    ambiguous.actorInstanceId = actor(ObservationActorKind::Npc, 1).actorInstanceId;
    EXPECT_FALSE(ambiguous.isValid());
}

TEST(ObservationService, ImplementsVanillaAwarenessTerms)
{
    AwarenessSettings settings;
    settings.sneakSkillMultiplier = 1.f;
    settings.sneakBootMultiplier = 2.f;
    settings.sneakDistanceBase = 1.f;
    settings.sneakDistanceMultiplier = 0.1f;
    settings.sneakNoViewMultiplier = 0.5f;
    settings.sneakViewMultiplier = 2.f;
    settings.fatigueBase = 1.25f;
    settings.fatigueMultiplier = 0.5f;

    auto target = snapshot(player(1), ObservationAuthority::PlayerClientDelegated);
    target.position = { 0.f, 10.f, 0.f };
    target.sneaking = true;
    target.onGround = true;
    target.sneakSkill = 20.f;
    target.agility = 50.f;
    target.luck = 40.f;
    target.bootWeight = 5.f;
    target.fatigueCurrent = 50.f;
    target.chameleon = 5.f;
    target.invisibility = 1.f;

    auto observer = snapshot(actor(ObservationActorKind::Npc, 2), ObservationAuthority::ActorAuthorityDelegated);
    observer.sneakSkill = 30.f;
    observer.agility = 50.f;
    observer.luck = 40.f;
    observer.blind = 4.f;
    observer.fatigueCurrent = 50.f;

    const AwarenessCalculation front = ObservationService::calculateAwareness(target, observer, settings, 99);
    EXPECT_FALSE(front.targetBehindObserver);
    EXPECT_FLOAT_EQ(front.targetConcealment, 193.f);
    EXPECT_FLOAT_EQ(front.observerDetection, 80.f);
    EXPECT_FLOAT_EQ(front.threshold, 113.f);
    EXPECT_FALSE(front.detected);

    target.position.y = -10.f;
    const AwarenessCalculation behind = ObservationService::calculateAwareness(target, observer, settings, 99);
    EXPECT_TRUE(behind.targetBehindObserver);
    EXPECT_FLOAT_EQ(behind.observerDetection, 20.f);
    EXPECT_FLOAT_EQ(behind.threshold, 173.f);
}

TEST(ObservationService, NonSneakingTargetIgnoresSneakStatsBootsAndDistance)
{
    AwarenessSettings settings;
    settings.sneakSkillMultiplier = 10.f;
    settings.sneakBootMultiplier = 10.f;
    settings.sneakDistanceBase = 10.f;
    settings.sneakDistanceMultiplier = 10.f;

    auto target = snapshot(player(1), ObservationAuthority::PlayerClientDelegated);
    target.position = { 1000.f, 1000.f, 0.f };
    target.sneakSkill = 100.f;
    target.agility = 100.f;
    target.luck = 100.f;
    target.bootWeight = 100.f;
    target.chameleon = 25.f;
    auto observer = snapshot(actor(ObservationActorKind::Npc, 2), ObservationAuthority::ActorAuthorityDelegated);

    const AwarenessCalculation result = ObservationService::calculateAwareness(target, observer, settings, 25);
    EXPECT_FLOAT_EQ(result.targetConcealment, 25.f);
    EXPECT_TRUE(result.detected);
}

TEST(ObservationService, FatigueTermMatchesCreatureStatsFormula)
{
    AwarenessSettings settings;
    settings.fatigueBase = 1.25f;
    settings.fatigueMultiplier = 0.5f;

    EXPECT_FLOAT_EQ(ObservationService::fatigueTerm(50.f, 100.f, settings), 1.f);
    EXPECT_FLOAT_EQ(ObservationService::fatigueTerm(-10.f, 100.f, settings), 0.75f);
    EXPECT_FLOAT_EQ(ObservationService::fatigueTerm(0.f, 0.5f, settings), 1.25f);
}

TEST(ObservationService, VictimAwarenessAndMurderHearingPreserveSpecialPaths)
{
    FakeCollision collision;
    SequenceRolls rolls({ 0 });
    ObservationService service({}, collision, rolls);
    ObservationQuery query = normalQuery();

    query.path = ObservationPath::VictimAware;
    query.victim = query.observer.identity;
    ObservationResult victimAware = service.observe(query);
    EXPECT_TRUE(victimAware.observable);
    EXPECT_EQ(victimAware.reason, ObservationReason::Observed);
    EXPECT_EQ(victimAware.path, ObservationPath::VictimAware);
    EXPECT_FALSE(victimAware.lineOfSight.has_value());
    EXPECT_FALSE(victimAware.awareness.has_value());
    EXPECT_EQ(victimAware.authority, ObservationAuthority::ActorAuthorityDelegated);

    query.path = ObservationPath::MurderHearing;
    query.victim = actor(ObservationActorKind::Npc, 99);
    ObservationResult murderHearing = service.observe(query);
    EXPECT_TRUE(murderHearing.observable);
    EXPECT_EQ(murderHearing.reason, ObservationReason::Observed);
    EXPECT_EQ(murderHearing.path, ObservationPath::MurderHearing);
    EXPECT_FALSE(murderHearing.lineOfSight.has_value());
    EXPECT_FALSE(murderHearing.awareness.has_value());
    EXPECT_EQ(collision.calls, 0);
    EXPECT_EQ(rolls.calls, 0);

    query.victim = query.observer.identity;
    EXPECT_EQ(service.observe(query).reason, ObservationReason::InvalidQuery);
}

TEST(ObservationService, RemoteHumanPlayerCannotBecomeVanillaNpcWitness)
{
    FakeCollision collision;
    SequenceRolls rolls({ 99 });
    ObservationService service({}, collision, rolls);
    ObservationQuery query = normalQuery();
    query.observer = snapshot(player(77), ObservationAuthority::PlayerClientDelegated);
    query.observerPolicy = ObservationObserverPolicy::VanillaCrimeWitness;

    const ObservationResult result = service.observe(query);
    EXPECT_FALSE(result.observable);
    EXPECT_EQ(result.reason, ObservationReason::ObserverKindRejected);
    EXPECT_EQ(collision.calls, 0);
}

TEST(ObservationService, GenericObservationAllowsCreatureObserver)
{
    FakeCollision collision;
    SequenceRolls rolls({ 99 });
    ObservationService service({}, collision, rolls);
    ObservationQuery query = normalQuery();
    query.observer = snapshot(actor(ObservationActorKind::Creature, 77), ObservationAuthority::ActorAuthorityDelegated);

    const ObservationResult result = service.observe(query);
    EXPECT_TRUE(result.observable);
    EXPECT_EQ(result.reason, ObservationReason::Observed);
    EXPECT_EQ(collision.calls, 1);
}

TEST(ObservationService, VanillaWitnessPolicyAllowsOnlyCanonicalNpcKind)
{
    FakeCollision collision;
    SequenceRolls rolls({ 99 });
    ObservationService service({}, collision, rolls);
    ObservationQuery query = normalQuery();
    query.observerPolicy = ObservationObserverPolicy::VanillaCrimeWitness;

    EXPECT_TRUE(service.observe(query).observable);

    query.observer = snapshot(actor(ObservationActorKind::Creature, 77), ObservationAuthority::ActorAuthorityDelegated);
    EXPECT_EQ(service.observe(query).reason, ObservationReason::ObserverKindRejected);

    query.observer = snapshot(player(77), ObservationAuthority::PlayerClientDelegated);
    EXPECT_EQ(service.observe(query).reason, ObservationReason::ObserverKindRejected);
}

TEST(ObservationService, NormalPathRequiresMatchingCanonicalCollisionGenerations)
{
    FakeCollision collision;
    SequenceRolls rolls({ 99 });
    ObservationService service({}, collision, rolls);
    ObservationQuery query = normalQuery();

    collision.result.clear = false;
    EXPECT_EQ(service.observe(query).reason, ObservationReason::BlockedLineOfSight);

    collision.result.clear = true;
    collision.result.generations[0].generation = 8;
    ObservationResult stale = service.observe(query);
    EXPECT_EQ(stale.reason, ObservationReason::CollisionGenerationMismatch);
    ASSERT_EQ(stale.collisionGenerations.size(), 1u);
    EXPECT_EQ(stale.collisionGenerations[0].generation, 8u);
    EXPECT_EQ(rolls.calls, 0);

    collision.result.available = false;
    collision.result.generations.clear();
    EXPECT_EQ(service.observe(query).reason, ObservationReason::CollisionUnavailable);
}

TEST(ObservationService, MultiCellLineOfSightUsesCanonicalGenerationsAndRejectsOneCellChange)
{
    FakeCollision collision;
    collision.result.generations = { { "EXT:-2,-2", 3 }, { "EXT:-3,-2", 7 } };
    SequenceRolls rolls({ 99 });
    ObservationService service({}, collision, rolls);
    ObservationQuery query = normalQuery();
    query.cellId = "EXT:-3,-2";
    query.collisionGenerations = collision.result.generations;

    const ObservationResult valid = service.observe(query);
    EXPECT_TRUE(valid.observable);
    EXPECT_EQ(collision.requestedCells, (std::vector<std::string>{ "EXT:-2,-2", "EXT:-3,-2" }));

    collision.result.generations[0].generation = 4;
    const ObservationResult changed = service.observe(query);
    EXPECT_FALSE(changed.observable);
    EXPECT_EQ(changed.reason, ObservationReason::CollisionGenerationMismatch);
    EXPECT_EQ(changed.collisionGenerations[0].generation, 4u);
    EXPECT_EQ(changed.collisionGenerations[1].generation, 7u);
}

TEST(ObservationService, CachedRollIsPerObserverAndExpiresAtFiveSeconds)
{
    FakeCollision collision;
    SequenceRolls rolls({ 40, 60, 30 });
    ObservationService service({}, collision, rolls);
    ObservationQuery query = normalQuery();
    query.target.chameleon = 50.f;

    ObservationResult first = service.observe(query);
    EXPECT_FALSE(first.observable);
    ASSERT_TRUE(first.awarenessRoll);
    EXPECT_EQ(*first.awarenessRoll, 40);

    query.eventId = "event-2";
    query.observedAtMs = 5999;
    query.observer.sampledAtMs = 5999;
    query.target.sampledAtMs = 5999;
    ObservationResult reused = service.observe(query);
    ASSERT_TRUE(reused.awarenessRoll);
    EXPECT_EQ(*reused.awarenessRoll, 40);
    EXPECT_EQ(rolls.calls, 1);

    query.eventId = "event-3";
    query.observedAtMs = 6000;
    query.observer.sampledAtMs = 6000;
    query.target.sampledAtMs = 6000;
    ObservationResult expired = service.observe(query);
    EXPECT_TRUE(expired.observable);
    ASSERT_TRUE(expired.awarenessRoll);
    EXPECT_EQ(*expired.awarenessRoll, 60);

    query.eventId = "event-4";
    query.observedAtMs = 6001;
    query.observer.sampledAtMs = 6001;
    query.target.sampledAtMs = 6001;
    ++query.observer.authorityGeneration;
    ObservationResult generationChanged = service.observe(query);
    EXPECT_FALSE(generationChanged.observable);
    ASSERT_TRUE(generationChanged.awarenessRoll);
    EXPECT_EQ(*generationChanged.awarenessRoll, 30);
    EXPECT_EQ(rolls.calls, 3);
}

TEST(ObservationService, AwarenessRollCacheIsPerObserverNotTarget)
{
    FakeCollision collision;
    SequenceRolls rolls({ 11, 22 });
    ObservationService service({}, collision, rolls);
    ObservationQuery query = normalQuery();

    const ObservationResult first = service.observe(query);
    ASSERT_TRUE(first.awarenessRoll);
    EXPECT_EQ(*first.awarenessRoll, 11);

    query.eventId = "different-target";
    query.target = snapshot(player(21), ObservationAuthority::PlayerClientDelegated);
    query.target.position = { 0.f, 20.f, 0.f };
    const ObservationResult sameObserver = service.observe(query);
    ASSERT_TRUE(sameObserver.awarenessRoll);
    EXPECT_EQ(*sameObserver.awarenessRoll, 11);

    query.eventId = "different-observer";
    query.observer = snapshot(actor(ObservationActorKind::Npc, 11), ObservationAuthority::ActorAuthorityDelegated);
    const ObservationResult differentObserver = service.observe(query);
    ASSERT_TRUE(differentObserver.awarenessRoll);
    EXPECT_EQ(*differentObserver.awarenessRoll, 22);
    EXPECT_EQ(rolls.calls, 2);
}

TEST(ObservationService, AwarenessRollCacheInvalidatesOnlyForAuditedTransitions)
{
    FakeCollision collision;
    SequenceRolls rolls({ 10, 20, 30, 40 });
    ObservationService service({}, collision, rolls);
    ObservationQuery query = normalQuery();

    EXPECT_EQ(*service.observe(query).awarenessRoll, 10);

    ++query.observer.snapshotGeneration;
    query.observer.sampledAtMs = ++query.observedAtMs;
    query.target.sampledAtMs = query.observedAtMs;
    EXPECT_EQ(*service.observe(query).awarenessRoll, 10);

    ++query.observer.migrationGeneration;
    EXPECT_EQ(*service.observe(query).awarenessRoll, 20);

    service.invalidateObserver(query.observer.identity);
    EXPECT_EQ(*service.observe(query).awarenessRoll, 30);

    service.clearAwarenessRolls();
    EXPECT_EQ(*service.observe(query).awarenessRoll, 40);
}

TEST(ObservationService, NormalResultMakesDelegatedProvenanceVisible)
{
    FakeCollision collision;
    SequenceRolls rolls({ 99 });
    ObservationService service({}, collision, rolls);
    const ObservationResult result = service.observe(normalQuery());

    EXPECT_TRUE(result.observable);
    EXPECT_EQ(result.reason, ObservationReason::Observed);
    EXPECT_EQ(result.authority, ObservationAuthority::MixedDelegated);
    ASSERT_TRUE(result.lineOfSight);
    EXPECT_TRUE(*result.lineOfSight);
    ASSERT_TRUE(result.awareness);
    EXPECT_TRUE(*result.awareness);
}

TEST(ObservationService, ServerOnlyInputsRemainServerAuthoritative)
{
    FakeCollision collision;
    SequenceRolls rolls({ 99 });
    ObservationService service({}, collision, rolls);
    ObservationQuery query = normalQuery();
    query.observer.authority = ObservationAuthority::ServerAuthoritative;
    query.target.authority = ObservationAuthority::ServerAuthoritative;

    const ObservationResult result = service.observe(query);
    EXPECT_TRUE(result.observable);
    EXPECT_EQ(result.authority, ObservationAuthority::ServerAuthoritative);
}

TEST(ObservationService, RejectsStaleOrNonCanonicalInputBeforeQueryingCollision)
{
    FakeCollision collision;
    SequenceRolls rolls({ 99 });
    ObservationService service({}, collision, rolls);
    ObservationQuery query = normalQuery();

    query.observedAtMs = 3000;
    EXPECT_EQ(service.observe(query).reason, ObservationReason::StaleActorSnapshot);

    query = normalQuery();
    query.observer.authorityGeneration = 0;
    EXPECT_EQ(service.observe(query).reason, ObservationReason::StaleActorSnapshot);

    query = normalQuery();
    query.observer.migrationGeneration = 0;
    EXPECT_EQ(service.observe(query).reason, ObservationReason::StaleActorSnapshot);

    query = normalQuery();
    query.target.snapshotGeneration = 0;
    EXPECT_EQ(service.observe(query).reason, ObservationReason::StaleActorSnapshot);

    query = normalQuery();
    query.target.position.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(service.observe(query).reason, ObservationReason::InvalidQuery);

    query = normalQuery();
    query.collisionGenerations = { { "Balmora", 7 }, { "Ald-ruhn", 3 } };
    EXPECT_EQ(service.observe(query).reason, ObservationReason::InvalidQuery);
    EXPECT_EQ(collision.calls, 0);

    query = normalQuery();
    query.collisionGenerations = { { "Ald-ruhn", 3 } };
    EXPECT_EQ(service.observe(query).reason, ObservationReason::InvalidQuery);
    EXPECT_EQ(collision.calls, 0);
}

TEST(ObservationService, RejectsNonFiniteSettings)
{
    FakeCollision collision;
    SequenceRolls rolls({ 99 });
    AwarenessSettings settings;
    settings.fatigueBase = std::numeric_limits<float>::infinity();
    EXPECT_THROW(ObservationService(settings, collision, rolls), std::invalid_argument);
}
