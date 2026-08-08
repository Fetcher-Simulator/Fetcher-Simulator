#include "AlchemyService.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include <components/alchemy/AlchemyMechanics.hpp>
#include <components/debug/debuglog.hpp>
#include <components/esm/attr.hpp>
#include <components/esm3/loadappa.hpp>
#include <components/esm3/loadclas.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadingr.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadskil.hpp>
#include <components/misc/rng.hpp>
#include <components/openmw-mp/Packets/Records/PacketAlchemyResult.hpp>
#include <components/openmw-mp/Records/DynamicRecordCodec.hpp>
#include <components/openmw-mp/Records/DynamicRecordFingerprint.hpp>
#include <components/openmw-mp/Records/DynamicRecordValidation.hpp>

#include <apps/openmw/mwworld/esmstore.hpp>

namespace
{
    std::vector<uint8_t> encodeResult(const mwmp::records::AlchemyResult& result)
    {
        mwmp::PacketAlchemyResult packet;
        packet.result = result;
        return packet.encode();
    }

    std::string asString(const std::vector<uint8_t>& bytes)
    {
        return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
    }

    /// Mirrors MWMechanics::Alchemy::getRecord(): name, script, weight, value,
    /// flags, and every effect field must match. Model and icon are
    /// intentionally ignored, exactly like single-player alchemy.
    bool nativeEquivalentPotion(const mwmp::records::Potion& left, const mwmp::records::Potion& right)
    {
        if (left.item.name != right.item.name || left.item.scriptId != right.item.scriptId
            || left.item.weight != right.item.weight || left.item.value != right.item.value
            || left.flags != right.flags)
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

    mwmp::records::Potion makePotionDto(const Crafting::PotionDefinition& definition)
    {
        mwmp::records::Potion potion;
        potion.item.recordFlags = 0;
        potion.item.name = definition.name;
        potion.item.model = definition.model;
        potion.item.icon = definition.icon;
        potion.item.weight = definition.weight;
        potion.item.value = definition.value;
        potion.flags = 0;
        potion.effects.reserve(definition.effects.size());
        for (const ESM::ENAMstruct& effect : definition.effects)
        {
            mwmp::records::MagicEffect target;
            target.effectId = refIdText(effect.mEffectID);
            target.skillId = refIdText(effect.mSkill);
            target.attributeId = refIdText(effect.mAttribute);
            target.range = effect.mRange;
            target.area = effect.mArea;
            target.duration = effect.mDuration;
            target.magnitudeMin = effect.mMagnMin;
            target.magnitudeMax = effect.mMagnMax;
            potion.effects.push_back(std::move(target));
        }
        return potion;
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

    /// Vanilla player skill-use progression, matching the default
    /// playerskillhandlers.lua formulas exactly:
    ///   progress += mUseValue[useType] / ((base + 1) * bonusFactor)
    ///   progress >= 1 -> base + 1, progress = 0, level progress += mult
    /// The attribute/specialization increase counters of the vanilla level-up
    /// dialog are not representable in the synced player state and are not
    /// applied (pre-existing multiplayer limitation).
    void applyAlchemySkillUse(mwmp::Skill& alchemySkill, float& levelProgress, const ESM::Skill* skillRecord,
        const ESM::Class& charClass, const MWWorld::ESMStore& store)
    {
        if (skillRecord == nullptr)
            throw std::runtime_error("alchemy skill record is missing from authoritative content");

        if (alchemySkill.base >= 100.f)
            return;

        const auto gmst = [&](std::string_view id) -> float {
            const ESM::GameSetting* setting = store.get<ESM::GameSetting>().search(ESM::RefId::stringRefId(id));
            if (setting == nullptr)
                throw std::runtime_error(std::string("alchemy skill progression GMST is missing: ") + std::string(id));
            return setting->mValue.getFloat();
        };

        const int alchemyIndex = ESM::Skill::refIdToIndex(ESM::Skill::Alchemy);
        const bool isMajor = skillInClass(charClass.mData.mSkills, true, alchemyIndex);
        const bool isMinor = skillInClass(charClass.mData.mSkills, false, alchemyIndex);

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

        const float gain = skillRecord->mData.mUseValue[ESM::Skill::Alchemy_CreatePotion];
        const float requirement = (alchemySkill.base + 1.f) * factor;
        alchemySkill.progress += gain / requirement;
        if (alchemySkill.progress >= 1.f)
        {
            if (alchemySkill.base < 100.f)
                alchemySkill.base += 1.f;
            alchemySkill.progress = 0.f;
            levelProgress += static_cast<float>(levelUpProgress);
        }
    }

    bool alchemyStatsChanged(const mwmp::BasePlayer& left, const mwmp::BasePlayer& right)
    {
        const int index = ESM::Skill::refIdToIndex(ESM::Skill::Alchemy);
        const mwmp::Skill& a = left.skills[index];
        const mwmp::Skill& b = right.skills[index];
        return a.base != b.base || a.mod != b.mod || a.damage != b.damage || a.progress != b.progress
            || left.level != right.level || left.levelProgress != right.levelProgress;
    }
}

namespace mwmp
{
    records::AlchemyResult AlchemyService::makeError(
        std::string requestId, records::AlchemyError error, uint64_t inventoryRevision)
    {
        records::AlchemyResult result;
        result.requestId = std::move(requestId);
        result.accepted = false;
        result.error = error;
        result.inventoryRevision = inventoryRevision;
        return result;
    }

    AlchemyService::Outcome AlchemyService::execute(
        const records::AlchemyRequest& request, std::string_view requestHash, const Context& context)
    {
        Outcome outcome;
        outcome.result.requestId = request.requestId;
        outcome.result.inventoryRevision = context.inventoryRevision;

        if (!context.player || !context.inventory || !context.store)
            throw std::invalid_argument("AlchemyService requires authoritative player state, inventory, and content");

        const auto existingRequest
            = mDatabase.loadCraftRequest(context.accountId, context.characterId, request.requestId);
        if (const auto& existing = existingRequest)
        {
            if (existing->requestHash != requestHash)
            {
                outcome.result = makeError(request.requestId, records::AlchemyError::DuplicateRequestConflict,
                    context.inventoryRevision);
                outcome.encodedResult = encodeResult(outcome.result);
                return outcome;
            }
            if (existing->status == "accepted" || existing->status == "rejected")
            {
                outcome.encodedResult.assign(existing->resultPayload.begin(), existing->resultPayload.end());
                PacketAlchemyResult packet;
                if (!packet.decode(outcome.encodedResult))
                    throw std::runtime_error("Persisted alchemy result is corrupt");
                outcome.result = std::move(packet.result);
                outcome.replayed = true;
                return outcome;
            }
            outcome.result = makeError(
                request.requestId, records::AlchemyError::RequestPending, context.inventoryRevision);
            outcome.encodedResult = encodeResult(outcome.result);
            return outcome;
        }

        // -------------------------------------------------------------------
        // Request-level validation. Every terminal rejection is journaled so a
        // retry replays the exact same rejection without mutation.
        // -------------------------------------------------------------------
        records::AlchemyError earlyError = records::AlchemyError::None;
        if (request.protocolVersion != records::CurrentAlchemyProtocolVersion)
            earlyError = records::AlchemyError::UnsupportedProtocol;
        else if (request.requestId.empty() || request.requestId.size() > 128 || requestHash.empty())
            earlyError = records::AlchemyError::InvalidRequest;
        else if (context.admissionError != records::CreateError::None)
            earlyError = context.admissionError == records::CreateError::RateLimited ? records::AlchemyError::RateLimited
                                                                                     : records::AlchemyError::ServerError;
        else if (request.inventoryRevision != context.inventoryRevision)
            earlyError = records::AlchemyError::StaleInventoryRevision;
        else if (request.count == 0 || request.count > records::MaxAlchemyAttempts)
            earlyError = records::AlchemyError::InvalidRequest;
        else if (request.potionName.empty() || request.potionName.size() > records::MaxAlchemyPotionNameLength)
            earlyError = records::AlchemyError::InvalidRequest;
        else if (request.ingredientInstanceIds.size() < 2
            || request.ingredientInstanceIds.size() > records::MaxAlchemyIngredients)
            earlyError = records::AlchemyError::InvalidRequest;
        else if (request.apparatusInstanceIds.size() > records::MaxAlchemyApparatus)
            earlyError = records::AlchemyError::InvalidRequest;

        // No source instance may appear twice anywhere in the request.
        if (earlyError == records::AlchemyError::None)
        {
            std::unordered_map<std::uint32_t, std::size_t> seen;
            for (const std::uint32_t id : request.ingredientInstanceIds)
            {
                if (!seen.emplace(id, 1).second)
                {
                    earlyError = records::AlchemyError::DuplicateSourceInstance;
                    break;
                }
            }
            if (earlyError == records::AlchemyError::None)
            {
                for (const std::uint32_t id : request.apparatusInstanceIds)
                {
                    if (!seen.emplace(id, 1).second)
                    {
                        earlyError = records::AlchemyError::DuplicateSourceInstance;
                        break;
                    }
                }
            }
        }

        auto reject = [&](records::AlchemyError error) {
            outcome.result = makeError(request.requestId, error, context.inventoryRevision);
            outcome.encodedResult = encodeResult(outcome.result);
            CraftRequestRecord journal;
            journal.accountId = context.accountId;
            journal.characterId = context.characterId;
            journal.requestId = request.requestId;
            journal.requestHash = std::string(requestHash);
            mDatabase.insertRejectedCraftRequest(journal, asString(outcome.encodedResult));
        };

        if (earlyError != records::AlchemyError::None)
        {
            reject(earlyError);
            return outcome;
        }

        // -------------------------------------------------------------------
        // Resolve exact inventory instances. Only instance identity proves
        // ownership; bare refIds are never accepted.
        // -------------------------------------------------------------------
        struct ResolvedIngredient
        {
            const ESM::Ingredient* record = nullptr;
            int count = 0;
        };

        std::vector<ResolvedIngredient> ingredients;
        ingredients.reserve(request.ingredientInstanceIds.size());
        for (const std::uint32_t instanceId : request.ingredientInstanceIds)
        {
            const Item* stack = nullptr;
            for (const Item& candidate : *context.inventory)
            {
                if (candidate.instanceId == instanceId)
                {
                    stack = &candidate;
                    break;
                }
            }
            if (stack == nullptr)
            {
                reject(records::AlchemyError::IngredientNotFound);
                return outcome;
            }
            if (stack->count <= 0)
            {
                reject(records::AlchemyError::IngredientNotOwned);
                return outcome;
            }
            if (stack->refId.empty())
            {
                reject(records::AlchemyError::InvalidIngredient);
                return outcome;
            }
            const ESM::Ingredient* record = context.store->get<ESM::Ingredient>().search(refIdFromText(stack->refId));
            if (record == nullptr)
            {
                // A known content record that is not an ingredient means the
                // wrong item type was submitted; an entirely unknown id means
                // the server content cannot be reconciled with the client.
                reject(context.isContentIdAllowed && context.isContentIdAllowed(stack->refId)
                        ? records::AlchemyError::InvalidIngredient
                        : records::AlchemyError::ContentMismatch);
                return outcome;
            }
            ingredients.push_back({ record, stack->count });
        }

        std::array<std::optional<float>, 4> apparatusQuality;
        for (const std::uint32_t instanceId : request.apparatusInstanceIds)
        {
            const Item* stack = nullptr;
            for (const Item& candidate : *context.inventory)
            {
                if (candidate.instanceId == instanceId)
                {
                    stack = &candidate;
                    break;
                }
            }
            if (stack == nullptr || stack->count <= 0)
            {
                reject(records::AlchemyError::ApparatusNotFound);
                return outcome;
            }
            const ESM::Apparatus* record = context.store->get<ESM::Apparatus>().search(refIdFromText(stack->refId));
            if (record == nullptr)
            {
                reject(context.isContentIdAllowed && context.isContentIdAllowed(stack->refId)
                        ? records::AlchemyError::InvalidApparatus
                        : records::AlchemyError::ContentMismatch);
                return outcome;
            }
            // The client contributes instance identities only; the apparatus
            // type comes from the authoritative content record. Two selected
            // apparatus of the same type are an invalid combination because
            // the native UI has exactly one slot per type.
            const int32_t type = record->mData.mType;
            if (type < 0 || type >= static_cast<int32_t>(apparatusQuality.size()))
            {
                reject(records::AlchemyError::InvalidApparatus);
                return outcome;
            }
            if (apparatusQuality[type])
            {
                reject(records::AlchemyError::InvalidApparatus);
                return outcome;
            }
            apparatusQuality[type] = record->mData.mQuality;
        }
        if (!apparatusQuality[ESM::Apparatus::MortarPestle])
        {
            reject(records::AlchemyError::InvalidApparatus);
            return outcome;
        }

        // -------------------------------------------------------------------
        // Build the shared mechanics input from authoritative state.
        // -------------------------------------------------------------------
        const MWWorld::ESMStore& store = *context.store;
        Crafting::AlchemyMechanicsInput mechanics;
        mechanics.ingredients.reserve(ingredients.size());
        for (const ResolvedIngredient& resolved : ingredients)
        {
            Crafting::AlchemyMechanicsInput::Ingredient ingredient;
            ingredient.weight = resolved.record->mData.mWeight;
            ingredient.count = resolved.count;
            for (std::size_t i = 0; i < 4; ++i)
            {
                ingredient.effectIds[i] = resolved.record->mData.mEffectID[i];
                ingredient.skills[i] = resolved.record->mData.mSkills[i];
                ingredient.attributes[i] = resolved.record->mData.mAttributes[i];
            }
            mechanics.ingredients.push_back(std::move(ingredient));
        }
        mechanics.apparatusQuality = apparatusQuality;

        const int alchemyIndex = ESM::Skill::refIdToIndex(ESM::Skill::Alchemy);
        const int intelligenceIndex = ESM::Attribute::refIdToIndex(ESM::Attribute::Intelligence);
        const int luckIndex = ESM::Attribute::refIdToIndex(ESM::Attribute::Luck);
        const Skill& alchemySkill = context.player->skills[alchemyIndex];
        const Attribute& intelligence = context.player->attributes[intelligenceIndex];
        const Attribute& luck = context.player->attributes[luckIndex];
        mechanics.alchemySkill = modifiedValue(alchemySkill.base, alchemySkill.damage, alchemySkill.mod);
        mechanics.intelligence = modifiedValue(
            static_cast<float>(intelligence.base), intelligence.damage, intelligence.mod);
        mechanics.luck = modifiedValue(static_cast<float>(luck.base), luck.damage, luck.mod);

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

        Crafting::AlchemyMechanics::Result ready;
        try
        {
            ready = Crafting::AlchemyMechanics::getReadyStatus(mechanics, request.potionName);
        }
        catch (const std::exception&)
        {
            reject(records::AlchemyError::MechanicsValidationFailed);
            return outcome;
        }

        // Resolve the skill-progression inputs before rolling so a content
        // gap fails the request instead of wasting an attempt.
        const ESM::Skill* skillRecord = store.get<ESM::Skill>().search(ESM::Skill::Alchemy);

        // Native semantics: an effectless setup consumes one set of
        // ingredients and fails. This is a committed (accepted) outcome.
        const bool committedFailureOnly = ready == Crafting::AlchemyMechanics::Result::NoEffects;
        if (!committedFailureOnly && ready != Crafting::AlchemyMechanics::Result::Success)
        {
            reject(records::AlchemyError::InvalidRequest);
            return outcome;
        }

        // Native semantics: the attempt count is capped by the smallest
        // ingredient stack. A matching inventory revision makes a larger
        // client count impossible without a stale or hostile client. The
        // effectless path consumes exactly one set regardless of the count,
        // exactly like the native NoEffects branch.
        int maxAttempts = std::numeric_limits<int>::max();
        for (const ResolvedIngredient& resolved : ingredients)
            maxAttempts = std::min(maxAttempts, resolved.count);
        if (!committedFailureOnly && request.count > static_cast<std::uint32_t>(maxAttempts))
        {
            reject(records::AlchemyError::InvalidRequest);
            return outcome;
        }

        // -------------------------------------------------------------------
        // Authoritative attempts. The server owns the RNG; the terminal result
        // durably captures every roll so retries never reroll.
        // -------------------------------------------------------------------
        Misc::Rng::Generator prng(
            context.rngSeed ? *context.rngSeed : Misc::Rng::generateDefaultSeed());

        // Request-local record resolution state: native-equivalent reuse
        // candidates produced earlier in this same request.
        std::vector<std::pair<std::string, records::Potion>> localPotions;
        std::unordered_map<std::string, std::string> localByFingerprint;

        std::vector<DynamicRecordService::CommittedRecord> newRecords;
        std::vector<DynamicRecordCommitEntry> commitEntries;
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
        const auto sameStack = [](const Item& left, const Item& right) {
            return left.refId == right.refId && left.charge == right.charge
                && std::abs(left.enchantmentCharge - right.enchantmentCharge) < 0.001f
                && left.soul == right.soul;
        };
        const auto grantPotion = [&](const std::string& recordId) {
            Item potion;
            potion.refId = recordId;
            potion.count = 1;
            potion.charge = -1;
            potion.enchantmentCharge = -1.f;
            const auto it = std::find_if(
                nextInventory.begin(), nextInventory.end(), [&](const Item& existing) { return sameStack(existing, potion); });
            if (it != nextInventory.end())
                ++it->count;
            else
                nextInventory.push_back(std::move(potion));
        };

        // Native getRecord-equivalent search over the runtime dynamic Potion
        // records in deterministic order; request-local successes win.
        std::vector<std::pair<std::string, std::string>> dynamicPotions;
        if (context.listDynamicPotions)
            dynamicPotions = context.listDynamicPotions();
        std::sort(dynamicPotions.begin(), dynamicPotions.end(),
            [](const auto& left, const auto& right) { return left.first < right.first; });

        const auto findNativeEquivalent = [&](const records::Potion& potion) -> std::optional<std::string> {
            for (const auto& [recordId, localPotion] : localPotions)
            {
                if (nativeEquivalentPotion(localPotion, potion))
                    return recordId;
            }
            for (const auto& [recordId, definition] : dynamicPotions)
            {
                try
                {
                    const records::DynamicRecordDefinition decoded = records::decodeDefinition(definition);
                    const records::Potion* candidate = std::get_if<records::Potion>(&decoded.data);
                    if (candidate == nullptr)
                        continue;
                    if (nativeEquivalentPotion(*candidate, potion))
                        return recordId;
                }
                catch (const std::exception&)
                {
                    // A corrupt runtime record must not poison alchemy; the
                    // canonical record validator handles it separately.
                }
            }
            return std::nullopt;
        };

        const auto findFingerprintEquivalent = [&](records::RecordType type, std::string_view fingerprint)
            -> std::optional<DynamicRecordService::CatalogRecord> {
            const std::string key(fingerprint);
            const auto local = localByFingerprint.find(key);
            if (local != localByFingerprint.end())
                return DynamicRecordService::CatalogRecord{
                    std::string(records::getRecordTypeName(type)), local->second, key, {} };
            if (context.findEquivalent)
                return context.findEquivalent(type, fingerprint);
            return std::nullopt;
        };

        const auto contentAllowed = [&](std::string_view id) {
            return id.empty() || (context.isContentIdAllowed && context.isContentIdAllowed(id));
        };
        const auto modelAllowed = [&](std::string_view path) {
            return path.empty() || (context.isModelAllowed && context.isModelAllowed(path));
        };
        const auto iconAllowed = [&](std::string_view path) {
            return path.empty() || (context.isIconAllowed && context.isIconAllowed(path));
        };

        DynamicRecordService recordService(mDatabase);
        try
        {
            if (committedFailureOnly)
            {
                records::AlchemyAttemptResult attempt;
                attempt.success = false;
                outcome.result.attempts.push_back(std::move(attempt));
                for (const std::uint32_t instanceId : request.ingredientInstanceIds)
                    consumeOneOf(instanceId);
            }
            else
            {
                outcome.result.attempts.reserve(request.count);
                for (std::uint32_t i = 0; i < request.count; ++i)
                {
                    const Crafting::AlchemyMechanics::Attempt roll
                        = Crafting::AlchemyMechanics::createSingle(mechanics, prng, request.potionName);
                    records::AlchemyAttemptResult attempt;
                    attempt.success = roll.success;
                    if (roll.success)
                    {
                        records::Potion potion = makePotionDto(roll.potion);
                        if (!modelAllowed(potion.item.model) || !iconAllowed(potion.item.icon))
                        {
                            reject(records::AlchemyError::ContentMismatch);
                            return outcome;
                        }
                        for (const records::MagicEffect& effect : potion.effects)
                        {
                            if (!contentAllowed(effect.effectId) || !contentAllowed(effect.skillId)
                                || !contentAllowed(effect.attributeId))
                            {
                                reject(records::AlchemyError::ContentMismatch);
                                return outcome;
                            }
                        }

                        if (const auto nativeId = findNativeEquivalent(potion))
                        {
                            attempt.recordId = *nativeId;
                            attempt.reused = true;
                            localPotions.emplace_back(attempt.recordId, std::move(potion));
                        }
                        else
                        {
                            records::DynamicRecordDefinition definition = records::canonicalize(
                                records::DynamicRecordDefinition{ records::CurrentSchemaVersion, potion });
                            const std::string fingerprint = records::fingerprint(definition);
                            const auto fingerprintMatch = localByFingerprint.find(fingerprint);
                            if (fingerprintMatch != localByFingerprint.end())
                            {
                                attempt.recordId = fingerprintMatch->second;
                                attempt.reused = true;
                                localPotions.emplace_back(attempt.recordId, std::move(potion));
                            }
                            else
                            {
                                records::RecordDraft draft;
                                draft.temporaryKey = "potion" + std::to_string(i);
                                draft.definition = std::move(definition);
                                const DynamicRecordService::PreparedRecord prepared = recordService.prepareSingleRecord(
                                    draft, context, findFingerprintEquivalent, context.allocateId);
                                attempt.recordId = prepared.created.recordId;
                                attempt.reused = prepared.created.reused;
                                if (prepared.entry)
                                {
                                    commitEntries.push_back(*prepared.entry);
                                    DynamicRecordService::CommittedRecord runtime;
                                    runtime.recordType = prepared.entry->record.recordType;
                                    runtime.recordId = prepared.entry->record.recordId;
                                    runtime.definition = prepared.entry->record.data;
                                    newRecords.push_back(std::move(runtime));
                                }
                                localByFingerprint.emplace(fingerprint, attempt.recordId);
                                localPotions.emplace_back(attempt.recordId, std::move(potion));
                            }
                        }
                    }
                    outcome.result.attempts.push_back(std::move(attempt));

                    // Every attempt consumes one of each ingredient, success
                    // or failure (native semantics).
                    for (const std::uint32_t instanceId : request.ingredientInstanceIds)
                        consumeOneOf(instanceId);
                    if (roll.success)
                        grantPotion(outcome.result.attempts.back().recordId);
                }
            }

            if (commitEntries.size() > context.maximumNewRecords)
            {
                reject(records::AlchemyError::QuotaExceeded);
                return outcome;
            }
        }
        catch (const std::exception& e)
        {
            Log(Debug::Error) << "[AlchemyService] mechanics failure requestId=" << request.requestId
                              << " what=" << e.what();
            reject(records::AlchemyError::MechanicsValidationFailed);
            return outcome;
        }

        // -------------------------------------------------------------------
        // Authoritative skill progression (vanilla player semantics): one use
        // per successful attempt.
        // -------------------------------------------------------------------
        std::optional<BasePlayer> resultingStats;
        {
            const bool anySuccess = std::any_of(outcome.result.attempts.begin(), outcome.result.attempts.end(),
                [](const records::AlchemyAttemptResult& attempt) { return attempt.success; });
            if (anySuccess)
            {
                BasePlayer stats = *context.player;
                const std::size_t successCount = static_cast<std::size_t>(std::count_if(
                    outcome.result.attempts.begin(), outcome.result.attempts.end(),
                    [](const records::AlchemyAttemptResult& attempt) { return attempt.success; }));
                try
                {
                    for (std::size_t i = 0; i < successCount; ++i)
                        applyAlchemySkillUse(
                            stats.skills[alchemyIndex], stats.levelProgress, skillRecord, context.player->charClass, store);
                }
                catch (const std::exception&)
                {
                    reject(records::AlchemyError::MechanicsValidationFailed);
                    return outcome;
                }
                if (alchemyStatsChanged(stats, *context.player))
                    resultingStats = std::move(stats);
            }
        }

        // -------------------------------------------------------------------
        // Atomic commit: record definitions + inventory + revision + stats +
        // journal in one transaction. All-or-nothing.
        // -------------------------------------------------------------------
        if (context.reconcileInventory)
            context.reconcileInventory(nextInventory);

        outcome.result.accepted = true;
        outcome.result.error = records::AlchemyError::None;
        outcome.result.inventoryRevision = context.inventoryRevision + 1;
        outcome.result.commitSequence
            = context.nextCommitSequence ? context.nextCommitSequence() : 0;
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
            outcome.result = makeError(request.requestId, records::AlchemyError::StaleInventoryRevision,
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
