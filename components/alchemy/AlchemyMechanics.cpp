#include "AlchemyMechanics.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <stdexcept>

#include <components/esm3/loadappa.hpp>
#include <components/esm3/loadmgef.hpp>

namespace
{
    constexpr std::size_t sNumEffects = 4;

    std::optional<Crafting::EffectKey> toKey(const Crafting::AlchemyMechanicsInput::Ingredient& ingredient, std::size_t i)
    {
        if (ingredient.effectIds[i].empty())
            return {};
        ESM::RefId arg = ingredient.skills[i];
        if (arg.empty())
            arg = ingredient.attributes[i];
        return Crafting::EffectKey{ ingredient.effectIds[i], std::move(arg) };
    }

    bool containsEffect(const Crafting::AlchemyMechanicsInput::Ingredient& ingredient, const Crafting::EffectKey& effect)
    {
        for (std::size_t j = 0; j < sNumEffects; ++j)
        {
            if (toKey(ingredient, j) == effect)
                return true;
        }
        return false;
    }

    void applyTools(const Crafting::AlchemyMechanicsInput& input, int flags, float& value)
    {
        const bool magnitude = !(flags & ESM::MagicEffect::NoMagnitude);
        const bool duration = !(flags & ESM::MagicEffect::NoDuration);
        const bool negative = (flags & ESM::MagicEffect::Harmful) != 0;

        const int tool = negative ? ESM::Apparatus::Alembic : ESM::Apparatus::Retort;

        int setup = 0;

        if (input.apparatusQuality[tool] && input.apparatusQuality[ESM::Apparatus::Calcinator])
            setup = 1;
        else if (input.apparatusQuality[tool])
            setup = 2;
        else if (input.apparatusQuality[ESM::Apparatus::Calcinator])
            setup = 3;
        else
            return;

        const float toolQuality
            = setup == 1 || setup == 2 ? *input.apparatusQuality[tool] : 0;
        const float calcinatorQuality
            = setup == 1 || setup == 3 ? *input.apparatusQuality[ESM::Apparatus::Calcinator] : 0;

        float quality = 1;

        switch (setup)
        {
            case 1:

                quality = negative ? 2 * toolQuality + 3 * calcinatorQuality
                                   : (magnitude && duration ? 2 * toolQuality + calcinatorQuality
                                                            : 2 / 3.0f * (toolQuality + calcinatorQuality) + 0.5f);
                break;

            case 2:

                quality = negative ? 1 + toolQuality
                                   : (magnitude && duration ? toolQuality : toolQuality + 0.5f);
                break;

            case 3:

                quality = magnitude && duration ? calcinatorQuality : calcinatorQuality + 0.5f;
                break;
        }

        if (setup == 3 || !negative)
        {
            value += quality;
        }
        else
        {
            if (quality == 0)
                throw std::runtime_error("invalid derived alchemy apparatus quality");

            value /= quality;
        }
    }

    float requireGmst(const Crafting::AlchemyMechanicsInput& input, std::string_view id)
    {
        if (!input.gmst)
            throw std::runtime_error(std::format("alchemy mechanics have no GMST source for {}", id));
        const std::optional<float> value = input.gmst(id);
        if (!value)
            throw std::runtime_error(std::format("invalid gmst: {}", id));
        return *value;
    }

    Crafting::MagicEffectData requireMagicEffect(
        const Crafting::AlchemyMechanicsInput& input, const ESM::RefId& id)
    {
        if (!input.magicEffect)
            throw std::runtime_error("alchemy mechanics have no magic effect source");
        const std::optional<Crafting::MagicEffectData> effect = input.magicEffect(id);
        if (!effect)
            throw std::runtime_error(
                std::format("magic effect {} is not available to alchemy mechanics", id.getRefIdString()));
        return *effect;
    }
}

namespace Crafting
{
    std::vector<EffectKey> AlchemyMechanics::listEffects(const AlchemyMechanicsInput& input)
    {
        // We care about the order of these effects as each effect can affect the next when applied.
        // The player can affect effect order by placing ingredients into different slots
        std::vector<EffectKey> effects;
        if (input.ingredients.size() < 2)
            return effects;

        for (std::size_t slotI = 0; slotI + 1 < input.ingredients.size(); ++slotI)
        {
            const AlchemyMechanicsInput::Ingredient& ingredient = input.ingredients[slotI];
            for (std::size_t slotJ = slotI + 1; slotJ < input.ingredients.size(); ++slotJ)
            {
                const AlchemyMechanicsInput::Ingredient& ingredient2 = input.ingredients[slotJ];
                for (std::size_t i = 0; i < sNumEffects; ++i)
                {
                    if (const auto key = toKey(ingredient, i))
                    {
                        if (std::find(effects.begin(), effects.end(), *key) != effects.end())
                            continue;
                        if (containsEffect(ingredient2, *key))
                            effects.push_back(*key);
                    }
                }
            }
        }
        return effects;
    }

    int AlchemyMechanics::countIngredients(const AlchemyMechanicsInput& input)
    {
        return static_cast<int>(input.ingredients.size());
    }

    float AlchemyMechanics::getAlchemyFactor(const AlchemyMechanicsInput& input)
    {
        return (input.alchemySkill + 0.1f * input.intelligence + 0.1f * input.luck);
    }

    AlchemyMechanics::EffectsResult AlchemyMechanics::updateEffects(const AlchemyMechanicsInput& input)
    {
        EffectsResult result;

        if (countIngredients(input) < 2 || !input.apparatusQuality[ESM::Apparatus::MortarPestle])
            return result;

        // find effects
        std::vector<EffectKey> effects = listEffects(input);

        // general alchemy factor
        float x = getAlchemyFactor(input);

        x *= *input.apparatusQuality[ESM::Apparatus::MortarPestle];
        x *= requireGmst(input, "fPotionStrengthMult");

        // value
        result.value = static_cast<int>(x * requireGmst(input, "iAlchemyMod"));

        // build quantified effect list
        for (const auto& effectKey : effects)
        {
            const MagicEffectData magicEffect = requireMagicEffect(input, effectKey.id);

            if (magicEffect.baseCost <= 0)
            {
                const std::string os
                    = std::format("invalid base cost for magic effect {}", effectKey.id.getRefIdString());
                throw std::runtime_error(os);
            }

            const float fPotionT1MagMul = requireGmst(input, "fPotionT1MagMult");

            if (fPotionT1MagMul <= 0)
                throw std::runtime_error("invalid gmst: fPotionT1MagMul");

            const float fPotionT1DurMult = requireGmst(input, "fPotionT1DurMult");

            if (fPotionT1DurMult <= 0)
                throw std::runtime_error("invalid gmst: fPotionT1DurMult");

            float magnitude
                = (magicEffect.flags & ESM::MagicEffect::NoMagnitude) ? 1.0f : (x / fPotionT1MagMul) / magicEffect.baseCost;
            float duration
                = (magicEffect.flags & ESM::MagicEffect::NoDuration) ? 1.0f : (x / fPotionT1DurMult) / magicEffect.baseCost;

            if (!(magicEffect.flags & ESM::MagicEffect::NoMagnitude))
                applyTools(input, static_cast<int>(magicEffect.flags), magnitude);

            if (!(magicEffect.flags & ESM::MagicEffect::NoDuration))
                applyTools(input, static_cast<int>(magicEffect.flags), duration);

            duration = roundf(duration);
            magnitude = roundf(magnitude);

            if (magnitude > 0 && duration > 0)
            {
                ESM::ENAMstruct effect;
                effect.mEffectID = effectKey.id;

                if (magicEffect.flags & ESM::MagicEffect::TargetSkill)
                    effect.mSkill = effectKey.arg;
                else if (magicEffect.flags & ESM::MagicEffect::TargetAttribute)
                    effect.mAttribute = effectKey.arg;

                effect.mRange = 0;
                effect.mArea = 0;

                effect.mDuration = static_cast<int>(duration);
                effect.mMagnMin = effect.mMagnMax = static_cast<int>(magnitude);

                result.effects.push_back(effect);
            }
        }
        return result;
    }

    AlchemyMechanics::Result AlchemyMechanics::getReadyStatus(
        const AlchemyMechanicsInput& input, const std::string& potionName)
    {
        if (!input.apparatusQuality[ESM::Apparatus::MortarPestle])
            return Result::NoMortarAndPestle;

        if (countIngredients(input) < 2)
            return Result::LessThanTwoIngredients;

        if (potionName.empty())
            return Result::NoName;

        if (listEffects(input).empty())
            return Result::NoEffects;

        return Result::Success;
    }

    int AlchemyMechanics::countPotionsToBrew(const AlchemyMechanicsInput& input, const std::string& potionName)
    {
        if (getReadyStatus(input, potionName) != Result::Success)
            return 0;

        int toBrew = -1;

        for (const auto& ingredient : input.ingredients)
        {
            const int count = ingredient.count;
            if ((count > 0 && count < toBrew) || toBrew < 0)
                toBrew = count;
        }

        return toBrew;
    }

    AlchemyMechanics::Attempt AlchemyMechanics::createSingle(
        const AlchemyMechanicsInput& input, Misc::Rng::Generator& prng, const std::string& potionName)
    {
        Attempt attempt;

        const EffectsResult quantified = updateEffects(input);
        if (quantified.effects.empty())
        {
            // all effects were nullified due to insufficient skill
            return attempt;
        }
        if (getAlchemyFactor(input) < Misc::Rng::roll0to99(prng))
            return attempt;

        attempt.success = true;
        PotionDefinition& potion = attempt.potion;
        potion.name = potionName;
        potion.value = quantified.value;
        potion.effects = quantified.effects;

        float weight = 0.f;
        for (const auto& ingredient : input.ingredients)
            weight += ingredient.weight;
        if (!input.ingredients.empty())
            weight /= static_cast<float>(input.ingredients.size());
        potion.weight = weight;

        const int index = Misc::Rng::rollDice(static_cast<int>(meshes().size()), prng);
        potion.model = std::format("m\\misc_potion_{}_01.nif", meshes()[index]);
        potion.icon = std::format("m\\tx_potion_{}_01.dds", meshes()[index]);

        return attempt;
    }

    const std::array<std::string_view, 6>& AlchemyMechanics::meshes()
    {
        static constexpr std::array<std::string_view, 6> sMeshes
            = { "standard", "bargain", "cheap", "fresh", "exclusive", "quality" };
        return sMeshes;
    }
}
