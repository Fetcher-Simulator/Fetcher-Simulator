#include <gtest/gtest.h>

#include <array>
#include <string_view>

#include <components/alchemy/AlchemyMechanics.hpp>
#include <components/esm3/loadappa.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/misc/rng.hpp>

namespace
{
    using Crafting::AlchemyMechanics;
    using Crafting::AlchemyMechanicsInput;
    using Crafting::MagicEffectData;

    ESM::RefId effectId(const char* id)
    {
        return ESM::RefId::stringRefId(id);
    }

    AlchemyMechanicsInput::Ingredient ingredient(
        float weight, std::string_view effect, std::string_view attribute = {})
    {
        AlchemyMechanicsInput::Ingredient result;
        result.weight = weight;
        result.count = 3;
        result.effectIds[0] = effectId(std::string(effect).c_str());
        if (!attribute.empty())
            result.attributes[0] = effectId(std::string(attribute).c_str());
        return result;
    }

    AlchemyMechanicsInput makeInput()
    {
        AlchemyMechanicsInput input;
        input.alchemySkill = 50.f;
        input.intelligence = 50.f;
        input.luck = 50.f;
        input.apparatusQuality[ESM::Apparatus::MortarPestle] = 1.f;
        input.apparatusQuality[ESM::Apparatus::Retort] = 1.f;
        input.magicEffect = [](const ESM::RefId& id) -> std::optional<MagicEffectData> {
            if (id == ESM::RefId::stringRefId("FireDamage"))
                return MagicEffectData{ 1.f, 0 };
            if (id == ESM::RefId::stringRefId("DamageHealth"))
                return MagicEffectData{ 1.f, ESM::MagicEffect::Harmful };
            if (id == ESM::RefId::stringRefId("NightEye"))
                return MagicEffectData{ 1.f, ESM::MagicEffect::NoMagnitude };
            if (id == ESM::RefId::stringRefId("Light"))
                return MagicEffectData{ 1.f, ESM::MagicEffect::NoDuration };
            return std::nullopt;
        };
        input.gmst = [](std::string_view id) -> std::optional<float> {
            if (id == "fPotionStrengthMult")
                return 1.f;
            if (id == "iAlchemyMod")
                return 1.f;
            if (id == "fPotionT1MagMult")
                return 1.f;
            if (id == "fPotionT1DurMult")
                return 1.f;
            return std::nullopt;
        };
        return input;
    }
}

TEST(AlchemyMechanics, listEffectsHandlesFewerThanTwoIngredients)
{
    AlchemyMechanicsInput input = makeInput();
    EXPECT_TRUE(AlchemyMechanics::listEffects(input).empty());

    input.ingredients.push_back(ingredient(1.f, "FireDamage"));
    EXPECT_TRUE(AlchemyMechanics::listEffects(input).empty());
}

TEST(AlchemyMechanics, listEffectsMatchesSharedIngredientEffectsInSlotOrder)
{
    AlchemyMechanicsInput input = makeInput();
    input.ingredients.push_back(ingredient(1.f, "FireDamage"));
    input.ingredients.push_back(ingredient(2.f, "FireDamage"));
    EXPECT_EQ(AlchemyMechanics::countIngredients(input), 2);

    const auto effects = AlchemyMechanics::listEffects(input);
    ASSERT_EQ(effects.size(), 1u);
    EXPECT_EQ(effects[0].id, effectId("FireDamage"));

    // An effect present in only one ingredient is not shared.
    input.ingredients[1].effectIds[0] = effectId("NightEye");
    EXPECT_TRUE(AlchemyMechanics::listEffects(input).empty());
}

TEST(AlchemyMechanics, effectsAndValueMatchNativeCalculation)
{
    AlchemyMechanicsInput input = makeInput();
    input.ingredients.push_back(ingredient(1.f, "FireDamage"));
    input.ingredients.push_back(ingredient(2.f, "FireDamage"));
    // x = skill + 0.1*int + 0.1*luck = 60; mortar 1; strength mult 1.
    // value = int(60 * iAlchemyMod) = 60.
    // magnitude/duration = (60 / 1) / baseCost 1 = 60; retort quality 1
    // (magnitude && duration) -> 60 + 1 = 61.

    const auto result = AlchemyMechanics::updateEffects(input);
    ASSERT_EQ(result.effects.size(), 1u);
    EXPECT_EQ(result.value, 60);
    EXPECT_EQ(result.effects[0].mEffectID, effectId("FireDamage"));
    EXPECT_EQ(result.effects[0].mMagnMin, 61);
    EXPECT_EQ(result.effects[0].mMagnMax, 61);
    EXPECT_EQ(result.effects[0].mDuration, 61);
    EXPECT_EQ(result.effects[0].mRange, 0);
    EXPECT_EQ(result.effects[0].mArea, 0);
}

TEST(AlchemyMechanics, harmfulEffectsUseAlembicAndDivide)
{
    AlchemyMechanicsInput input = makeInput();
    input.ingredients.push_back(ingredient(1.f, "DamageHealth"));
    input.ingredients.push_back(ingredient(2.f, "DamageHealth"));
    // Negative effect: alembic + calcinator, setup 1 -> quality =
    // 2*alembic + 3*calcinator = 5; value /= 5.
    input.apparatusQuality[ESM::Apparatus::Alembic] = 1.f;
    input.apparatusQuality[ESM::Apparatus::Calcinator] = 1.f;
    input.apparatusQuality[ESM::Apparatus::Retort] = std::nullopt;

    const auto result = AlchemyMechanics::updateEffects(input);
    ASSERT_EQ(result.effects.size(), 1u);
    EXPECT_EQ(result.effects[0].mMagnMin, 12); // 60 / 5
}

TEST(AlchemyMechanics, noMagnitudeAndNoDurationEffectsAreOne)
{
    AlchemyMechanicsInput input = makeInput();
    input.ingredients.push_back(ingredient(1.f, "NightEye"));
    input.ingredients.push_back(ingredient(2.f, "NightEye"));

    // NoMagnitude pins magnitude to 1. Duration still gets the retort bonus:
    // setup 2, quality = toolQuality + 0.5 (magnitude && duration is false)
    // -> 60 + 1.5 = 61.5 -> roundf = 62.
    const auto result = AlchemyMechanics::updateEffects(input);
    ASSERT_EQ(result.effects.size(), 1u);
    EXPECT_EQ(result.effects[0].mMagnMin, 1);
    EXPECT_EQ(result.effects[0].mDuration, 62);
}

TEST(AlchemyMechanics, potionWeightIsAverageIngredientWeight)
{
    AlchemyMechanicsInput input = makeInput();
    input.ingredients.push_back(ingredient(1.f, "FireDamage"));
    input.ingredients.push_back(ingredient(2.f, "FireDamage"));

    Misc::Rng::Generator prng(1234);
    const auto attempt = AlchemyMechanics::createSingle(input, prng, "Test Potion");
    ASSERT_TRUE(attempt.success);
    EXPECT_FLOAT_EQ(attempt.potion.weight, 1.5f);
    EXPECT_EQ(attempt.potion.value, 60);
    EXPECT_EQ(attempt.potion.name, "Test Potion");
    EXPECT_FALSE(attempt.potion.model.empty());
    EXPECT_FALSE(attempt.potion.icon.empty());
    EXPECT_EQ(attempt.potion.effects.size(), 1u);
}

TEST(AlchemyMechanics, successRollIsAuthoritativeAndDeterministicPerSeed)
{
    AlchemyMechanicsInput input = makeInput();
    input.alchemySkill = 0.f;
    input.intelligence = 0.f;
    input.luck = 0.f;
    input.ingredients.push_back(ingredient(1.f, "FireDamage"));
    input.ingredients.push_back(ingredient(2.f, "FireDamage"));

    // The same seed reproduces the same roll and therefore the same outcome.
    Misc::Rng::Generator first(99);
    const auto firstAttempt = AlchemyMechanics::createSingle(input, first, "Doomed");
    Misc::Rng::Generator replay(99);
    const auto replayAttempt = AlchemyMechanics::createSingle(input, replay, "Doomed");
    EXPECT_EQ(firstAttempt.success, replayAttempt.success);
    EXPECT_EQ(firstAttempt.potion.model, replayAttempt.potion.model);

    // A factor far above the roll range succeeds deterministically.
    input.alchemySkill = 500.f;
    input.intelligence = 500.f;
    input.luck = 500.f;
    Misc::Rng::Generator winning(99);
    EXPECT_TRUE(AlchemyMechanics::createSingle(input, winning, "Blessed").success);
}

TEST(AlchemyMechanics, readyStatusMirrorsNativeOrder)
{
    AlchemyMechanicsInput input = makeInput();
    input.ingredients.push_back(ingredient(1.f, "FireDamage"));
    input.ingredients.push_back(ingredient(2.f, "FireDamage"));

    EXPECT_EQ(AlchemyMechanics::getReadyStatus(input, "Potion"), AlchemyMechanics::Result::Success);
    EXPECT_EQ(AlchemyMechanics::getReadyStatus(input, ""), AlchemyMechanics::Result::NoName);

    input.apparatusQuality[ESM::Apparatus::MortarPestle] = std::nullopt;
    EXPECT_EQ(AlchemyMechanics::getReadyStatus(input, "Potion"), AlchemyMechanics::Result::NoMortarAndPestle);

    input.apparatusQuality[ESM::Apparatus::MortarPestle] = 1.f;
    input.ingredients.pop_back();
    EXPECT_EQ(AlchemyMechanics::getReadyStatus(input, "Potion"), AlchemyMechanics::Result::LessThanTwoIngredients);
}

TEST(AlchemyMechanics, countPotionsToBrewIsSmallestStack)
{
    AlchemyMechanicsInput input = makeInput();
    input.ingredients.push_back(ingredient(1.f, "FireDamage"));
    input.ingredients.push_back(ingredient(2.f, "FireDamage"));
    input.ingredients[0].count = 5;
    input.ingredients[1].count = 2;
    EXPECT_EQ(AlchemyMechanics::countPotionsToBrew(input, "Potion"), 2);
    EXPECT_EQ(AlchemyMechanics::countPotionsToBrew(input, ""), 0);
}

TEST(AlchemyMechanics, invalidContentThrows)
{
    AlchemyMechanicsInput input = makeInput();
    input.ingredients.push_back(ingredient(1.f, "UnknownEffect"));
    input.ingredients.push_back(ingredient(2.f, "UnknownEffect"));
    EXPECT_THROW(AlchemyMechanics::updateEffects(input), std::runtime_error);

    // Missing GMSTs are reported before any effect is quantified.
    AlchemyMechanicsInput missingGmst = makeInput();
    missingGmst.ingredients.push_back(ingredient(1.f, "FireDamage"));
    missingGmst.ingredients.push_back(ingredient(2.f, "FireDamage"));
    missingGmst.gmst = [](std::string_view) { return std::nullopt; };
    EXPECT_THROW(AlchemyMechanics::updateEffects(missingGmst), std::runtime_error);

    // Zero magic effect base cost is invalid content.
    AlchemyMechanicsInput zeroCost = makeInput();
    zeroCost.magicEffect = [](const ESM::RefId&) -> std::optional<MagicEffectData> {
        return MagicEffectData{ 0.f, 0 };
    };
    zeroCost.ingredients.push_back(ingredient(1.f, "FireDamage"));
    zeroCost.ingredients.push_back(ingredient(2.f, "FireDamage"));
    EXPECT_THROW(AlchemyMechanics::updateEffects(zeroCost), std::runtime_error);
}

TEST(AlchemyMechanics, meshSelectionIsBounded)
{
    AlchemyMechanicsInput input = makeInput();
    input.ingredients.push_back(ingredient(1.f, "FireDamage"));
    input.ingredients.push_back(ingredient(2.f, "FireDamage"));

    const auto& meshes = AlchemyMechanics::meshes();
    ASSERT_EQ(meshes.size(), 6u);
    for (unsigned int seed = 0; seed < 64; ++seed)
    {
        Misc::Rng::Generator prng(seed);
        const auto attempt = AlchemyMechanics::createSingle(input, prng, "Mesh");
        ASSERT_TRUE(attempt.success);
        bool matches = false;
        for (const std::string_view mesh : meshes)
        {
            if (attempt.potion.model == "m\\misc_potion_" + std::string(mesh) + "_01.nif"
                && attempt.potion.icon == "m\\tx_potion_" + std::string(mesh) + "_01.dds")
            {
                matches = true;
                break;
            }
        }
        EXPECT_TRUE(matches) << "unexpected potion model " << attempt.potion.model;
    }
}
