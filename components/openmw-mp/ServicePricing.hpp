#pragma once
#include <algorithm>
#include <cmath>
#include <optional>
#include <components/enchanting/EnchantingMechanics.hpp>
#include <components/esm/attr.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadskil.hpp>
#include <components/openmw-mp/Base/BasePlayer.hpp>

namespace mwmp
{
    template <class Gmst>
    float serviceFatigueTerm(const DynamicStats& stats, const Gmst& gmst)
    {
        const float maximum = stats.fatigue.base + stats.fatigue.mod;
        const float normalized = std::floor(maximum) == 0 ? 1 : std::max(0.f, stats.fatigue.current / maximum);
        return gmst("fFatigueBase") - gmst("fFatigueMult") * (1 - normalized);
    }

    // Supported authoritative service model: content NPC attributes/disposition,
    // synced player attributes, bounty and both actors' fatigue. Runtime Charm,
    // faction/disease/weapon-drawn disposition are
    // deliberately excluded. MP previews and server validation must use this same
    // resolver; single-player continues to resolve full native actor state.
    template <class Gmst>
    Crafting::EnchantingBarterInput serviceBarterInput(
        const ESM::NPC* npc, const BasePlayer& player, const DynamicStats* actorStats, const Gmst& gmst, std::optional<int> baseDisposition = std::nullopt)
    {
        Crafting::EnchantingBarterInput result;
        result.creatureMerchant = npc == nullptr;
        const int mercantile = ESM::Skill::refIdToIndex(ESM::Skill::Mercantile);
        const int personality = ESM::Attribute::refIdToIndex(ESM::Attribute::Personality);
        const int luck = ESM::Attribute::refIdToIndex(ESM::Attribute::Luck);
        const auto attribute = [&](int i) {
            const auto& value = player.attributes[i];
            return std::max(0.f, static_cast<float>(value.base) - value.damage + value.mod);
        };
        result.playerMercantile = std::max(0.f, player.skills[mercantile].base
            - player.skills[mercantile].damage + player.skills[mercantile].mod);
        result.playerPersonality = attribute(personality);
        result.playerLuck = attribute(luck);
        result.playerFatigueTerm = serviceFatigueTerm(player.dynamicStats, gmst);
        if (actorStats) result.enchanterFatigueTerm = serviceFatigueTerm(*actorStats, gmst);
        if (npc)
        {
            float disposition = baseDisposition.value_or(npc->mNpdt.mDisposition);
            if (!player.race.empty() && npc->mRace == ESM::RefId::stringRefId(player.race))
                disposition += gmst("fDispRaceMod");
            disposition += gmst("fDispPersonalityMult") * (result.playerPersonality - gmst("fDispPersonalityBase"));
            disposition -= gmst("fDispCrimeMod") * player.bounty;
            result.disposition = std::clamp(static_cast<int>(disposition), 0, 100);
            result.enchanterMercantile = npc->mNpdt.mSkills[mercantile];
            result.enchanterPersonality = npc->mNpdt.mAttributes[personality];
            result.enchanterLuck = npc->mNpdt.mAttributes[luck];
        }
        return result;
    }
}
