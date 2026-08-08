#include "EnchantingService.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include <components/enchanting/EnchantingMechanics.hpp>
#include <components/debug/debuglog.hpp>
#include <components/esm/attr.hpp>
#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadbook.hpp>
#include <components/esm3/loadclot.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/esm3/loadlock.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadmisc.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadprob.hpp>
#include <components/esm3/loadrepa.hpp>
#include <components/esm3/loadskil.hpp>
#include <components/esm3/loadweap.hpp>
#include <components/misc/rng.hpp>
#include <components/openmw-mp/Packets/Records/PacketEnchantingResult.hpp>
#include <components/openmw-mp/Records/DynamicRecordCodec.hpp>
#include <components/openmw-mp/Records/DynamicRecordFingerprint.hpp>
#include <components/openmw-mp/Records/DynamicRecordValidation.hpp>
#include <components/openmw-mp/Records/EsmDynamicRecordConversion.hpp>

#include <apps/openmw/mwworld/esmstore.hpp>

namespace
{
    std::vector<uint8_t> encodeResult(const mwmp::records::EnchantingResult& result)
    {
        mwmp::PacketEnchantingResult packet;
        packet.result = result;
        return packet.encode();
    }

    std::string asString(const std::vector<uint8_t>& bytes)
    {
        return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
    }

    std::string refIdText(const ESM::RefId& id)
    {
        return id.empty() ? std::string{} : id.serializeText();
    }

    ESM::RefId refIdFromText(std::string_view value)
    {
        // Same fallback as ServerContentRegistry::hasContentId: plain content
        // ids arrive as ordinary strings while generated ids use their
        // serialized prefixes.
        ESM::RefId result = ESM::RefId::deserializeText(value);
        return result.empty() ? ESM::RefId::stringRefId(value) : result;
    }

    /// Mirrors MWMechanics::Enchanting::getRecord(): type, cost, charge,
    /// flags, and every effect field must match, and only dynamic records
    /// qualify (static content records are never reused).
    bool nativeEquivalentEnchantment(
        const mwmp::records::Enchantment& left, const mwmp::records::Enchantment& right)
    {
        if (left.type != right.type || left.cost != right.cost || left.charge != right.charge
            || left.flags != right.flags || left.recordFlags != right.recordFlags)
            return false;
        if (left.effects.size() != right.effects.size())
            return false;
        for (std::size_t i = 0; i < left.effects.size(); ++i)
        {
            const mwmp::records::MagicEffect& a = left.effects[i];
            const mwmp::records::MagicEffect& b = right.effects[i];
            if (a.effectId != b.effectId || a.skillId != b.skillId || a.attributeId != b.attributeId
                || a.range != b.range || a.area != b.area || a.duration != b.duration
                || a.magnitudeMin != b.magnitudeMin || a.magnitudeMax != b.magnitudeMax)
                return false;
        }
        return true;
    }

    float modifiedValue(float base, float damage, float mod)
    {
        return std::max(0.f, base - damage + mod);
    }

    bool skillInClass(const std::array<std::array<int32_t, 2>, 5>& classSkills, bool major, int skillIndex)
    {
        for (const auto& pair : classSkills)
        {
            if (pair[major ? 1 : 0] == skillIndex)
                return true;
        }
        return false;
    }

    /// Vanilla player skill-use progression for Enchant_CreateMagicItem,
    /// matching the same formulas the vanilla playerskillhandlers.lua uses
    /// (identical to the alchemy progression, different use type).
    void applyEnchantSkillUse(mwmp::Skill& enchantSkill, float& levelProgress, const ESM::Skill* skillRecord,
        const ESM::Class& charClass, const MWWorld::ESMStore& store)
    {
        if (skillRecord == nullptr)
            throw std::runtime_error("enchant skill record is missing from authoritative content");

        if (enchantSkill.base >= 100.f)
            return;

        const auto gmst = [&](std::string_view id) -> float {
            const ESM::GameSetting* setting = store.get<ESM::GameSetting>().search(ESM::RefId::stringRefId(id));
            if (setting == nullptr)
                throw std::runtime_error(
                    std::string("enchant skill progression GMST is missing: ") + std::string(id));
            return setting->mValue.getFloat();
        };

        const int enchantIndex = ESM::Skill::refIdToIndex(ESM::Skill::Enchant);
        const bool isMajor = skillInClass(charClass.mData.mSkills, true, enchantIndex);
        const bool isMinor = skillInClass(charClass.mData.mSkills, false, enchantIndex);

        float factor = gmst("fMiscSkillBonus");
        if (isMajor)
            factor = gmst("fMajorSkillBonus");
        else if (isMinor)
            factor = gmst("fMinorSkillBonus");
        if (skillRecord->mData.mSpecialization == charClass.mData.mSpecialization)
            factor *= gmst("fSpecialSkillBonus");

        int levelUpProgress = 0;
        if (isMajor)
            levelUpProgress = static_cast<int>(gmst("iLevelUpMajorMult"));
        else if (isMinor)
            levelUpProgress = static_cast<int>(gmst("iLevelUpMinorMult"));

        const float gain = skillRecord->mData.mUseValue[ESM::Skill::Enchant_CreateMagicItem];
        const float requirement = (enchantSkill.base + 1.f) * factor;
        enchantSkill.progress += gain / requirement;
        if (enchantSkill.progress >= 1.f)
        {
            if (enchantSkill.base < 100.f)
                enchantSkill.base += 1.f;
            enchantSkill.progress = 0.f;
            levelProgress += static_cast<float>(levelUpProgress);
        }
    }

    bool enchantStatsChanged(const mwmp::BasePlayer& left, const mwmp::BasePlayer& right)
    {
        const int index = ESM::Skill::refIdToIndex(ESM::Skill::Enchant);
        const mwmp::Skill& a = left.skills[index];
        const mwmp::Skill& b = right.skills[index];
        return a.base != b.base || a.mod != b.mod || a.damage != b.damage || a.progress != b.progress
            || left.level != right.level || left.levelProgress != right.levelProgress;
    }

    /// The subset of the native derived-disposition formula that the
    /// multiplayer state model can express: base disposition from the NPC
    /// record plus the race, personality, and crime (bounty) modifiers. The
    /// faction-reaction, disease, weapon-drawn, and charm terms are not
    /// representable in the authoritative sync model (documented deviation;
    /// prices stay deterministic and content-derived).
    int derivedDisposition(const ESM::NPC& npc, const mwmp::BasePlayer& player, const MWWorld::ESMStore& store)
    {
        const auto gmst = [&](std::string_view id) -> float {
            const ESM::GameSetting* setting = store.get<ESM::GameSetting>().search(ESM::RefId::stringRefId(id));
            if (setting == nullptr)
                throw std::runtime_error(
                    std::string("enchanting disposition GMST is missing: ") + std::string(id));
            return setting->mValue.getFloat();
        };

        float x = static_cast<float>(npc.mNpdt.mDisposition);

        if (!player.race.empty() && npc.mRace == ESM::RefId::stringRefId(player.race))
            x += gmst("fDispRaceMod");

        const int personalityIndex = ESM::Attribute::refIdToIndex(ESM::Attribute::Personality);
        const mwmp::Attribute& personality = player.attributes[personalityIndex];
        x += gmst("fDispPersonalityMult")
            * (modifiedValue(static_cast<float>(personality.base), personality.damage, personality.mod)
                - gmst("fDispPersonalityBase"));

        x -= gmst("fDispCrimeMod") * static_cast<float>(player.bounty);

        return std::clamp(static_cast<int>(x), 0, 100);
    }

    /// Native CreatureStats::getFatigueTerm over the synced dynamic stats.
    float fatigueTerm(const mwmp::DynamicStats& stats, const MWWorld::ESMStore& store)
    {
        const float max = stats.fatigue.base + stats.fatigue.mod;
        const float current = stats.fatigue.current;

        const float normalised = std::floor(max) == 0 ? 1 : std::max(0.0f, current / max);

        const ESM::GameSetting* fFatigueBase
            = store.get<ESM::GameSetting>().search(ESM::RefId::stringRefId("fFatigueBase"));
        const ESM::GameSetting* fFatigueMult
            = store.get<ESM::GameSetting>().search(ESM::RefId::stringRefId("fFatigueMult"));
        if (fFatigueBase == nullptr || fFatigueMult == nullptr)
            throw std::runtime_error("fatigue GMST is missing from authoritative content");

        return fFatigueBase->mValue.getFloat() - fFatigueMult->mValue.getFloat() * (1 - normalised);
    }
}

namespace mwmp
{
    records::EnchantingResult EnchantingService::makeError(
        std::string requestId, records::EnchantingError error, uint64_t inventoryRevision)
    {
        records::EnchantingResult result;
        result.requestId = std::move(requestId);
        result.accepted = false;
        result.error = error;
        result.inventoryRevision = inventoryRevision;
        return result;
    }

    EnchantingService::Outcome EnchantingService::execute(
        const records::EnchantingRequest& request, std::string_view requestHash, const Context& context)
    {
        Outcome outcome;
        outcome.result.requestId = request.requestId;
        outcome.result.inventoryRevision = context.inventoryRevision;

        if (!context.player || !context.inventory || !context.store)
            throw std::invalid_argument(
                "EnchantingService requires authoritative player state, inventory, and content");

        const auto existingRequest
            = mDatabase.loadCraftRequest(context.accountId, context.characterId, request.requestId);
        if (const auto& existing = existingRequest)
        {
            if (existing->requestHash != requestHash)
            {
                outcome.result = makeError(request.requestId, records::EnchantingError::DuplicateRequestConflict,
                    context.inventoryRevision);
                outcome.encodedResult = encodeResult(outcome.result);
                return outcome;
            }
            if (existing->status == "accepted" || existing->status == "rejected")
            {
                outcome.encodedResult.assign(existing->resultPayload.begin(), existing->resultPayload.end());
                PacketEnchantingResult packet;
                if (!packet.decode(outcome.encodedResult))
                    throw std::runtime_error("Persisted enchanting result is corrupt");
                outcome.result = std::move(packet.result);
                outcome.replayed = true;
                return outcome;
            }
            outcome.result = makeError(
                request.requestId, records::EnchantingError::RequestPending, context.inventoryRevision);
            outcome.encodedResult = encodeResult(outcome.result);
            return outcome;
        }

        // -------------------------------------------------------------------
        // Request-level validation. Every terminal rejection is journaled so a
        // retry replays the exact same rejection without mutation.
        // -------------------------------------------------------------------
        records::EnchantingError earlyError = records::EnchantingError::None;
        if (request.protocolVersion != records::CurrentEnchantingProtocolVersion)
            earlyError = records::EnchantingError::UnsupportedProtocol;
        else if (request.requestId.empty() || request.requestId.size() > 128 || requestHash.empty())
            earlyError = records::EnchantingError::InvalidRequest;
        else if (context.admissionError != records::CreateError::None)
            earlyError = context.admissionError == records::CreateError::RateLimited
                ? records::EnchantingError::RateLimited
                : records::EnchantingError::ServerError;
        else if (request.inventoryRevision != context.inventoryRevision)
            earlyError = records::EnchantingError::StaleInventoryRevision;
        else if (request.targetInstanceId == 0 || request.soulGemInstanceId == 0)
            earlyError = records::EnchantingError::InvalidRequest;
        else if (request.targetInstanceId == request.soulGemInstanceId)
            earlyError = records::EnchantingError::DuplicateSourceInstance;
        else if (request.effects.empty() || request.effects.size() > records::MaxEnchantingEffects)
            earlyError = records::EnchantingError::InvalidRequest;
        else if (request.itemName.empty() || request.itemName.size() > records::MaxEnchantingItemNameLength)
            earlyError = records::EnchantingError::InvalidRequest;
        else if (request.selfEnchanting ? request.enchanterNetId != 0 : request.enchanterNetId == 0)
            earlyError = records::EnchantingError::InvalidRequest;

        auto reject = [&](records::EnchantingError error) {
            outcome.result = makeError(request.requestId, error, context.inventoryRevision);
            outcome.encodedResult = encodeResult(outcome.result);
            CraftRequestRecord journal;
            journal.accountId = context.accountId;
            journal.characterId = context.characterId;
            journal.requestId = request.requestId;
            journal.requestHash = std::string(requestHash);
            mDatabase.insertRejectedCraftRequest(journal, asString(outcome.encodedResult));
        };

        if (earlyError != records::EnchantingError::None)
        {
            reject(earlyError);
            return outcome;
        }

        // -------------------------------------------------------------------
        // Resolve exact inventory instances. Only instance identity proves
        // ownership; bare refIds are never accepted.
        // -------------------------------------------------------------------
        const Item* targetStack = nullptr;
        for (const Item& candidate : *context.inventory)
        {
            if (candidate.instanceId == request.targetInstanceId)
            {
                targetStack = &candidate;
                break;
            }
        }
        if (targetStack == nullptr)
        {
            reject(records::EnchantingError::TargetItemNotFound);
            return outcome;
        }
        if (targetStack->count <= 0)
        {
            reject(records::EnchantingError::TargetItemNotOwned);
            return outcome;
        }
        if (targetStack->refId.empty())
        {
            reject(records::EnchantingError::InvalidTargetItem);
            return outcome;
        }

        const Item* gemStack = nullptr;
        for (const Item& candidate : *context.inventory)
        {
            if (candidate.instanceId == request.soulGemInstanceId)
            {
                gemStack = &candidate;
                break;
            }
        }
        if (gemStack == nullptr)
        {
            reject(records::EnchantingError::SoulGemNotFound);
            return outcome;
        }
        if (gemStack->count <= 0)
        {
            reject(records::EnchantingError::SoulGemNotOwned);
            return outcome;
        }
        if (gemStack->refId.empty())
        {
            reject(records::EnchantingError::InvalidSoulGem);
            return outcome;
        }

        const MWWorld::ESMStore& store = *context.store;

        // -------------------------------------------------------------------
        // Resolve the target base record from authoritative content.
        // -------------------------------------------------------------------
        struct ResolvedTarget
        {
            int itemType = 0;
            int weaponType = -1;
            int enchantCapacity = 0;
            bool bookIsScroll = false;
            const ESM::Weapon* weapon = nullptr;
            const ESM::Armor* armor = nullptr;
            const ESM::Clothing* clothing = nullptr;
            const ESM::Book* book = nullptr;
        };
        ResolvedTarget target;
        bool targetResolved = false;
        {
            const ESM::RefId targetRefId = refIdFromText(targetStack->refId);
            if (const ESM::Weapon* weapon = store.get<ESM::Weapon>().search(targetRefId))
            {
                target.itemType = ESM::Weapon::sRecordId;
                target.weaponType = weapon->mData.mType;
                target.enchantCapacity = weapon->mData.mEnchant;
                target.weapon = weapon;
                targetResolved = true;
            }
            else if (const ESM::Armor* armor = store.get<ESM::Armor>().search(targetRefId))
            {
                target.itemType = ESM::Armor::sRecordId;
                target.enchantCapacity = armor->mData.mEnchant;
                target.armor = armor;
                targetResolved = true;
            }
            else if (const ESM::Clothing* clothing = store.get<ESM::Clothing>().search(targetRefId))
            {
                target.itemType = ESM::Clothing::sRecordId;
                target.enchantCapacity = clothing->mData.mEnchant;
                target.clothing = clothing;
                targetResolved = true;
            }
            else if (const ESM::Book* book = store.get<ESM::Book>().search(targetRefId))
            {
                target.itemType = ESM::Book::sRecordId;
                target.enchantCapacity = book->mData.mEnchant;
                target.bookIsScroll = book->mData.mIsScroll != 0;
                target.book = book;
                targetResolved = true;
            }
            else if (store.get<ESM::Miscellaneous>().search(targetRefId) != nullptr
                || store.get<ESM::Potion>().search(targetRefId) != nullptr
                || store.get<ESM::Ingredient>().search(targetRefId) != nullptr
                || store.get<ESM::Apparatus>().search(targetRefId) != nullptr
                || store.get<ESM::Lockpick>().search(targetRefId) != nullptr
                || store.get<ESM::Probe>().search(targetRefId) != nullptr
                || store.get<ESM::Repair>().search(targetRefId) != nullptr
                || store.get<ESM::Light>().search(targetRefId) != nullptr)
            {
                // A known content record that is not enchantable means the
                // wrong item type was submitted.
                reject(records::EnchantingError::InvalidTargetItem);
                return outcome;
            }
            else
            {
                // An entirely unknown id means the server content cannot be
                // reconciled with the client.
                reject(context.isContentIdAllowed && context.isContentIdAllowed(targetStack->refId)
                        ? records::EnchantingError::InvalidTargetItem
                        : records::EnchantingError::ContentMismatch);
                return outcome;
            }
        }
        if (!Crafting::EnchantingMechanics::isEnchantable(
                Crafting::EnchantingMechanicsInput{ .itemType = target.itemType, .bookIsScroll = target.bookIsScroll }))
        {
            reject(records::EnchantingError::InvalidTargetItem);
            return outcome;
        }

        // -------------------------------------------------------------------
        // Resolve the soul gem and its contained soul from authoritative
        // inventory state and content.
        // -------------------------------------------------------------------
        const ESM::Miscellaneous* gemRecord = store.get<ESM::Miscellaneous>().search(refIdFromText(gemStack->refId));
        if (gemRecord == nullptr)
        {
            reject(records::EnchantingError::InvalidSoulGem);
            return outcome;
        }
        if (gemStack->soul.empty())
        {
            reject(records::EnchantingError::EmptySoul);
            return outcome;
        }
        const ESM::RefId soulRefId = refIdFromText(gemStack->soul);
        const ESM::Creature* soulCreature = store.get<ESM::Creature>().search(soulRefId);
        if (soulCreature == nullptr)
        {
            reject(records::EnchantingError::InvalidSoul);
            return outcome;
        }
        const int gemCharge = soulCreature->mData.mSoul;

        // -------------------------------------------------------------------
        // Build the shared mechanics input from authoritative state.
        // -------------------------------------------------------------------
        Crafting::EnchantingMechanicsInput mechanics;
        mechanics.itemType = target.itemType;
        mechanics.weaponType = target.weaponType;
        mechanics.enchantCapacity = target.enchantCapacity;
        mechanics.bookIsScroll = target.bookIsScroll;
        mechanics.gemCharge = gemCharge;
        mechanics.castStyle = request.castStyle;
        mechanics.selfEnchanting = request.selfEnchanting;
        mechanics.projectilesEnchantMultiplier = context.projectilesEnchantMultiplier;
        mechanics.availableCount = 0;
        for (const Item& item : *context.inventory)
        {
            if (item.refId == targetStack->refId && item.count > 0)
                mechanics.availableCount += item.count;
        }

        const int enchantIndex = ESM::Skill::refIdToIndex(ESM::Skill::Enchant);
        const int intelligenceIndex = ESM::Attribute::refIdToIndex(ESM::Attribute::Intelligence);
        const int luckIndex = ESM::Attribute::refIdToIndex(ESM::Attribute::Luck);
        const Skill& enchantSkill = context.player->skills[enchantIndex];
        const Attribute& intelligence = context.player->attributes[intelligenceIndex];
        const Attribute& luck = context.player->attributes[luckIndex];

        // The enchanter whose statistics determine the outcome: the player
        // for self-enchanting, the paid NPC otherwise.
        float enchanterFatigueTerm = 1.f;

        if (!request.selfEnchanting)
        {
            // Paid service: resolve the enchanter actor and derive its
            // statistics from the authoritative NPC content record.
            if (!context.resolveEnchanter)
            {
                reject(records::EnchantingError::InvalidEnchanter);
                return outcome;
            }
            const auto enchanter = context.resolveEnchanter(request.enchanterNetId);
            if (!enchanter || enchanter->refId.empty())
            {
                reject(records::EnchantingError::InvalidEnchanter);
                return outcome;
            }
            if (!enchanter->cellLoaded)
            {
                reject(records::EnchantingError::EnchanterUnavailable);
                return outcome;
            }

            const ESM::NPC* npcRecord = store.get<ESM::NPC>().search(refIdFromText(enchanter->refId));
            const ESM::Creature* creatureRecord
                = npcRecord == nullptr ? store.get<ESM::Creature>().search(refIdFromText(enchanter->refId)) : nullptr;
            if (npcRecord == nullptr && creatureRecord == nullptr)
            {
                reject(records::EnchantingError::InvalidEnchanter);
                return outcome;
            }

            // Service availability: the Enchanting dialogue service must be
            // offered (autocalc NPCs inherit it from their class record).
            int services = 0;
            if (npcRecord != nullptr)
            {
                services = (npcRecord->mFlags & ESM::NPC::Autocalc) && !npcRecord->mClass.empty()
                    ? store.get<ESM::Class>().search(npcRecord->mClass) != nullptr
                        ? store.get<ESM::Class>().find(npcRecord->mClass)->mData.mServices
                        : 0
                    : npcRecord->mAiData.mServices;
            }
            else
            {
                services = creatureRecord->mAiData.mServices;
            }
            if ((services & ESM::NPC::Enchanting) == 0)
            {
                reject(records::EnchantingError::EnchanterUnavailable);
                return outcome;
            }

            // Statistics come from the NPC content record (base values; the
            // sync model does not carry per-actor skill/attribute state).
            if (npcRecord != nullptr)
            {
                const int mercantileIndex = ESM::Skill::refIdToIndex(ESM::Skill::Mercantile);
                const int personalityIndex = ESM::Attribute::refIdToIndex(ESM::Attribute::Personality);

                Skill npcEnchant;
                npcEnchant.base = static_cast<float>(npcRecord->mNpdt.mSkills[enchantIndex]);
                Attribute npcIntelligence;
                npcIntelligence.base = npcRecord->mNpdt.mAttributes[intelligenceIndex];
                Attribute npcLuck;
                npcLuck.base = npcRecord->mNpdt.mAttributes[luckIndex];
                Skill npcMercantile;
                npcMercantile.base = static_cast<float>(npcRecord->mNpdt.mSkills[mercantileIndex]);
                Attribute npcPersonality;
                npcPersonality.base = npcRecord->mNpdt.mAttributes[personalityIndex];

                mechanics.enchantSkill = npcEnchant.base;
                mechanics.intelligence = static_cast<float>(npcIntelligence.base);
                mechanics.luck = static_cast<float>(npcLuck.base);
                if (enchanter->dynamicStats)
                    enchanterFatigueTerm = fatigueTerm(*enchanter->dynamicStats, store);
                mechanics.fatigueTerm = enchanterFatigueTerm;

                Crafting::EnchantingBarterInput barter;
                const int mercantilePlayerIndex = ESM::Skill::refIdToIndex(ESM::Skill::Mercantile);
                const int personalityPlayerIndex = ESM::Attribute::refIdToIndex(ESM::Attribute::Personality);
                const int luckPlayerIndex = ESM::Attribute::refIdToIndex(ESM::Attribute::Luck);
                barter.playerMercantile
                    = modifiedValue(context.player->skills[mercantilePlayerIndex].base,
                        context.player->skills[mercantilePlayerIndex].damage,
                        context.player->skills[mercantilePlayerIndex].mod);
                barter.playerLuck = modifiedValue(static_cast<float>(context.player->attributes[luckPlayerIndex].base),
                    context.player->attributes[luckPlayerIndex].damage, context.player->attributes[luckPlayerIndex].mod);
                barter.playerPersonality
                    = modifiedValue(static_cast<float>(context.player->attributes[personalityPlayerIndex].base),
                        context.player->attributes[personalityPlayerIndex].damage,
                        context.player->attributes[personalityPlayerIndex].mod);
                barter.playerFatigueTerm = fatigueTerm(context.player->dynamicStats, store);
                barter.enchanterMercantile = npcMercantile.base;
                barter.enchanterLuck = static_cast<float>(npcLuck.base);
                barter.enchanterPersonality = static_cast<float>(npcPersonality.base);
                barter.enchanterFatigueTerm = enchanterFatigueTerm;
                barter.disposition = derivedDisposition(*npcRecord, *context.player, store);
                mechanics.barter = std::move(barter);
            }
            else
            {
                // Creature enchanters keep the native base-price behavior
                // (no barter adjustment) and always succeed; their skill
                // values are not used by the formulas.
                mechanics.enchantSkill = 100.f;
                mechanics.intelligence = 100.f;
                mechanics.luck = 50.f;
                mechanics.fatigueTerm = enchanterFatigueTerm;
                Crafting::EnchantingBarterInput barter;
                barter.playerMercantile = modifiedValue(
                    context.player->skills[ESM::Skill::refIdToIndex(ESM::Skill::Mercantile)].base,
                    context.player->skills[ESM::Skill::refIdToIndex(ESM::Skill::Mercantile)].damage,
                    context.player->skills[ESM::Skill::refIdToIndex(ESM::Skill::Mercantile)].mod);
                barter.playerLuck = modifiedValue(
                    static_cast<float>(context.player->attributes[ESM::Attribute::refIdToIndex(ESM::Attribute::Luck)].base),
                    context.player->attributes[ESM::Attribute::refIdToIndex(ESM::Attribute::Luck)].damage,
                    context.player->attributes[ESM::Attribute::refIdToIndex(ESM::Attribute::Luck)].mod);
                barter.playerFatigueTerm = fatigueTerm(context.player->dynamicStats, store);
                barter.creatureMerchant = true;
                mechanics.barter = std::move(barter);
            }
        }
        else
        {
            mechanics.enchantSkill = modifiedValue(enchantSkill.base, enchantSkill.damage, enchantSkill.mod);
            mechanics.intelligence
                = modifiedValue(static_cast<float>(intelligence.base), intelligence.damage, intelligence.mod);
            mechanics.luck = modifiedValue(static_cast<float>(luck.base), luck.damage, luck.mod);
            mechanics.fatigueTerm = fatigueTerm(context.player->dynamicStats, store);
        }

        mechanics.magicEffect = [&store](const ESM::RefId& id) -> std::optional<Crafting::MagicEffectData> {
            const ESM::MagicEffect* effect = store.get<ESM::MagicEffect>().search(id);
            if (effect == nullptr)
                return std::nullopt;
            return Crafting::MagicEffectData{
                effect->mData.mBaseCost, static_cast<std::uint32_t>(effect->mData.mFlags) };
        };
        mechanics.gmst = [&store](std::string_view id) -> std::optional<float> {
            const ESM::GameSetting* setting = store.get<ESM::GameSetting>().search(ESM::RefId::stringRefId(id));
            if (setting == nullptr)
                return std::nullopt;
            return setting->mValue.getFloat();
        };

        // -------------------------------------------------------------------
        // Validate the selected effects against authoritative content.
        // -------------------------------------------------------------------
        std::vector<ESM::ENAMstruct> effects;
        effects.reserve(request.effects.size());
        for (const records::EnchantingEffectChoice& choice : request.effects)
        {
            const ESM::RefId effectId = refIdFromText(choice.effectId);
            const ESM::MagicEffect* effectRecord = store.get<ESM::MagicEffect>().search(effectId);
            if (effectRecord == nullptr)
            {
                reject(records::EnchantingError::InvalidEffect);
                return outcome;
            }
            if ((effectRecord->mData.mFlags & ESM::MagicEffect::AllowEnchanting) == 0)
            {
                reject(records::EnchantingError::EffectNotAllowed);
                return outcome;
            }
            // The native editor permits only ranges the effect supports, and
            // constant effects are always Self-only.
            const bool constant = request.castStyle == ESM::Enchantment::ConstantEffect;
            const bool allowSelf = (effectRecord->mData.mFlags & ESM::MagicEffect::CastSelf) != 0 || constant;
            const bool allowTouch = (effectRecord->mData.mFlags & ESM::MagicEffect::CastTouch) != 0 && !constant;
            const bool allowTarget = (effectRecord->mData.mFlags & ESM::MagicEffect::CastTarget) != 0 && !constant;
            const bool rangeAllowed = (choice.range == ESM::RT_Self && allowSelf)
                || (choice.range == ESM::RT_Touch && allowTouch)
                || (choice.range == ESM::RT_Target && allowTarget);
            if (!rangeAllowed)
            {
                reject(records::EnchantingError::InvalidEffect);
                return outcome;
            }
            if (choice.magnitudeMin > choice.magnitudeMax)
            {
                reject(records::EnchantingError::InvalidMagnitude);
                return outcome;
            }

            ESM::ENAMstruct effect;
            effect.mEffectID = effectId;
            effect.mRange = choice.range;
            effect.mArea = choice.area;
            effect.mDuration = choice.duration;
            effect.mMagnMin = choice.magnitudeMin;
            effect.mMagnMax = choice.magnitudeMax;
            if (effectRecord->mData.mFlags & ESM::MagicEffect::TargetSkill)
            {
                // The client picks a target skill; the authoritative content
                // must know it.
                effect.mSkill = refIdFromText(choice.skillId);
                if (effect.mSkill.empty() || store.get<ESM::Skill>().search(effect.mSkill) == nullptr)
                {
                    reject(records::EnchantingError::InvalidEffect);
                    return outcome;
                }
            }
            if (effectRecord->mData.mFlags & ESM::MagicEffect::TargetAttribute)
            {
                effect.mAttribute = refIdFromText(choice.attributeId);
                if (effect.mAttribute.empty()
                    || ESM::Attribute::refIdToIndex(effect.mAttribute) < 0)
                {
                    reject(records::EnchantingError::InvalidEffect);
                    return outcome;
                }
            }
            effects.push_back(std::move(effect));
        }
        mechanics.effects = std::move(effects);

        // -------------------------------------------------------------------
        // Cast style, capacity, and paid-service pricing validation.
        // -------------------------------------------------------------------
        try
        {
            const std::vector<int> validStyles = Crafting::EnchantingMechanics::validCastStyles(mechanics);
            if (std::find(validStyles.begin(), validStyles.end(), request.castStyle) == validStyles.end())
            {
                reject(records::EnchantingError::InvalidCastStyle);
                return outcome;
            }

            if (static_cast<int>(Crafting::EnchantingMechanics::enchantPoints(mechanics, false))
                > Crafting::EnchantingMechanics::maxEnchantValue(mechanics))
            {
                reject(records::EnchantingError::CapacityExceeded);
                return outcome;
            }
        }
        catch (const std::exception&)
        {
            reject(records::EnchantingError::MechanicsValidationFailed);
            return outcome;
        }

        int count = 1;
        int price = 0;
        try
        {
            count = Crafting::EnchantingMechanics::enchantItemsCount(mechanics, mechanics.availableCount);
            // Native removes the enchanted count from the selected stack only;
            // clamp by the selected stack so a multi-stack ammo quirk cannot
            // mint items (documented deviation from the native edge case).
            count = std::min(count, targetStack->count);
            if (!request.selfEnchanting)
            {
                price = Crafting::EnchantingMechanics::enchantPrice(mechanics, count);
                int playerGold = 0;
                for (const Item& item : *context.inventory)
                {
                    if (item.refId == "gold_001" && item.count > 0)
                        playerGold += item.count;
                }
                if (playerGold < price)
                {
                    reject(records::EnchantingError::InsufficientGold);
                    return outcome;
                }
            }
        }
        catch (const std::exception&)
        {
            reject(records::EnchantingError::MechanicsValidationFailed);
            return outcome;
        }

        // -------------------------------------------------------------------
        // Authoritative roll. The server owns the RNG; the terminal result
        // durably captures the roll so retries never reroll.
        // -------------------------------------------------------------------
        Misc::Rng::Generator prng(context.rngSeed ? *context.rngSeed : Misc::Rng::generateDefaultSeed());
        const bool success = Crafting::EnchantingMechanics::rollSuccess(mechanics, prng);

        // -------------------------------------------------------------------
        // Native consumption semantics, executed against the proposed
        // inventory. The soul gem is consumed before the roll and on every
        // outcome; the target item is consumed only on success.
        // -------------------------------------------------------------------
        std::vector<Item> nextInventory = *context.inventory;

        const auto consumeOneOf = [&](std::uint32_t instanceId) {
            for (auto it = nextInventory.begin(); it != nextInventory.end(); ++it)
            {
                if (it->instanceId != instanceId)
                    continue;
                --it->count;
                if (it->count <= 0)
                    nextInventory.erase(it);
                return;
            }
        };
        const auto consumeMany = [&](std::uint32_t instanceId, int amount) {
            for (auto it = nextInventory.begin(); it != nextInventory.end(); ++it)
            {
                if (it->instanceId != instanceId)
                    continue;
                it->count -= amount;
                if (it->count <= 0)
                    nextInventory.erase(it);
                return;
            }
        };
        const auto consumeGold = [&](int amount) {
            int remaining = amount;
            for (auto it = nextInventory.begin(); it != nextInventory.end() && remaining > 0;)
            {
                if (it->refId != "gold_001" || it->count <= 0)
                {
                    ++it;
                    continue;
                }
                const int take = std::min(remaining, it->count);
                it->count -= take;
                remaining -= take;
                if (it->count <= 0)
                    it = nextInventory.erase(it);
                else
                    ++it;
            }
        };
        const auto sameStack = [](const Item& left, const Item& right) {
            return left.refId == right.refId && left.charge == right.charge
                && std::abs(left.enchantmentCharge - right.enchantmentCharge) < 0.001f
                && left.soul == right.soul;
        };
        const auto grantItem = [&](const std::string& recordId, int amount) {
            Item granted;
            granted.refId = recordId;
            granted.count = amount;
            granted.charge = -1;
            granted.enchantmentCharge = -1.f;
            const auto it = std::find_if(nextInventory.begin(), nextInventory.end(),
                [&](const Item& existing) { return sameStack(existing, granted); });
            if (it != nextInventory.end())
                it->count += amount;
            else
                nextInventory.push_back(std::move(granted));
        };

        consumeOneOf(request.soulGemInstanceId);
        // Exception for Azura Star: a fresh one is added back immediately.
        if (gemStack->refId == "Misc_SoulGem_Azura")
            grantItem("Misc_SoulGem_Azura", 1);

        std::vector<DynamicRecordService::CommittedRecord> newRecords;
        std::vector<DynamicRecordCommitEntry> commitEntries;
        outcome.result.success = success;

        if (success)
        {
            // -----------------------------------------------------------------
            // Build the Enchantment + owning-item record pair as a single
            // bundled preparation: the enchantment is prepared first so its
            // canonical id is known before the owning item's definition and
            // fingerprint are finalized.
            // -----------------------------------------------------------------
            DynamicRecordService recordService(mDatabase);
            try
            {
                records::Enchantment enchantment;
                enchantment.recordFlags = 0;
                enchantment.type = request.castStyle;
                enchantment.cost = Crafting::EnchantingMechanics::baseCastCost(mechanics);
                enchantment.charge = Crafting::EnchantingMechanics::enchantmentCharge(mechanics, count);
                enchantment.flags = 0;
                for (const ESM::ENAMstruct& effect : mechanics.effects)
                {
                    records::MagicEffect converted;
                    converted.effectId = refIdText(effect.mEffectID);
                    converted.skillId = refIdText(effect.mSkill);
                    converted.attributeId = refIdText(effect.mAttribute);
                    converted.range = effect.mRange;
                    converted.area = effect.mArea;
                    converted.duration = effect.mDuration;
                    converted.magnitudeMin = effect.mMagnMin;
                    converted.magnitudeMax = effect.mMagnMax;
                    enchantment.effects.push_back(std::move(converted));
                }

                records::DynamicRecordDefinition enchantmentDefinition = records::canonicalize(
                    records::DynamicRecordDefinition{ records::CurrentSchemaVersion, enchantment });
                const std::string enchantmentFingerprint = records::fingerprint(enchantmentDefinition);
                records::RecordDraft enchantmentDraft;
                enchantmentDraft.temporaryKey = "enchantment";
                enchantmentDraft.definition = std::move(enchantmentDefinition);

                const DynamicRecordService::PreparedRecord preparedEnchantment = recordService.prepareSingleRecord(
                    enchantmentDraft, context, context.findEquivalent, context.allocateId);
                if (preparedEnchantment.entry)
                    commitEntries.push_back(*preparedEnchantment.entry);

                // The owning item references the canonical enchantment id.
                records::DynamicRecordDefinition itemDefinition = [&]() {
                    if (target.weapon)
                        return records::fromEsmRecord(*target.weapon);
                    if (target.armor)
                        return records::fromEsmRecord(*target.armor);
                    if (target.clothing)
                        return records::fromEsmRecord(*target.clothing);
                    return records::fromEsmRecord(*target.book);
                }();
                const int charge = gemCharge;
                std::visit(
                    [&](auto& record) {
                        using Record = std::decay_t<decltype(record)>;
                        if constexpr (std::is_same_v<Record, records::Weapon>
                            || std::is_same_v<Record, records::Armor>
                            || std::is_same_v<Record, records::Clothing>
                            || std::is_same_v<Record, records::Book>)
                        {
                            record.item.name = request.itemName;
                            record.enchantment = records::RecordReference{
                                records::ReferenceKind::ContentId, preparedEnchantment.created.recordId };
                            // Native casts the charge to the record field width
                            // for weapons and clothing (uint16).
                            if constexpr (std::is_same_v<Record, records::Weapon>
                                || std::is_same_v<Record, records::Clothing>)
                                record.enchantCapacity = static_cast<std::uint16_t>(charge);
                            else
                                record.enchantCapacity = charge;
                            if constexpr (std::is_same_v<Record, records::Weapon>)
                                record.flags |= ESM::Weapon::Magical;
                            if constexpr (std::is_same_v<Record, records::Book>)
                                record.isScroll = true;
                        }
                        else
                        {
                            throw std::runtime_error("unreachable item type in enchanting service");
                        }
                    },
                    itemDefinition.data);

                // The item carries only server-issued references and content
                // fields copied from the authoritative base record, so every
                // field must pass the same content gates as the alchemy path.
                const auto contentAllowed = [&](std::string_view id) {
                    return id.empty() || (context.isContentIdAllowed && context.isContentIdAllowed(id));
                };
                const auto modelAllowed = [&](std::string_view path) {
                    return path.empty() || (context.isModelAllowed && context.isModelAllowed(path));
                };
                const auto iconAllowed = [&](std::string_view path) {
                    return path.empty() || (context.isIconAllowed && context.isIconAllowed(path));
                };
                std::visit(
                    [&](const auto& record) {
                        using Record = std::decay_t<decltype(record)>;
                        if constexpr (std::is_same_v<Record, records::Weapon>
                            || std::is_same_v<Record, records::Armor>
                            || std::is_same_v<Record, records::Clothing>
                            || std::is_same_v<Record, records::Book>)
                        {
                            if (!modelAllowed(record.item.model) || !iconAllowed(record.item.icon)
                                || !contentAllowed(record.item.scriptId))
                                throw std::runtime_error("enchanted item references unknown content");
                            if constexpr (std::is_same_v<Record, records::Armor>
                                || std::is_same_v<Record, records::Clothing>)
                            {
                                for (const records::BodyPartReference& part : record.parts)
                                {
                                    if (!contentAllowed(part.maleId) || !contentAllowed(part.femaleId))
                                        throw std::runtime_error("enchanted item references unknown body part");
                                }
                            }
                        }
                    },
                    itemDefinition.data);

                records::RecordDraft itemDraft;
                itemDraft.temporaryKey = "item";
                itemDraft.definition = std::move(itemDefinition);

                const DynamicRecordService::PreparedRecord preparedItem
                    = recordService.prepareSingleRecord(itemDraft, context, context.findEquivalent, context.allocateId);
                if (preparedItem.entry)
                    commitEntries.push_back(*preparedItem.entry);

                outcome.result.enchantmentRecordId = preparedEnchantment.created.recordId;
                outcome.result.enchantmentReused = preparedEnchantment.created.reused;
                outcome.result.itemRecordId = preparedItem.created.recordId;
                outcome.result.itemReused = preparedItem.created.reused;

                if (commitEntries.size() > context.maximumNewRecords)
                {
                    reject(records::EnchantingError::QuotaExceeded);
                    return outcome;
                }

                // Publish the runtime definitions for broadcast; the entries
                // are already in dependency order (enchantment before item).
                for (const DynamicRecordCommitEntry& entry : commitEntries)
                {
                    DynamicRecordService::CommittedRecord runtime;
                    runtime.recordType = entry.record.recordType;
                    runtime.recordId = entry.record.recordId;
                    runtime.definition = entry.record.data;
                    newRecords.push_back(std::move(runtime));
                }

                // Consume the target (the enchanted count) and grant the
                // enchanted items.
                consumeMany(request.targetInstanceId, count);
                grantItem(outcome.result.itemRecordId, count);
                if (!request.selfEnchanting)
                    consumeGold(price);
            }
            catch (const std::exception& e)
            {
                Log(Debug::Error) << "[EnchantingService] mechanics failure requestId=" << request.requestId
                                  << " what=" << e.what();
                reject(records::EnchantingError::MechanicsValidationFailed);
                return outcome;
            }
        }

        // -------------------------------------------------------------------
        // Authoritative skill progression (vanilla player semantics): one
        // Enchant_CreateMagicItem use per successful self-enchant.
        // -------------------------------------------------------------------
        std::optional<BasePlayer> resultingStats;
        if (success && request.selfEnchanting)
        {
            const ESM::Skill* skillRecord = store.get<ESM::Skill>().search(ESM::Skill::Enchant);
            BasePlayer stats = *context.player;
            try
            {
                applyEnchantSkillUse(
                    stats.skills[enchantIndex], stats.levelProgress, skillRecord, context.player->charClass, store);
            }
            catch (const std::exception&)
            {
                reject(records::EnchantingError::MechanicsValidationFailed);
                return outcome;
            }
            if (enchantStatsChanged(stats, *context.player))
                resultingStats = std::move(stats);
        }

        // -------------------------------------------------------------------
        // Atomic commit: record pair + inventory + revision + stats + journal
        // in one transaction. All-or-nothing.
        // -------------------------------------------------------------------
        if (context.reconcileInventory)
            context.reconcileInventory(nextInventory);

        outcome.result.accepted = true;
        outcome.result.error = records::EnchantingError::None;
        outcome.result.inventoryRevision = context.inventoryRevision + 1;
        outcome.result.commitSequence = context.nextCommitSequence ? context.nextCommitSequence() : 0;
        outcome.encodedResult = encodeResult(outcome.result);

        DynamicRecordCommit commit;
        commit.accountId = context.accountId;
        commit.characterId = context.characterId;
        commit.requestId = request.requestId;
        commit.requestHash = std::string(requestHash);
        commit.resultPayload = asString(outcome.encodedResult);
        commit.expectedInventoryRevision = context.inventoryRevision;
        commit.resultingInventoryRevision = outcome.result.inventoryRevision;
        commit.records = std::move(commitEntries);
        commit.inventory = nextInventory;
        commit.characterStats = resultingStats;

        const DynamicRecordCommitStatus status = mDatabase.commitDynamicRecordRequest(commit);
        if (status == DynamicRecordCommitStatus::DuplicateRequest
            || status == DynamicRecordCommitStatus::DuplicateRequestConflict)
        {
            // A concurrent/re-entrant caller won the idempotency key. Re-enter
            // once through the replay path; no runtime state has been published.
            return execute(request, requestHash, context);
        }
        if (status == DynamicRecordCommitStatus::StaleInventoryRevision)
        {
            outcome.result = makeError(request.requestId, records::EnchantingError::StaleInventoryRevision,
                mDatabase.loadInventoryRevision(context.characterId));
            outcome.encodedResult = encodeResult(outcome.result);
            CraftRequestRecord journal;
            journal.accountId = context.accountId;
            journal.characterId = context.characterId;
            journal.requestId = request.requestId;
            journal.requestHash = std::string(requestHash);
            mDatabase.insertRejectedCraftRequest(journal, asString(outcome.encodedResult));
            return outcome;
        }

        outcome.committed = true;
        outcome.newRecords = std::move(newRecords);
        outcome.resultingInventory = std::move(nextInventory);
        outcome.resultingInventoryRevision = outcome.result.inventoryRevision;
        outcome.resultingStats = std::move(resultingStats);
        return outcome;
    }
}
