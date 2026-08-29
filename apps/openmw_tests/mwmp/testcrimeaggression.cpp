#include <gtest/gtest.h>

#include <apps/openmw-server/CrimeAggression.hpp>

namespace
{
    using namespace mwmp;

    CrimeAggressionPolicy vanillaPolicy()
    {
        CrimeAggressionPolicy policy;
        policy.fightTrespass = 5;
        policy.fightPickpocket = 25;
        policy.fightAttack = 100;
        policy.fightAttacking = 50;
        policy.fightKilling = 50;
        policy.fightStealing = 1;
        policy.dispositionTrespass = -5.f;
        policy.dispositionPickpocket = -10.f;
        policy.dispositionAttack = -50.f;
        policy.dispositionAttacking = -10.f;
        policy.dispositionKilling = -50.f;
        policy.dispositionStealing = -1.f;
        policy.fightDispositionMultiplier = 0.2f;
        policy.fightDistanceBase = 20;
        policy.fightDistanceMultiplier = 0.01f;
        return policy;
    }

    CrimeAggressionInput assaultWitness(std::int32_t fight = 30, std::int32_t alarm = 90)
    {
        CrimeAggressionInput input;
        input.type = CrimeType::Assault;
        input.baseFight = fight;
        input.alarm = alarm;
        return input;
    }
}

TEST(CrimeAggression, LuckyLockupAssaultWitnessCrossesHostilityThreshold)
{
    const CrimeAggressionResult result
        = calculateCrimeAggression(assaultWitness(), vanillaPolicy());

    ASSERT_TRUE(result.evaluated);
    EXPECT_TRUE(result.combat);
    EXPECT_EQ(result.crimeFight, 50);
    EXPECT_FLOAT_EQ(result.alarmTerm, 0.9f);
    EXPECT_FLOAT_EQ(result.fightTerm, 70.f);
    EXPECT_EQ(result.finalFight, 100);
}

TEST(CrimeAggression, LowAggressionWitnessRemainsPassive)
{
    const CrimeAggressionResult result
        = calculateCrimeAggression(assaultWitness(0, 90), vanillaPolicy());

    EXPECT_TRUE(result.evaluated);
    EXPECT_FALSE(result.combat);
    EXPECT_FLOAT_EQ(result.fightTerm, 81.f);
    EXPECT_EQ(result.finalFight, 0);
}

TEST(CrimeAggression, AssaultVictimUsesFightAttack)
{
    CrimeAggressionInput input = assaultWitness(0, 90);
    input.victim = true;
    const CrimeAggressionResult result = calculateCrimeAggression(input, vanillaPolicy());

    EXPECT_TRUE(result.combat);
    EXPECT_EQ(result.crimeFight, 100);
    EXPECT_EQ(result.finalFight, 100);
}

TEST(CrimeAggression, AlarmScalesTheCompleteFightTerm)
{
    const CrimeAggressionResult high
        = calculateCrimeAggression(assaultWitness(30, 90), vanillaPolicy());
    const CrimeAggressionResult low
        = calculateCrimeAggression(assaultWitness(30, 50), vanillaPolicy());

    EXPECT_TRUE(high.combat);
    EXPECT_FALSE(low.combat);
    EXPECT_FLOAT_EQ(low.fightTerm, 45.f);
}

TEST(CrimeAggression, ModdedPolicyValuesDriveDecision)
{
    CrimeAggressionPolicy policy = vanillaPolicy();
    policy.fightAttacking = 0;
    const CrimeAggressionResult passive
        = calculateCrimeAggression(assaultWitness(), policy);
    policy.fightAttacking = 80;
    const CrimeAggressionResult hostile
        = calculateCrimeAggression(assaultWitness(), policy);

    EXPECT_FALSE(passive.combat);
    EXPECT_TRUE(hostile.combat);
    EXPECT_EQ(hostile.crimeFight, 80);
}

TEST(CrimeAggression, MurderUsesKillingSemantics)
{
    CrimeAggressionInput input = assaultWitness();
    input.type = CrimeType::Murder;
    const CrimeAggressionResult result = calculateCrimeAggression(input, vanillaPolicy());

    EXPECT_TRUE(result.combat);
    EXPECT_EQ(result.crimeFight, 50);
    EXPECT_FLOAT_EQ(result.dispositionTerm, -50.f);
}

TEST(CrimeAggression, PickpocketPreservesVictimAndObserverAlarmSpecialCases)
{
    CrimeAggressionInput input = assaultWitness(0, 0);
    input.type = CrimeType::Pickpocket;
    const CrimeAggressionResult observer = calculateCrimeAggression(input, vanillaPolicy());
    input.victim = true;
    const CrimeAggressionResult victim = calculateCrimeAggression(input, vanillaPolicy());

    EXPECT_FALSE(observer.combat);
    EXPECT_FLOAT_EQ(observer.alarmTerm, 0.f);
    EXPECT_EQ(victim.crimeFight, 100);
    EXPECT_FLOAT_EQ(victim.alarmTerm, 1.f);
    EXPECT_TRUE(victim.combat);
}

TEST(CrimeAggression, TheftAndTrespassUseTheGenericNativeFightPath)
{
    CrimeAggressionInput input = assaultWitness(30, 100);
    input.type = CrimeType::Theft;
    input.value = 50;
    const CrimeAggressionResult theft = calculateCrimeAggression(input, vanillaPolicy());
    input.type = CrimeType::Trespass;
    input.value = 0;
    input.baseFight = 70;
    const CrimeAggressionResult trespass = calculateCrimeAggression(input, vanillaPolicy());

    EXPECT_TRUE(theft.combat);
    EXPECT_EQ(theft.crimeFight, 50);
    EXPECT_TRUE(trespass.combat);
    EXPECT_EQ(trespass.crimeFight, 5);
}

TEST(CrimeAggression, NpcDistanceBiasUsesFullThreeDimensionalDistance)
{
    CrimeAggressionInput input = assaultWitness(0, 100);
    input.witnessPosition = { 3.f, 4.f, 12.f };
    const CrimeAggressionResult result = calculateCrimeAggression(input, vanillaPolicy());

    EXPECT_FLOAT_EQ(result.distance, 13.f);
    EXPECT_FLOAT_EQ(result.distanceBias, 19.87f);
}

TEST(CrimeAggression, GuardEnforcementUsesConfiguredCrimeThresholds)
{
    EXPECT_EQ(calculateGuardCrimeEnforcement(999, 1000, 5),
        GuardCrimeEnforcementDecision::None);
    EXPECT_EQ(calculateGuardCrimeEnforcement(1000, 1000, 5),
        GuardCrimeEnforcementDecision::Arrest);
    EXPECT_EQ(calculateGuardCrimeEnforcement(4999, 1000, 5),
        GuardCrimeEnforcementDecision::Arrest);
    EXPECT_EQ(calculateGuardCrimeEnforcement(5000, 1000, 5),
        GuardCrimeEnforcementDecision::Combat);
}

TEST(CrimeAggression, GuardEnforcementRejectsInvalidPolicy)
{
    EXPECT_EQ(calculateGuardCrimeEnforcement(10000, 0, 5),
        GuardCrimeEnforcementDecision::None);
    EXPECT_EQ(calculateGuardCrimeEnforcement(10000, 1000, 0),
        GuardCrimeEnforcementDecision::None);
}
