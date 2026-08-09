#include "EsmDynamicRecordConversion.hpp"

#include <stdexcept>
#include <iterator>
#include <type_traits>

namespace mwmp::records
{
    namespace
    {
        std::string idText(const ESM::RefId& id) { return id.empty() ? std::string{} : id.serializeText(); }

        ESM::RefId refId(std::string_view value)
        {
            if (value.empty())
                return {};
            ESM::RefId result = ESM::RefId::deserializeText(value);
            return result.empty() ? ESM::RefId::stringRefId(value) : result;
        }

        ESM::RefId contentId(const RecordReference& reference)
        {
            if (reference.kind == ReferenceKind::TemporaryKey)
                throw std::runtime_error("Temporary record reference must be resolved before ESM conversion");
            return reference.kind == ReferenceKind::ContentId ? refId(reference.value) : ESM::RefId{};
        }

        RecordReference reference(const ESM::RefId& id)
        {
            return id.empty() ? RecordReference{} : RecordReference{ ReferenceKind::ContentId, id.serializeText() };
        }

        void toEsmItem(const ItemFields& source, std::uint32_t& recordFlags, std::string& name, ESM::Path& model,
            ESM::Path& icon, ESM::RefId& script)
        {
            recordFlags = source.recordFlags;
            name = source.name;
            model = source.model;
            icon = source.icon;
            script = refId(source.scriptId);
        }

        ItemFields fromEsmItem(std::uint32_t recordFlags, const std::string& name, const ESM::Path& model,
            const ESM::Path& icon, const ESM::RefId& script, float weight, std::int32_t value)
        {
            ItemFields result;
            result.recordFlags = recordFlags;
            result.name = name;
            result.model = model.getOriginal();
            result.icon = icon.getOriginal();
            result.scriptId = idText(script);
            result.weight = weight;
            result.value = value;
            return result;
        }

        ESM::EffectList toEsmEffects(const std::vector<MagicEffect>& effects)
        {
            ESM::EffectList result;
            result.mList.reserve(effects.size());
            for (std::size_t i = 0; i < effects.size(); ++i)
            {
                const MagicEffect& source = effects[i];
                ESM::IndexedENAMstruct target;
                target.mIndex = static_cast<std::uint32_t>(i);
                target.mData.mEffectID = refId(source.effectId);
                target.mData.mSkill = refId(source.skillId);
                target.mData.mAttribute = refId(source.attributeId);
                target.mData.mRange = source.range;
                target.mData.mArea = source.area;
                target.mData.mDuration = source.duration;
                target.mData.mMagnMin = source.magnitudeMin;
                target.mData.mMagnMax = source.magnitudeMax;
                result.mList.push_back(std::move(target));
            }
            return result;
        }

        std::vector<MagicEffect> fromEsmEffects(const ESM::EffectList& effects)
        {
            std::vector<MagicEffect> result;
            result.reserve(effects.mList.size());
            for (const ESM::IndexedENAMstruct& source : effects.mList)
            {
                MagicEffect target;
                target.effectId = idText(source.mData.mEffectID);
                target.skillId = idText(source.mData.mSkill);
                target.attributeId = idText(source.mData.mAttribute);
                target.range = source.mData.mRange;
                target.area = source.mData.mArea;
                target.duration = source.mData.mDuration;
                target.magnitudeMin = source.mData.mMagnMin;
                target.magnitudeMax = source.mData.mMagnMax;
                result.push_back(std::move(target));
            }
            return result;
        }

        ESM::PartReferenceList toEsmParts(const std::vector<BodyPartReference>& parts)
        {
            ESM::PartReferenceList result;
            result.mParts.reserve(parts.size());
            for (const BodyPartReference& source : parts)
            {
                ESM::PartReference target;
                target.mPart = source.part;
                target.mMale = refId(source.maleId);
                target.mFemale = refId(source.femaleId);
                result.mParts.push_back(std::move(target));
            }
            return result;
        }

        std::vector<BodyPartReference> fromEsmParts(const ESM::PartReferenceList& parts)
        {
            std::vector<BodyPartReference> result;
            result.reserve(parts.mParts.size());
            for (const ESM::PartReference& source : parts.mParts)
                result.push_back({ source.mPart, idText(source.mMale), idText(source.mFemale) });
            return result;
        }

        ESM::DialogueCondition toEsmCondition(const DialogueCondition& source)
        {
            ESM::DialogueCondition target;
            target.mVariable = source.variable;
            target.mValue = source.value;
            target.mIndex = source.index;
            target.mFunction = static_cast<ESM::DialogueCondition::Function>(source.function);
            target.mComparison = static_cast<ESM::DialogueCondition::Comparison>(source.comparison);
            return target;
        }

        DialogueCondition fromEsmCondition(const ESM::DialogueCondition& source)
        {
            DialogueCondition target;
            target.variable = source.mVariable;
            target.value = source.mValue;
            target.index = source.mIndex;
            target.function = static_cast<std::int8_t>(source.mFunction);
            target.comparison = static_cast<char>(source.mComparison);
            return target;
        }

        ESM::DialInfo toEsmDialogueInfo(const DialogueInfo& source)
        {
            ESM::DialInfo target;
            target.blank();
            target.mId = refId(source.infoId);
            target.mData.mType = source.dialogueType;
            target.mData.mDisposition = source.dispositionOrJournalIndex;
            target.mData.mRank = source.rank;
            target.mData.mGender = source.gender;
            target.mData.mPCrank = source.pcRank;
            target.mSelects.reserve(source.conditions.size());
            for (const DialogueCondition& condition : source.conditions)
                target.mSelects.push_back(toEsmCondition(condition));
            target.mActor = refId(source.actorId);
            target.mRace = refId(source.raceId);
            target.mClass = refId(source.classId);
            target.mFaction = refId(source.factionId);
            target.mPcFaction = refId(source.pcFactionId);
            target.mCell = refId(source.cellId);
            target.mSound = source.sound;
            target.mResponse = source.response;
            target.mResultScript = source.resultScript;
            target.mFactionLess = source.factionLess;
            target.mQuestStatus = static_cast<ESM::DialInfo::QuestStatus>(source.questStatus);
            return target;
        }

        DialogueInfo fromEsmDialogueInfo(const ESM::DialInfo& source)
        {
            DialogueInfo target;
            target.infoId = idText(source.mId);
            target.dialogueType = source.mData.mType;
            target.dispositionOrJournalIndex = source.mData.mDisposition;
            target.rank = source.mData.mRank;
            target.gender = source.mData.mGender;
            target.pcRank = source.mData.mPCrank;
            target.conditions.reserve(source.mSelects.size());
            for (const ESM::DialogueCondition& condition : source.mSelects)
                target.conditions.push_back(fromEsmCondition(condition));
            target.actorId = idText(source.mActor);
            target.raceId = idText(source.mRace);
            target.classId = idText(source.mClass);
            target.factionId = idText(source.mFaction);
            target.pcFactionId = idText(source.mPcFaction);
            target.cellId = idText(source.mCell);
            target.sound = source.mSound;
            target.response = source.mResponse;
            target.resultScript = source.mResultScript;
            target.factionLess = source.mFactionLess;
            target.questStatus = static_cast<std::int8_t>(source.mQuestStatus);
            return target;
        }
    }

    EsmDynamicRecord toEsmRecord(const DynamicRecordDefinition& definition)
    {
        if (definition.schemaVersion != CurrentSchemaVersion)
            throw std::runtime_error("Unsupported dynamic record schema version");

        return std::visit(
            [](const auto& source) -> EsmDynamicRecord {
                using Record = std::decay_t<decltype(source)>;
                if constexpr (std::is_same_v<Record, Potion>)
                {
                    ESM::Potion target;
                    target.blank();
                    toEsmItem(source.item, target.mRecordFlags, target.mName, target.mModel, target.mIcon, target.mScript);
                    target.mData.mWeight = source.item.weight;
                    target.mData.mValue = source.item.value;
                    target.mData.mFlags = source.flags;
                    target.mEffects = toEsmEffects(source.effects);
                    return target;
                }
                else if constexpr (std::is_same_v<Record, Enchantment>)
                {
                    ESM::Enchantment target;
                    target.blank();
                    target.mRecordFlags = source.recordFlags;
                    target.mData.mType = source.type;
                    target.mData.mCost = source.cost;
                    target.mData.mCharge = source.charge;
                    target.mData.mFlags = source.flags;
                    target.mEffects = toEsmEffects(source.effects);
                    return target;
                }
                else if constexpr (std::is_same_v<Record, Weapon>)
                {
                    ESM::Weapon target;
                    target.blank();
                    toEsmItem(source.item, target.mRecordFlags, target.mName, target.mModel, target.mIcon, target.mScript);
                    target.mEnchant = contentId(source.enchantment);
                    target.mData.mWeight = source.item.weight;
                    target.mData.mValue = source.item.value;
                    target.mData.mType = source.type;
                    target.mData.mHealth = source.health;
                    target.mData.mSpeed = source.speed;
                    target.mData.mReach = source.reach;
                    target.mData.mEnchant = source.enchantCapacity;
                    target.mData.mChop = source.chop;
                    target.mData.mSlash = source.slash;
                    target.mData.mThrust = source.thrust;
                    target.mData.mFlags = source.flags;
                    return target;
                }
                else if constexpr (std::is_same_v<Record, Armor>)
                {
                    ESM::Armor target;
                    target.blank();
                    toEsmItem(source.item, target.mRecordFlags, target.mName, target.mModel, target.mIcon, target.mScript);
                    target.mEnchant = contentId(source.enchantment);
                    target.mData.mType = source.type;
                    target.mData.mWeight = source.item.weight;
                    target.mData.mValue = source.item.value;
                    target.mData.mHealth = source.health;
                    target.mData.mEnchant = source.enchantCapacity;
                    target.mData.mArmor = source.armorRating;
                    target.mParts = toEsmParts(source.parts);
                    return target;
                }
                else if constexpr (std::is_same_v<Record, Clothing>)
                {
                    ESM::Clothing target;
                    target.blank();
                    toEsmItem(source.item, target.mRecordFlags, target.mName, target.mModel, target.mIcon, target.mScript);
                    target.mEnchant = contentId(source.enchantment);
                    target.mData.mType = source.type;
                    target.mData.mWeight = source.item.weight;
                    target.mData.mValue = static_cast<std::uint16_t>(source.item.value);
                    target.mData.mEnchant = source.enchantCapacity;
                    target.mParts = toEsmParts(source.parts);
                    return target;
                }
                else if constexpr (std::is_same_v<Record, Book>)
                {
                    ESM::Book target;
                    target.blank();
                    toEsmItem(source.item, target.mRecordFlags, target.mName, target.mModel, target.mIcon, target.mScript);
                    target.mEnchant = contentId(source.enchantment);
                    target.mData.mWeight = source.item.weight;
                    target.mData.mValue = source.item.value;
                    target.mData.mIsScroll = source.isScroll ? 1 : 0;
                    target.mData.mSkillId = source.skillId;
                    target.mData.mEnchant = source.enchantCapacity;
                    target.mText = source.text;
                    return target;
                }
                else if constexpr (std::is_same_v<Record, Dialogue>)
                {
                    ESM::Dialogue target;
                    target.blank();
                    target.mStringId = source.stringId;
                    target.mType = static_cast<ESM::Dialogue::Type>(source.type);
                    for (const DialogueInfo& info : source.infos)
                        target.mInfo.push_back(toEsmDialogueInfo(info));
                    for (auto it = target.mInfo.begin(); it != target.mInfo.end(); ++it)
                    {
                        it->mPrev = it == target.mInfo.begin() ? ESM::RefId{} : std::prev(it)->mId;
                        const auto next = std::next(it);
                        it->mNext = next == target.mInfo.end() ? ESM::RefId{} : next->mId;
                    }
                    return target;
                }
                else
                {
                    ESM::Script target;
                    target.mRecordFlags = source.recordFlags;
                    target.mNumShorts = 0;
                    target.mNumLongs = 0;
                    target.mNumFloats = 0;
                    target.mVarNames.clear();
                    target.mScriptData.clear();
                    target.mScriptText = source.sourceText;
                    return target;
                }
            },
            definition.data);
    }

    DynamicRecordDefinition fromEsmRecord(const ESM::Potion& source)
    {
        Potion target;
        target.item = fromEsmItem(source.mRecordFlags, source.mName, source.mModel, source.mIcon, source.mScript,
            source.mData.mWeight, source.mData.mValue);
        target.flags = source.mData.mFlags;
        target.effects = fromEsmEffects(source.mEffects);
        return { CurrentSchemaVersion, std::move(target) };
    }

    DynamicRecordDefinition fromEsmRecord(const ESM::Enchantment& source)
    {
        Enchantment target;
        target.recordFlags = source.mRecordFlags;
        target.type = source.mData.mType;
        target.cost = source.mData.mCost;
        target.charge = source.mData.mCharge;
        target.flags = source.mData.mFlags;
        target.effects = fromEsmEffects(source.mEffects);
        return { CurrentSchemaVersion, std::move(target) };
    }

    DynamicRecordDefinition fromEsmRecord(const ESM::Weapon& source)
    {
        Weapon target;
        target.item = fromEsmItem(source.mRecordFlags, source.mName, source.mModel, source.mIcon, source.mScript,
            source.mData.mWeight, source.mData.mValue);
        target.enchantment = reference(source.mEnchant);
        target.type = source.mData.mType;
        target.health = source.mData.mHealth;
        target.speed = source.mData.mSpeed;
        target.reach = source.mData.mReach;
        target.enchantCapacity = source.mData.mEnchant;
        target.chop = source.mData.mChop;
        target.slash = source.mData.mSlash;
        target.thrust = source.mData.mThrust;
        target.flags = source.mData.mFlags;
        return { CurrentSchemaVersion, std::move(target) };
    }

    DynamicRecordDefinition fromEsmRecord(const ESM::Armor& source)
    {
        Armor target;
        target.item = fromEsmItem(source.mRecordFlags, source.mName, source.mModel, source.mIcon, source.mScript,
            source.mData.mWeight, source.mData.mValue);
        target.enchantment = reference(source.mEnchant);
        target.type = source.mData.mType;
        target.health = source.mData.mHealth;
        target.enchantCapacity = source.mData.mEnchant;
        target.armorRating = source.mData.mArmor;
        target.parts = fromEsmParts(source.mParts);
        return { CurrentSchemaVersion, std::move(target) };
    }

    DynamicRecordDefinition fromEsmRecord(const ESM::Clothing& source)
    {
        Clothing target;
        target.item = fromEsmItem(source.mRecordFlags, source.mName, source.mModel, source.mIcon, source.mScript,
            source.mData.mWeight, source.mData.mValue);
        target.enchantment = reference(source.mEnchant);
        target.type = source.mData.mType;
        target.enchantCapacity = source.mData.mEnchant;
        target.parts = fromEsmParts(source.mParts);
        return { CurrentSchemaVersion, std::move(target) };
    }

    DynamicRecordDefinition fromEsmRecord(const ESM::Book& source)
    {
        Book target;
        target.item = fromEsmItem(source.mRecordFlags, source.mName, source.mModel, source.mIcon, source.mScript,
            source.mData.mWeight, source.mData.mValue);
        target.enchantment = reference(source.mEnchant);
        target.text = source.mText;
        target.isScroll = source.mData.mIsScroll != 0;
        target.skillId = source.mData.mSkillId;
        target.enchantCapacity = source.mData.mEnchant;
        return { CurrentSchemaVersion, std::move(target) };
    }

    DynamicRecordDefinition fromEsmRecord(const ESM::Dialogue& source)
    {
        Dialogue target;
        target.stringId = source.mStringId;
        target.type = static_cast<std::int8_t>(source.mType);
        target.infos.reserve(source.mInfo.size());
        for (const ESM::DialInfo& info : source.mInfo)
            target.infos.push_back(fromEsmDialogueInfo(info));
        return { CurrentSchemaVersion, std::move(target) };
    }

    DynamicRecordDefinition fromEsmRecord(const ESM::Script& source)
    {
        Script target;
        target.recordFlags = source.mRecordFlags;
        target.sourceText = source.mScriptText;
        return { CurrentSchemaVersion, std::move(target) };
    }
}
