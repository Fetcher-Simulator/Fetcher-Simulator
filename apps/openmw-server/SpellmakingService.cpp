#include "SpellmakingService.hpp"
#include <components/openmw-mp/ServicePricing.hpp>
#include <algorithm>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <cmath>
#include <components/esm/attr.hpp>
#include <components/esm3/loadclas.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadskil.hpp>
#include <components/openmw-mp/Packets/Records/PacketSpellmakingResult.hpp>
#include <components/openmw-mp/Records/EsmDynamicRecordConversion.hpp>
#include <components/openmw-mp/SpellbookSync.hpp>
#include <components/openmw-mp/SpellmakingCost.hpp>
#include <limits>
#include <set>
#include <tuple>

namespace mwmp
{
    SpellmakingService::Outcome SpellmakingService::execute(
        const records::SpellmakingRequest& request, std::string_view hash, const Context& context)
    {
        using Error = records::SpellmakingError;
        Outcome out;
        out.result.requestId = request.requestId;
        out.result.inventoryRevision = context.inventoryRevision;
        out.result.spellbookRevision = context.spellbookRevision;
        auto encode = [&]() {
            PacketSpellmakingResult packet;
            packet.result = out.result;
            out.encodedResult = packet.encode();
        };
        auto reject = [&](Error error, bool journal = true) {
            out.result.error = error;
            encode();
            if (journal && !request.requestId.empty() && request.requestId.size() <= 128 && !hash.empty())
            {
                CraftRequestRecord row;
                row.accountId = context.accountId;
                row.characterId = context.characterId;
                row.requestId = request.requestId;
                row.requestHash = hash;
                mDatabase.insertRejectedCraftRequest(
                    row, { reinterpret_cast<const char*>(out.encodedResult.data()), out.encodedResult.size() });
            }
            return out;
        };
        if (auto previous = mDatabase.loadCraftRequest(context.accountId, context.characterId, request.requestId))
        {
            if (previous->requestHash != hash)
                return reject(Error::DuplicateRequestConflict, false);
            if (previous->status != "accepted" && previous->status != "rejected")
                return reject(Error::RequestPending, false);
            out.encodedResult.assign(previous->resultPayload.begin(), previous->resultPayload.end());
            PacketSpellmakingResult packet;
            if (!packet.decode(out.encodedResult))
                throw std::runtime_error("Corrupt spellmaking journal result");
            out.result = packet.result;
            out.replayed = true;
            return out;
        }
        if (!context.player || !context.store || !context.lookupSpell)
            return reject(Error::ServerError);
        if (request.protocolVersion != records::SpellmakingProtocolVersion)
            return reject(Error::UnsupportedProtocol);
        if (request.requestId.empty() || request.requestId.size() > 128 || hash.empty() || request.name.empty()
            || request.name.size() > 64 || request.name.find_first_of("\r\n\t") != std::string::npos
            || request.name.find('\0') != std::string::npos || request.maximumPrice < 0 || request.effects.empty()
            || request.effects.size() > records::MaxSpellmakingEffects)
            return reject(Error::InvalidRequest);
        if (context.admissionError != records::CreateError::None)
            return reject(Error::RateLimited);
        if (request.inventoryRevision != context.inventoryRevision)
            return reject(Error::StaleInventoryRevision);
        if (request.spellbookRevision != context.spellbookRevision)
            return reject(Error::StaleSpellbookRevision);
        const auto actor = context.resolveActor ? context.resolveActor(request.actorNetId) : std::nullopt;
        if (!actor || !actor->available || request.actorNetId == 0)
            return reject(Error::InvalidService);
        const auto& store = *context.store;
        const auto actorId = ESM::RefId::stringRefId(actor->refId);
        const auto* npc = store.get<ESM::NPC>().search(actorId);
        const auto* creature = store.get<ESM::Creature>().search(actorId);
        int services = 0;
        if (npc)
        {
            services = npc->mAiData.mServices;
            if (npc->mFlags & ESM::NPC::Autocalc)
            {
                const auto* cls = store.get<ESM::Class>().search(npc->mClass);
                services = cls ? cls->mData.mServices : 0;
            }
        }
        else if (creature)
            services = creature->mAiData.mServices;
        if (!(services & ESM::NPC::Spellmaking))
            return reject(Error::InvalidService);
        std::set<ESM::RefId> known;
        for (const auto& id : context.player->spellbookChanges.spellIds)
        {
            if (auto spell = context.lookupSpell(id); spell && spell->mData.mType == ESM::Spell::ST_Spell)
                for (const auto& effect : spell->mEffects.mList)
                    known.insert(effect.mData.mEffectID);
        }
        std::set<std::tuple<ESM::RefId, ESM::RefId, ESM::RefId>> selected;
        std::vector<ESM::ENAMstruct> effects;
        for (const auto& choice : request.effects)
        {
            ESM::ENAMstruct effect{};
            effect.mEffectID = ESM::RefId::deserializeText(choice.effectId);
            const auto* magic = store.get<ESM::MagicEffect>().search(effect.mEffectID);
            if (!magic || !(magic->mData.mFlags & ESM::MagicEffect::AllowSpellmaking))
                return reject(Error::InvalidEffect);
            if (!known.contains(effect.mEffectID))
                return reject(Error::UnknownEffect);
            const auto flags = magic->mData.mFlags;
            if (choice.range < 0 || choice.range > 2 || !(flags & (ESM::MagicEffect::CastSelf << choice.range))
                || choice.magnitudeMin < 0 || choice.magnitudeMax < choice.magnitudeMin || choice.magnitudeMax > 1000
                || choice.duration < 0 || choice.duration > 3600 || choice.area < 0 || choice.area > 1000
                || (choice.range == ESM::RT_Self && choice.area != 0))
                return reject(Error::InvalidEffect);
            if (flags & ESM::MagicEffect::TargetSkill)
            {
                effect.mSkill = ESM::RefId::deserializeText(choice.skillId);
                if (effect.mSkill.empty() || !store.get<ESM::Skill>().search(effect.mSkill))
                    return reject(Error::InvalidEffect);
            }
            else if (!choice.skillId.empty())
                return reject(Error::InvalidEffect);
            if (flags & ESM::MagicEffect::TargetAttribute)
            {
                effect.mAttribute = ESM::RefId::deserializeText(choice.attributeId);
                if (effect.mAttribute.empty() || ESM::Attribute::refIdToIndex(effect.mAttribute) < 0)
                    return reject(Error::InvalidEffect);
            }
            else if (!choice.attributeId.empty())
                return reject(Error::InvalidEffect);
            if (!selected.emplace(effect.mEffectID, effect.mSkill, effect.mAttribute).second)
                return reject(Error::InvalidEffect);
            effect.mRange = choice.range;
            effect.mArea = choice.area;
            effect.mDuration = flags & ESM::MagicEffect::NoDuration ? 0 : choice.duration;
            effect.mMagnMin = flags & ESM::MagicEffect::NoMagnitude ? 0 : choice.magnitudeMin;
            effect.mMagnMax = flags & ESM::MagicEffect::NoMagnitude ? 0 : choice.magnitudeMax;
            effects.push_back(effect);
        }
        const auto gmst
            = [&](std::string_view id) { return store.get<ESM::GameSetting>().find(id)->mValue.getFloat(); };
        ESM::Spell spell;
        spell.blank();
        spell.mName = request.name;
        spell.mData.mType = ESM::Spell::ST_Spell;
        spell.mData.mFlags = 0;
        const float rawCost = spellmakingCost(
            effects,
            [&](const ESM::RefId& id) -> const ESM::MagicEffect& { return *store.get<ESM::MagicEffect>().find(id); },
            gmst("fEffectCostMult"));
        spell.mData.mCost = static_cast<int>(rawCost);
        spell.mEffects.populate(effects);
        const auto barter = serviceBarterInput(npc, *context.player, &actor->dynamicStats, gmst, actor->baseDisposition);
        out.result.price = spellmakingPrice(rawCost, gmst("fSpellMakingValueMult"), barter);
        if (out.result.price > request.maximumPrice)
            return reject(Error::PriceChanged);
        std::int64_t gold = 0;
        for (const auto& item : context.player->inventoryChanges.items)
            if (item.refId == "gold_001" && item.count > 0)
                gold += item.count;
        if (gold < out.result.price)
            return reject(Error::InsufficientGold);
        records::RecordDraft draft;
        draft.temporaryKey = "spell";
        draft.definition = records::fromEsmRecord(spell);
        DynamicRecordService service(mDatabase);
        const auto prepared = service.prepareSingleRecord(draft, context, context.findEquivalent, context.allocateId);
        if (prepared.entry && context.maximumNewRecords == 0)
            return reject(Error::QuotaExceeded);
        out.spellbook = canonicalizeSpellIds(context.player->spellbookChanges.spellIds);
        if (std::find(out.spellbook.begin(), out.spellbook.end(), prepared.created.recordId) != out.spellbook.end())
            return reject(Error::AlreadyKnown);
        if (out.spellbook.size() >= MAX_SPELLBOOK_SIZE)
            return reject(Error::TooManySpells);
        out.spellbook.push_back(prepared.created.recordId);
        out.spellbook = canonicalizeSpellIds(std::move(out.spellbook));
        out.inventory = context.player->inventoryChanges.items;
        int remaining = out.result.price;
        for (auto& item : out.inventory)
            if (item.refId == "gold_001" && item.count > 0)
            {
                const int amount = std::min(item.count, remaining);
                item.count -= amount;
                remaining -= amount;
            }
        std::erase_if(out.inventory, [](const Item& item) { return item.count <= 0; });
        auto merchantGold = actor->gold;
        const std::int64_t newGold = static_cast<std::int64_t>(merchantGold.resultingGold) + out.result.price;
        if (newGold > std::numeric_limits<int>::max())
            return reject(Error::ServerError);
        merchantGold.resultingGold = static_cast<int>(newGold);
        out.result.accepted = true;
        out.result.recordId = prepared.created.recordId;
        out.result.inventoryRevision++;
        out.result.spellbookRevision++;
        out.result.commitSequence = context.nextCommitSequence();
        encode();
        DynamicRecordCommit commit;
        commit.accountId = context.accountId;
        commit.characterId = context.characterId;
        commit.requestId = request.requestId;
        commit.requestHash = hash;
        commit.resultPayload.assign(out.encodedResult.begin(), out.encodedResult.end());
        commit.expectedInventoryRevision = context.inventoryRevision;
        commit.resultingInventoryRevision = out.result.inventoryRevision;
        commit.expectedSpellbookRevision = context.spellbookRevision;
        commit.resultingSpellbookRevision = out.result.spellbookRevision;
        commit.inventory = out.inventory;
        commit.spellbook = out.spellbook;
        commit.merchantGoldMutation = merchantGold;
        if (prepared.entry)
            commit.records.push_back(*prepared.entry);
        const auto status = mDatabase.commitDynamicRecordRequest(commit);
        if (status == DynamicRecordCommitStatus::DuplicateRequest
            || status == DynamicRecordCommitStatus::DuplicateRequestConflict)
            return execute(request, hash, context);
        if (status != DynamicRecordCommitStatus::Committed)
        {
            out.result.accepted = false;
            out.result.recordId.clear();
            return reject(status == DynamicRecordCommitStatus::StaleSpellbookRevision ? Error::StaleSpellbookRevision
                                                                                      : Error::StaleInventoryRevision);
        }
        out.committed = true;
        if (prepared.entry)
            out.newRecords.push_back({ prepared.entry->record.recordType, prepared.entry->record.recordId,
                prepared.entry->record.data, prepared.entry->dependencyRecordIds });
        return out;
    }
}
