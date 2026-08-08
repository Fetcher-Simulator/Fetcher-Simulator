#include <gtest/gtest.h>

#include <stdexcept>
#include <string_view>

#include <components/enchanting/EnchantingMechanics.hpp>
#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadbook.hpp>
#include <components/esm3/loadclot.hpp>
#include <components/esm3/loadweap.hpp>
#include <components/misc/rng.hpp>

namespace
{
    using Crafting::EnchantingBarterInput;
    using Crafting::EnchantingMechanics;
    using Crafting::EnchantingMechanicsInput;
    using Crafting::MagicEffectData;

    ESM::RefId effectId(const char* id)
    {
        return ESM::RefId::stringRefId(id);
    }

    ESM::ENAMstruct effect(std::string_view id, int magMin, int magMax, int duration, int area, int range = ESM::RT_Self)
    {
        ESM::ENAMstruct result;
        result.mEffectID = effectId(std::string(id).c_str());
        result.mMagnMin = magMin;
        result.mMagnMax = magMax;
        result.mDuration = duration;
        result.mArea = area;
        result.mRange = range;
        return result;
    }

    EnchantingMechanicsInput makeInput()
    {
        EnchantingMechanicsInput input;
        input.itemType = ESM::Weapon::sRecordId;
        input.weaponType = ESM::Weapon::LongBladeOneHand;
        input.enchantCapacity = 100;
        input.gemCharge = 400;
        input.castStyle = ESM::Enchantment::WhenStrikes;
        input.enchantSkill = 50.f;
        input.intelligence = 50.f;
        input.luck = 50.f;
        input.fatigueTerm = 1.f;
        input.availableCount = 1;
        input.magicEffect = [](const ESM::RefId& id) -> std::optional<MagicEffectData> {
            if (id == effectId("FireDamage"))
                return MagicEffectData{ 1.f, 0 };
            if (id == effectId("FrostDamage"))
                return MagicEffectData{ 2.f, 0 };
            return std::nullopt;
        };
        input.gmst = [](std::string_view id) -> std::optional<float> {
            if (id == "fEffectCostMult")
                return 1.f;
            if (id == "fEnchantmentConstantDurationMult")
                return 1.f;
            if (id == "fEnchantmentMult")
                return 1.f;
            if (id == "fEnchantmentChanceMult")
                return 1.f;
            if (id == "fEnchantmentConstantChanceMult")
                return 1.f;
            if (id == "fEnchantmentValueMult")
                return 1.f;
            if (id == "iSoulAmountForConstantEffect")
                return 400.f;
            return std::nullopt;
        };
        return input;
    }
}

TEST(EnchantingMechanics, weaponClassOfMatchesNativeTable)
{
    EXPECT_EQ(EnchantingMechanics::weaponClassOf(ESM::Weapon::LongBladeOneHand), ESM::WeaponType::Melee);
    EXPECT_EQ(EnchantingMechanics::weaponClassOf(ESM::Weapon::MarksmanBow), ESM::WeaponType::Ranged);
    EXPECT_EQ(EnchantingMechanics::weaponClassOf(ESM::Weapon::MarksmanCrossbow), ESM::WeaponType::Ranged);
    EXPECT_EQ(EnchantingMechanics::weaponClassOf(ESM::Weapon::MarksmanThrown), ESM::WeaponType::Thrown);
    EXPECT_EQ(EnchantingMechanics::weaponClassOf(ESM::Weapon::Arrow), ESM::WeaponType::Ammo);
    EXPECT_EQ(EnchantingMechanics::weaponClassOf(ESM::Weapon::Bolt), ESM::WeaponType::Ammo);
}

TEST(EnchantingMechanics, isEnchantableMatchesNativeFilter)
{
    EnchantingMechanicsInput input = makeInput();
    input.itemType = ESM::Weapon::sRecordId;
    EXPECT_TRUE(EnchantingMechanics::isEnchantable(input));
    input.itemType = ESM::Armor::sRecordId;
    EXPECT_TRUE(EnchantingMechanics::isEnchantable(input));
    input.itemType = ESM::Clothing::sRecordId;
    EXPECT_TRUE(EnchantingMechanics::isEnchantable(input));

    // Only scroll-type books are enchantable.
    input.itemType = ESM::Book::sRecordId;
    input.bookIsScroll = false;
    EXPECT_FALSE(EnchantingMechanics::isEnchantable(input));
    input.bookIsScroll = true;
    EXPECT_TRUE(EnchantingMechanics::isEnchantable(input));

    input.itemType = 0;
    EXPECT_FALSE(EnchantingMechanics::isEnchantable(input));
}

TEST(EnchantingMechanics, effectCostsMatchVanillaFormula)
{
    EnchantingMechanicsInput input = makeInput();
    // Vanilla: cost = ((min + max) * duration + area) * baseCost * mult * 0.05
    // with min/max/area clamped to >= 1; target range multiplies by 1.5.
    input.effects = { effect("FireDamage", 10, 20, 5, 0) };
    // ((10 + 20) * 5 + 1) * 1 * 1 * 0.05 = 7.55
    EXPECT_FLOAT_EQ(EnchantingMechanics::effectCosts(input)[0], 7.55f);

    input.effects[0].mRange = ESM::RT_Target;
    EXPECT_FLOAT_EQ(EnchantingMechanics::effectCosts(input)[0], 7.55f * 1.5f);

    // Zero magnitudes and areas are clamped to 1, exactly like native.
    input.effects = { effect("FireDamage", 0, 0, 0, 0) };
    // ((1 + 1) * 0 + 1) * 0.05 = 0.05 -> floored up to 1
    EXPECT_FLOAT_EQ(EnchantingMechanics::effectCosts(input)[0], 1.f);

    // A second effect with a different base cost accumulates into the running
    // total, exactly like native (the pushed cost is the accumulated value).
    input.effects = { effect("FireDamage", 1, 1, 1, 0), effect("FrostDamage", 1, 1, 1, 0) };
    const auto costs = EnchantingMechanics::effectCosts(input);
    ASSERT_EQ(costs.size(), 2u);
    // ((1 + 1) * 1 + 1) * 1 * 0.05 = 0.15 -> 1
    EXPECT_FLOAT_EQ(costs[0], 1.f);
    // 1 + ((1 + 1) * 1 + 1) * 2 * 0.05 = 1 + 0.3
    EXPECT_FLOAT_EQ(costs[1], 1.3f);
}

TEST(EnchantingMechanics, constantEffectUsesConstantDurationMult)
{
    EnchantingMechanicsInput input = makeInput();
    input.castStyle = ESM::Enchantment::ConstantEffect;
    // With fEnchantmentConstantDurationMult = 1 the duration is ignored:
    // ((1 + 1) * 1 + 1) * 1 * 0.05 = 0.15 -> 1
    input.effects = { effect("FireDamage", 1, 1, 100, 0) };
    EXPECT_FLOAT_EQ(EnchantingMechanics::effectCosts(input)[0], 1.f);
    EXPECT_EQ(EnchantingMechanics::baseCastCost(input), 0);
    EXPECT_EQ(EnchantingMechanics::enchantmentCharge(input, 1), 0);
}

TEST(EnchantingMechanics, enchantPointsPreciseFloorsPerEffect)
{
    EnchantingMechanicsInput input = makeInput();
    input.effects = { effect("FireDamage", 10, 20, 5, 0), effect("FrostDamage", 10, 20, 5, 0) };
    // The per-effect costs accumulate: 7.55 then 7.55 + 15.1 = 22.65.
    EXPECT_FLOAT_EQ(EnchantingMechanics::enchantPoints(input, true), 30.2f);
    // 7 + 22
    EXPECT_FLOAT_EQ(EnchantingMechanics::enchantPoints(input, false), 29.f);
    EXPECT_EQ(EnchantingMechanics::baseCastCost(input), 29);
}

TEST(EnchantingMechanics, effectiveCastCostScalesWithEnchantSkill)
{
    EnchantingMechanicsInput input = makeInput();
    input.effects = { effect("FireDamage", 1, 1, 1, 0) }; // base cast cost 1
    input.enchantSkill = 10.f;
    EXPECT_EQ(EnchantingMechanics::effectiveCastCost(input), 1);
    input.enchantSkill = 50.f;
    EXPECT_EQ(EnchantingMechanics::effectiveCastCost(input), 1);
    // A more expensive enchantment exposes the skill scaling.
    input.effects = { effect("FrostDamage", 100, 100, 10, 0) };
    // ((100 + 100) * 10 + 1) * 2 * 0.05 = 200.1 -> cost 200
    EXPECT_EQ(EnchantingMechanics::baseCastCost(input), 200);
    input.enchantSkill = 10.f;
    EXPECT_EQ(EnchantingMechanics::effectiveCastCost(input), 200);
    input.enchantSkill = 50.f;
    // 200 - (200 / 100) * 40 = 120
    EXPECT_EQ(EnchantingMechanics::effectiveCastCost(input), 120);
}

TEST(EnchantingMechanics, maxEnchantValueUsesCapacityAndMult)
{
    EnchantingMechanicsInput input = makeInput();
    input.enchantCapacity = 10;
    EXPECT_EQ(EnchantingMechanics::maxEnchantValue(input), 10);
    input.enchantCapacity = 0;
    EXPECT_EQ(EnchantingMechanics::maxEnchantValue(input), 0);
}

TEST(EnchantingMechanics, enchantChanceMatchesNativeFormula)
{
    EnchantingMechanicsInput input = makeInput();
    input.effects = { effect("FireDamage", 10, 20, 5, 0) }; // points 7.55
    // x = (50 - 7.55 * 1 * 1 * 1 + 0.2 * 50 + 0.1 * 50) * 1 = 57.45
    EXPECT_EQ(EnchantingMechanics::enchantChance(input), 57);

    // Constant effect uses the constant duration multiplier: points drop to
    // ((10 + 20) * 1 + 1) * 0.05 = 1.55 -> x = 50 - 1.55 + 15 = 63.45.
    input.castStyle = ESM::Enchantment::ConstantEffect;
    EXPECT_EQ(EnchantingMechanics::enchantChance(input), 63);

    input.fatigueTerm = 0.5f;
    EXPECT_EQ(EnchantingMechanics::enchantChance(input), 31);
}

TEST(EnchantingMechanics, enchantItemsCountHandlesAmmoAndThrown)
{
    EnchantingMechanicsInput input = makeInput();
    input.effects = { effect("FireDamage", 10, 20, 5, 0) }; // points 7.55

    // Non-projectile items enchant exactly one.
    EXPECT_EQ(EnchantingMechanics::enchantItemsCount(input, 10), 1);

    // Ammo scales with the soul charge and the configured multiplier.
    input.weaponType = ESM::Weapon::Arrow;
    input.projectilesEnchantMultiplier = 0.f; // default: one at a time
    EXPECT_EQ(EnchantingMechanics::enchantItemsCount(input, 10), 1);

    input.projectilesEnchantMultiplier = 2.f;
    // int(400 * 2 / 7.55) = 105 -> clamped to the available stack
    EXPECT_EQ(EnchantingMechanics::enchantItemsCount(input, 10), 10);
    EXPECT_EQ(EnchantingMechanics::enchantItemsCount(input, 3), 3);

    input.weaponType = ESM::Weapon::MarksmanThrown;
    EXPECT_EQ(EnchantingMechanics::enchantItemsCount(input, 4), 4);
}

TEST(EnchantingMechanics, typeMultiplierAppliesOnlyToProjectiles)
{
    EnchantingMechanicsInput input = makeInput();
    input.effects = { effect("FireDamage", 10, 20, 5, 0) };
    input.projectilesEnchantMultiplier = 2.f;

    EXPECT_FLOAT_EQ(EnchantingMechanics::typeMultiplier(input), 1.f);

    input.weaponType = ESM::Weapon::Arrow;
    EXPECT_FLOAT_EQ(EnchantingMechanics::typeMultiplier(input), 0.125f);

    input.projectilesEnchantMultiplier = 0.f;
    EXPECT_FLOAT_EQ(EnchantingMechanics::typeMultiplier(input), 1.f);
}

TEST(EnchantingMechanics, enchantmentChargeDividesGemChargeByCount)
{
    EnchantingMechanicsInput input = makeInput();
    input.gemCharge = 400;
    EXPECT_EQ(EnchantingMechanics::enchantmentCharge(input, 1), 400);
    EXPECT_EQ(EnchantingMechanics::enchantmentCharge(input, 4), 100);
    input.castStyle = ESM::Enchantment::ConstantEffect;
    EXPECT_EQ(EnchantingMechanics::enchantmentCharge(input, 1), 0);
}

TEST(EnchantingMechanics, enchantPriceIsZeroWithoutBarterInputs)
{
    EnchantingMechanicsInput input = makeInput();
    input.effects = { effect("FireDamage", 10, 20, 5, 0) }; // final cost 7.55
    EXPECT_EQ(EnchantingMechanics::enchantPrice(input, 1), 0);
}

TEST(EnchantingMechanics, enchantPriceMatchesNativeBarterCore)
{
    EnchantingMechanicsInput input = makeInput();
    input.effects = { effect("FireDamage", 10, 20, 5, 0) }; // final cost 7.55 -> base 7

    EnchantingBarterInput barter;
    barter.playerMercantile = 100.f;
    barter.playerLuck = 100.f;
    barter.playerPersonality = 100.f;
    barter.playerFatigueTerm = 1.f;
    barter.enchanterMercantile = 0.f;
    barter.enchanterLuck = 0.f;
    barter.enchanterPersonality = 0.f;
    barter.enchanterFatigueTerm = 1.f;
    barter.disposition = 100;
    input.barter = barter;

    // a=100 b=10 c=10; pcTerm = (100-50+120)*1 = 170; npcTerm = 0
    // buyTerm = 0.01 * (100 - 0.5*170) = 0.15; offer = int(7*0.15) = 1
    EXPECT_EQ(EnchantingMechanics::enchantPrice(input, 1), 1);

    // A neutral disposition keeps the base price.
    barter.disposition = 50;
    barter.playerMercantile = 0.f;
    barter.playerLuck = 0.f;
    barter.playerPersonality = 0.f;
    input.barter = barter;
    // pcTerm = 0; buyTerm = 1; offer = 7
    EXPECT_EQ(EnchantingMechanics::enchantPrice(input, 1), 7);

    // Creature merchants never apply the barter adjustment.
    barter.creatureMerchant = true;
    input.barter = barter;
    EXPECT_EQ(EnchantingMechanics::enchantPrice(input, 1), 7);

    // The final price multiplies by the enchanted count and type multiplier.
    barter.creatureMerchant = false;
    input.barter = barter;
    EXPECT_EQ(EnchantingMechanics::enchantPrice(input, 3), 21);

    // A zero final cost still costs at least one gold.
    input.effects = { effect("FireDamage", 0, 0, 0, 0) };
    EXPECT_EQ(EnchantingMechanics::enchantPrice(input, 1), 1);
}

TEST(EnchantingMechanics, nextCastStyleCyclesLikeNative)
{
    EnchantingMechanicsInput input = makeInput();

    // Melee weapon: CastOnce falls through to the default branch which lands
    // on WhenStrikes, then WhenUsed -> Constant (powerful soul) -> WhenStrikes.
    input.itemType = ESM::Weapon::sRecordId;
    input.weaponType = ESM::Weapon::LongBladeOneHand;
    EXPECT_EQ(EnchantingMechanics::nextCastStyle(input, ESM::Enchantment::CastOnce), ESM::Enchantment::WhenStrikes);
    EXPECT_EQ(EnchantingMechanics::nextCastStyle(input, ESM::Enchantment::WhenStrikes), ESM::Enchantment::WhenUsed);
    EXPECT_EQ(EnchantingMechanics::nextCastStyle(input, ESM::Enchantment::WhenUsed), ESM::Enchantment::ConstantEffect);
    EXPECT_EQ(
        EnchantingMechanics::nextCastStyle(input, ESM::Enchantment::ConstantEffect), ESM::Enchantment::WhenStrikes);

    // Weak soul: constant effect is unreachable.
    input.gemCharge = 100;
    EXPECT_EQ(EnchantingMechanics::nextCastStyle(input, ESM::Enchantment::WhenUsed), ESM::Enchantment::WhenStrikes);

    // Ammo never reaches constant effect and stays WhenStrikes.
    input.gemCharge = 400;
    input.weaponType = ESM::Weapon::Arrow;
    EXPECT_EQ(EnchantingMechanics::nextCastStyle(input, ESM::Enchantment::WhenStrikes), ESM::Enchantment::WhenStrikes);
    EXPECT_EQ(EnchantingMechanics::nextCastStyle(input, ESM::Enchantment::WhenUsed), ESM::Enchantment::WhenStrikes);

    // Books/scrolls are always CastOnce.
    input.itemType = ESM::Book::sRecordId;
    input.weaponType = -1;
    EXPECT_EQ(EnchantingMechanics::nextCastStyle(input, ESM::Enchantment::CastOnce), ESM::Enchantment::CastOnce);
}

TEST(EnchantingMechanics, validCastStylesMatchReachableSet)
{
    EnchantingMechanicsInput input = makeInput();

    input.itemType = ESM::Armor::sRecordId;
    input.weaponType = -1;
    input.gemCharge = 400;
    EXPECT_EQ(EnchantingMechanics::validCastStyles(input),
        (std::vector<int>{ ESM::Enchantment::WhenUsed, ESM::Enchantment::ConstantEffect }));
    input.gemCharge = 100;
    EXPECT_EQ(EnchantingMechanics::validCastStyles(input), (std::vector<int>{ ESM::Enchantment::WhenUsed }));

    input.itemType = ESM::Weapon::sRecordId;
    input.weaponType = ESM::Weapon::LongBladeOneHand;
    input.gemCharge = 400;
    // The native cycle from CastOnce visits WhenStrikes, WhenUsed, Constant.
    EXPECT_EQ(EnchantingMechanics::validCastStyles(input),
        (std::vector<int>{ ESM::Enchantment::WhenStrikes, ESM::Enchantment::WhenUsed,
            ESM::Enchantment::ConstantEffect }));

    input.weaponType = ESM::Weapon::MarksmanBow;
    EXPECT_EQ(EnchantingMechanics::validCastStyles(input),
        (std::vector<int>{ ESM::Enchantment::WhenUsed, ESM::Enchantment::ConstantEffect }));

    input.weaponType = ESM::Weapon::Arrow;
    EXPECT_EQ(EnchantingMechanics::validCastStyles(input), (std::vector<int>{ ESM::Enchantment::WhenStrikes }));

    input.itemType = ESM::Book::sRecordId;
    input.weaponType = -1;
    EXPECT_EQ(EnchantingMechanics::validCastStyles(input), (std::vector<int>{ ESM::Enchantment::CastOnce }));
}

TEST(EnchantingMechanics, rollSuccessIsDeterministicForFixedSeed)
{
    EnchantingMechanicsInput input = makeInput();
    input.effects = { effect("FireDamage", 1, 1, 1, 0) }; // cost 1, chance high

    Misc::Rng::Generator first(1234u);
    const bool a = EnchantingMechanics::rollSuccess(input, first);
    Misc::Rng::Generator second(1234u);
    const bool b = EnchantingMechanics::rollSuccess(input, second);
    EXPECT_EQ(a, b);
}

TEST(EnchantingMechanics, rollSuccessGuaranteedOutcomes)
{
    EnchantingMechanicsInput input = makeInput();
    // A very cheap enchantment with a huge skill is an automatic success.
    input.effects = { effect("FireDamage", 1, 1, 1, 0) };
    input.enchantSkill = 100.f;
    input.intelligence = 100.f;
    input.luck = 100.f;
    EXPECT_GE(EnchantingMechanics::enchantChance(input), 100);

    Misc::Rng::Generator prng(42u);
    EXPECT_TRUE(EnchantingMechanics::rollSuccess(input, prng));

    // A hopeless enchantment always fails the roll.
    input.enchantSkill = 0.f;
    input.intelligence = 0.f;
    input.luck = 0.f;
    input.effects = { effect("FrostDamage", 100, 100, 100, 100) };
    EXPECT_LE(EnchantingMechanics::enchantChance(input), 0);
    EXPECT_FALSE(EnchantingMechanics::rollSuccess(input, prng));

    // Paid services never roll.
    input.selfEnchanting = false;
    EXPECT_TRUE(EnchantingMechanics::rollSuccess(input, prng));
}

TEST(EnchantingMechanics, missingContentThrows)
{
    EnchantingMechanicsInput input = makeInput();
    input.effects = { effect("UnknownEffect", 1, 1, 1, 0) };
    EXPECT_THROW(EnchantingMechanics::effectCosts(input), std::runtime_error);

    input.effects = { effect("FireDamage", 1, 1, 1, 0) };
    input.gmst = [](std::string_view) -> std::optional<float> { return std::nullopt; };
    EXPECT_THROW(EnchantingMechanics::effectCosts(input), std::runtime_error);
    EXPECT_THROW(EnchantingMechanics::enchantChance(input), std::runtime_error);
    EXPECT_THROW(EnchantingMechanics::validCastStyles(input), std::runtime_error);
}
