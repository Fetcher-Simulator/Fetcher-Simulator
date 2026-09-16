#pragma once
#include <algorithm>
#include <components/enchanting/EnchantingMechanics.hpp>
#include <cmath>
#include <components/esm3/effectlist.hpp>
#include <components/esm3/loadmgef.hpp>
#include <limits>
#include <stdexcept>

namespace mwmp
{
    inline int spellmakingPrice(float cost, float valueMultiplier, const Crafting::EnchantingBarterInput& barter)
    {
        const float base = cost * valueMultiplier;
        if (!std::isfinite(base) || base < 0 || base >= static_cast<float>(std::numeric_limits<int>::max()))
            throw std::runtime_error("Spellmaking price is out of range");
        return Crafting::serviceBarterOffer(std::max(1, static_cast<int>(base)), barter);
    }

    // The native player-spell formula, including the cumulative Target multiplier.
    // Shared by the preview and authoritative service; effect order is significant.
    template <class Lookup>
    float spellmakingCost(const std::vector<ESM::ENAMstruct>& effects, Lookup&& lookup, float multiplier)
    {
        float cost = 0.f;
        for (const auto& effect : effects)
        {
            const auto& magic = lookup(effect.mEffectID);
            const auto flags = magic.mData.mFlags;
            const int low = flags & ESM::MagicEffect::NoMagnitude ? 1 : std::max(1, effect.mMagnMin);
            const int high = flags & ESM::MagicEffect::NoMagnitude ? 1 : std::max(1, effect.mMagnMax);
            int duration = flags & ESM::MagicEffect::NoDuration ? 1 : effect.mDuration;
            if (!(flags & ESM::MagicEffect::AppliedOnce))
                duration = std::max(1, duration);
            float value = 0.5f * (low + high);
            value *= 0.1f * magic.mData.mBaseCost;
            value *= 1 + duration;
            value += 0.05f * std::max(1, effect.mArea) * magic.mData.mBaseCost;
            cost += std::max(1.f, value * multiplier);
            if (effect.mRange == ESM::RT_Target)
                cost *= 1.5f;
        }
        if (!std::isfinite(cost) || cost < 0 || cost >= static_cast<float>(std::numeric_limits<int>::max()))
            throw std::runtime_error("Spell cost is out of range");
        return cost;
    }
}
