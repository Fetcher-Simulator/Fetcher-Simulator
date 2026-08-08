#include "EnchantingMechanics.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadbook.hpp>
#include <components/esm3/loadclot.hpp>
#include <components/esm3/loadweap.hpp>

namespace
{
    float requireGmst(const Crafting::EnchantingMechanicsInput& input, std::string_view id)
    {
        const std::optional<float> value = input.gmst ? input.gmst(id) : std::nullopt;
        if (!value)
            throw std::runtime_error("enchanting GMST is missing from authoritative content: " + std::string(id));
        return *value;
    }

    float requireBaseCost(const Crafting::EnchantingMechanicsInput& input, const ESM::RefId& effectId)
    {
        const std::optional<Crafting::MagicEffectData> effect
            = input.magicEffect ? input.magicEffect(effectId) : std::nullopt;
        if (!effect)
            throw std::runtime_error("enchanting magic-effect record is missing from authoritative content");
        return effect->baseCost;
    }
}

namespace Crafting
{
    int EnchantingMechanics::weaponClassOf(int weaponType)
    {
        // The weapon-class column of the hardcoded native weapon-type table
        // (mwmechanics/weapontype.cpp). Only this column participates in the
        // enchanting formulas; keep it in one place so the client and server
        // can never disagree.
        switch (weaponType)
        {
            case ESM::Weapon::MarksmanBow:
            case ESM::Weapon::MarksmanCrossbow:
                return ESM::WeaponType::Ranged;
            case ESM::Weapon::MarksmanThrown:
                return ESM::WeaponType::Thrown;
            case ESM::Weapon::Arrow:
            case ESM::Weapon::Bolt:
                return ESM::WeaponType::Ammo;
            default:
                return ESM::WeaponType::Melee;
        }
    }

    bool EnchantingMechanics::isEnchantable(const EnchantingMechanicsInput& input)
    {
        switch (input.itemType)
        {
            case ESM::Weapon::sRecordId:
            case ESM::Armor::sRecordId:
            case ESM::Clothing::sRecordId:
                return true;
            case ESM::Book::sRecordId:
                // Only scrolls (books with the isScroll flag) are enchantable,
                // matching the native UI filter.
                return input.bookIsScroll;
            default:
                return false;
        }
    }

    std::vector<float> EnchantingMechanics::effectCosts(const EnchantingMechanicsInput& input)
    {
        /*
         * Vanilla enchant cost formula:
         *
         *  Touch/Self:          (min + max) * baseCost * 0.025 * duration + area * baseCost * 0.025
         *  Target:       1.5 * ((min + max) * baseCost * 0.025 * duration + area * baseCost * 0.025)
         *  Constant eff:        (min + max) * baseCost * 2.5              + area * baseCost * 0.025
         *
         *  For multiple effects - cost of each effect is multiplied by number of effects that follows +1.
         *
         *  Note: Minimal value inside formula for 'min' and 'max' is 1. So in vanilla:
         *        (0 + 0) == (1 + 0) == (1 + 1) => 2 or (2 + 0) == (1 + 2) => 3
         *
         *  Formula on UESPWiki is not entirely correct.
         */
        std::vector<float> costs;
        if (input.effects.empty())
            return costs;

        costs.reserve(input.effects.size());
        const float fEffectCostMult = requireGmst(input, "fEffectCostMult");
        const float fEnchantmentConstantDurationMult = requireGmst(input, "fEnchantmentConstantDurationMult");

        float cost = 0.f;
        for (const ESM::ENAMstruct& effect : input.effects)
        {
            const float baseCost = requireBaseCost(input, effect.mEffectID);
            const int magMin = std::max(1, effect.mMagnMin);
            const int magMax = std::max(1, effect.mMagnMax);
            const int area = std::max(1, effect.mArea);
            float duration = static_cast<float>(effect.mDuration);
            if (input.castStyle == ESM::Enchantment::ConstantEffect)
                duration = fEnchantmentConstantDurationMult;

            cost += ((magMin + magMax) * duration + area) * baseCost * fEffectCostMult * 0.05f;

            cost = std::max(1.f, cost);

            if (effect.mRange == ESM::RT_Target)
                cost *= 1.5f;

            costs.push_back(cost);
        }

        return costs;
    }

    float EnchantingMechanics::enchantPoints(const EnchantingMechanicsInput& input, bool precise)
    {
        float enchantmentCost = 0.f;
        for (float cost : effectCosts(input))
            enchantmentCost += precise ? cost : std::floor(cost);

        return enchantmentCost;
    }

    int EnchantingMechanics::baseCastCost(const EnchantingMechanicsInput& input)
    {
        if (input.castStyle == ESM::Enchantment::ConstantEffect)
            return 0;

        return static_cast<int>(enchantPoints(input, false));
    }

    int EnchantingMechanics::effectiveCastCost(const EnchantingMechanicsInput& input)
    {
        const int baseCost = baseCastCost(input);
        const float result
            = static_cast<float>(baseCost) - (static_cast<float>(baseCost) / 100) * (input.enchantSkill - 10);

        return static_cast<int>((result < 1) ? 1 : result);
    }

    int EnchantingMechanics::maxEnchantValue(const EnchantingMechanicsInput& input)
    {
        return static_cast<int>(input.enchantCapacity * requireGmst(input, "fEnchantmentMult"));
    }

    int EnchantingMechanics::enchantChance(const EnchantingMechanicsInput& input)
    {
        const float fEnchantmentChanceMult = requireGmst(input, "fEnchantmentChanceMult");
        const float fEnchantmentConstantChanceMult = requireGmst(input, "fEnchantmentConstantChanceMult");

        float x = (input.enchantSkill - enchantPoints(input) * fEnchantmentChanceMult * typeMultiplier(input)
                      * static_cast<float>(enchantItemsCount(input, input.availableCount))
                      + 0.2f * input.intelligence + 0.1f * input.luck)
            * input.fatigueTerm;
        if (input.castStyle == ESM::Enchantment::ConstantEffect)
            x *= fEnchantmentConstantChanceMult;

        return static_cast<int>(x);
    }

    int EnchantingMechanics::enchantItemsCount(const EnchantingMechanicsInput& input, int availableCount)
    {
        int count = 1;
        const float enchantPointsValue = enchantPoints(input);
        if (input.weaponType != -1 && enchantPointsValue > 0)
        {
            const int weaponClass = weaponClassOf(input.weaponType);
            if (weaponClass == ESM::WeaponType::Thrown || weaponClass == ESM::WeaponType::Ammo)
            {
                count = std::clamp(
                    static_cast<int>(input.gemCharge * input.projectilesEnchantMultiplier / enchantPointsValue), 1,
                    std::max(1, availableCount));
            }
        }

        return count;
    }

    float EnchantingMechanics::typeMultiplier(const EnchantingMechanicsInput& input)
    {
        if (input.projectilesEnchantMultiplier > 0 && input.weaponType != -1 && enchantPoints(input) > 0)
        {
            const int weaponClass = weaponClassOf(input.weaponType);
            if (weaponClass == ESM::WeaponType::Thrown || weaponClass == ESM::WeaponType::Ammo)
                return 0.125f;
        }

        return 1.f;
    }

    int EnchantingMechanics::enchantmentCharge(const EnchantingMechanicsInput& input, int count)
    {
        if (input.castStyle == ESM::Enchantment::ConstantEffect)
            return 0;

        return input.gemCharge / count;
    }

    int EnchantingMechanics::enchantPrice(const EnchantingMechanicsInput& input, int count)
    {
        if (!input.barter)
            return 0;

        // The base price uses the final (last) effect's accumulated cost,
        // exactly like native Enchanting::getEnchantPrice.
        const std::vector<float> costs = effectCosts(input);
        const float finalEffectCost = costs.empty() ? 0.f : costs.back();

        const int basePrice = static_cast<int>(finalEffectCost * requireGmst(input, "fEnchantmentValueMult"));

        // Native MechanicsManager::getBarterOffer with the caller-resolved
        // disposition and statistics. The special cases (zero base price and
        // creature merchants) return the base price unchanged.
        int offer = basePrice;
        const EnchantingBarterInput& barter = *input.barter;
        if (basePrice != 0 && !barter.creatureMerchant)
        {
            const float a = std::min(barter.playerMercantile, 100.f);
            const float b = std::min(0.1f * barter.playerLuck, 10.f);
            const float c = std::min(0.2f * barter.playerPersonality, 10.f);
            const float d = std::min(barter.enchanterMercantile, 100.f);
            const float e = std::min(0.1f * barter.enchanterLuck, 10.f);
            const float f = std::min(0.2f * barter.enchanterPersonality, 10.f);
            const float pcTerm = (barter.disposition - 50 + a + b + c) * barter.playerFatigueTerm;
            const float npcTerm = (d + e + f) * barter.enchanterFatigueTerm;
            const float buyTerm = 0.01f * (100 - 0.5f * (pcTerm - npcTerm));
            offer = std::max(1, static_cast<int>(basePrice * buyTerm));
        }

        const int price = static_cast<int>(offer * static_cast<int>(count * typeMultiplier(input)));
        return std::max(1, price);
    }

    int EnchantingMechanics::nextCastStyle(const EnchantingMechanicsInput& input, int current)
    {
        const bool powerfulSoul
            = input.gemCharge >= static_cast<int>(requireGmst(input, "iSoulAmountForConstantEffect"));

        if (input.itemType == ESM::Armor::sRecordId || input.itemType == ESM::Clothing::sRecordId)
        { // Armor or Clothing
            switch (current)
            {
                case ESM::Enchantment::WhenUsed:
                    if (powerfulSoul)
                        return ESM::Enchantment::ConstantEffect;
                    return ESM::Enchantment::WhenUsed;
                default: // takes care of Constant effect too
                    return ESM::Enchantment::WhenUsed;
            }
        }
        if (input.weaponType != -1)
        { // Weapon
            const int weaponClass = weaponClassOf(input.weaponType);
            switch (current)
            {
                case ESM::Enchantment::WhenStrikes:
                    if (weaponClass == ESM::WeaponType::Melee || weaponClass == ESM::WeaponType::Ranged)
                        return ESM::Enchantment::WhenUsed;
                    return ESM::Enchantment::WhenStrikes;
                case ESM::Enchantment::WhenUsed:
                    if (powerfulSoul && weaponClass != ESM::WeaponType::Ammo && weaponClass != ESM::WeaponType::Thrown)
                        return ESM::Enchantment::ConstantEffect;
                    if (weaponClass != ESM::WeaponType::Ranged)
                        return ESM::Enchantment::WhenStrikes;
                    return ESM::Enchantment::WhenUsed;
                default: // takes care of Constant effect too
                    if (weaponClass != ESM::WeaponType::Ranged)
                        return ESM::Enchantment::WhenStrikes;
                    return ESM::Enchantment::WhenUsed;
            }
        }
        if (input.itemType == ESM::Book::sRecordId)
        { // Scroll or Book
            return ESM::Enchantment::CastOnce;
        }

        // Fail case
        return ESM::Enchantment::CastOnce;
    }

    std::vector<int> EnchantingMechanics::validCastStyles(const EnchantingMechanicsInput& input)
    {
        // The native dialog always starts from CastOnce and advances the
        // cycle whenever the item or soul gem changes. A style is creatable
        // exactly when the cycle from CastOnce can settle on it.
        std::vector<int> visited;
        int current = ESM::Enchantment::CastOnce;
        for (;;)
        {
            current = nextCastStyle(input, current);
            if (std::find(visited.begin(), visited.end(), current) != visited.end())
                break;
            visited.push_back(current);
        }
        return visited;
    }

    bool EnchantingMechanics::rollSuccess(const EnchantingMechanicsInput& input, Misc::Rng::Generator& prng)
    {
        if (!input.selfEnchanting)
            return true;

        // Native: failure when chance <= roll, so success when roll < chance.
        return static_cast<int>(Misc::Rng::roll0to99(prng)) < enchantChance(input);
    }
}
