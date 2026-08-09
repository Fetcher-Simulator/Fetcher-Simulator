#include "ResolvedContentFingerprint.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_set>
#include <vector>

#include <components/esm3/loadappa.hpp>
#include <components/esm3/loadclas.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadingr.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadskil.hpp>
#include <components/openmw-mp/Records/DynamicRecordCodec.hpp>
#include <components/openmw-mp/Records/DynamicRecordValidation.hpp>
#include <components/openmw-mp/Records/EsmDynamicRecordConversion.hpp>
#include <components/openmw-mp/Sha256.hpp>

#include <apps/openmw/mwworld/esmstore.hpp>

namespace
{
    enum class ResolvedRecordType : std::uint8_t
    {
        Potion = 1,
        Enchantment = 2,
        Weapon = 3,
        Armor = 4,
        Clothing = 5,
        Book = 6,
        Ingredient = 0x40,
        Apparatus = 0x41,
        MagicEffect = 0x42,
        GameSetting = 0x43,
        Skill = 0x44,
        Class = 0x45,
        Creature = 0x46,
        NPC = 0x47,
    };

    struct Entry
    {
        std::uint8_t type = 0;
        std::string id;
        std::string definition;
    };

    void appendU32(std::string& out, std::uint32_t value)
    {
        out.push_back(static_cast<char>(value));
        out.push_back(static_cast<char>(value >> 8));
        out.push_back(static_cast<char>(value >> 16));
        out.push_back(static_cast<char>(value >> 24));
    }

    void appendI32(std::string& out, std::int32_t value)
    {
        appendU32(out, static_cast<std::uint32_t>(value));
    }

    void appendFloat(std::string& out, float value)
    {
        appendU32(out, std::bit_cast<std::uint32_t>(value == 0.f ? 0.f : value));
    }

    void appendString(std::string& out, std::string_view value)
    {
        appendU32(out, static_cast<std::uint32_t>(value.size()));
        out.append(value);
    }

    void appendRefId(std::string& out, const ESM::RefId& value)
    {
        appendString(out, value.serializeText());
    }

    void appendPath(std::string& out, const ESM::Path& value)
    {
        appendString(out, value.getNormalized().value());
    }

    template <class T>
    void appendTypedRecords(const MWWorld::ESMStore& store, mwmp::records::RecordType type, std::vector<Entry>& out)
    {
        std::unordered_set<ESM::RefId> visited;
        for (const T& record : store.get<T>())
        {
            if (!visited.insert(record.mId).second)
                continue;
            const T* staticRecord = store.get<T>().searchStatic(record.mId);
            if (staticRecord == nullptr)
                continue;
            Entry entry;
            entry.type = static_cast<std::uint8_t>(type);
            entry.id = staticRecord->mId.toString();
            entry.definition = mwmp::records::encodeDefinition(
                mwmp::records::canonicalize(mwmp::records::fromEsmRecord(*staticRecord)));
            out.push_back(std::move(entry));
        }
    }

    template <class T, class Encode>
    void appendMechanicsRecords(
        const MWWorld::ESMStore& store, ResolvedRecordType type, std::vector<Entry>& out, Encode&& encode)
    {
        for (const T& record : store.get<T>())
        {
            // The normal client promotes the special Player NPC to the dynamic
            // store during World::loadData(), while the headless authoritative
            // content registry intentionally skips gameplay-state initialization.
            // Player is mutable runtime character state and cannot be a paid
            // enchanting service provider, so it must not participate in the
            // resolved static NPC content identity.
            if constexpr (std::is_same_v<T, ESM::NPC>)
            {
                if (record.mId == ESM::RefId::stringRefId("Player"))
                    continue;
            }
            if (store.get<T>().isDynamic(record.mId))
                continue;
            Entry entry;
            entry.type = static_cast<std::uint8_t>(type);
            entry.id = record.mId.toString();
            encode(record, entry.definition);
            out.push_back(std::move(entry));
        }
    }

    void appendIngredient(const ESM::Ingredient& record, std::string& out)
    {
        appendU32(out, record.mRecordFlags);
        appendString(out, record.mName);
        appendPath(out, record.mModel);
        appendPath(out, record.mIcon);
        appendRefId(out, record.mScript);
        appendFloat(out, record.mData.mWeight);
        appendI32(out, record.mData.mValue);
        for (std::size_t i = 0; i < 4; ++i)
        {
            appendRefId(out, record.mData.mEffectID[i]);
            appendRefId(out, record.mData.mSkills[i]);
            appendRefId(out, record.mData.mAttributes[i]);
        }
    }

    void appendApparatus(const ESM::Apparatus& record, std::string& out)
    {
        appendU32(out, record.mRecordFlags);
        appendString(out, record.mName);
        appendPath(out, record.mModel);
        appendPath(out, record.mIcon);
        appendRefId(out, record.mScript);
        appendI32(out, record.mData.mType);
        appendFloat(out, record.mData.mQuality);
        appendFloat(out, record.mData.mWeight);
        appendI32(out, record.mData.mValue);
    }

    void appendMagicEffect(const ESM::MagicEffect& record, std::string& out)
    {
        appendU32(out, record.mRecordFlags);
        appendRefId(out, record.mData.mSchool);
        appendFloat(out, record.mData.mBaseCost);
        appendI32(out, record.mData.mFlags);
        appendI32(out, record.mData.mRed);
        appendI32(out, record.mData.mGreen);
        appendI32(out, record.mData.mBlue);
        appendFloat(out, record.mData.mUnknown1);
        appendFloat(out, record.mData.mSpeed);
        appendFloat(out, record.mData.mUnknown2);
        appendPath(out, record.mIcon);
        appendPath(out, record.mParticle);
        appendRefId(out, record.mCasting);
        appendRefId(out, record.mHit);
        appendRefId(out, record.mArea);
        appendRefId(out, record.mBolt);
        appendRefId(out, record.mCastSound);
        appendRefId(out, record.mBoltSound);
        appendRefId(out, record.mHitSound);
        appendRefId(out, record.mAreaSound);
        appendString(out, record.mDescription);
        appendString(out, record.mName);
    }

    void appendGameSetting(const ESM::GameSetting& record, std::string& out)
    {
        appendU32(out, record.mRecordFlags);
        appendU32(out, static_cast<std::uint32_t>(record.mValue.getType()));
        switch (record.mValue.getType())
        {
            case ESM::VT_Short:
            case ESM::VT_Int:
            case ESM::VT_Long:
                appendI32(out, record.mValue.getInteger());
                break;
            case ESM::VT_Float:
                appendFloat(out, record.mValue.getFloat());
                break;
            case ESM::VT_String:
                appendString(out, record.mValue.getString());
                break;
            case ESM::VT_Unknown:
            case ESM::VT_None:
                break;
        }
    }

    void appendSkill(const ESM::Skill& record, std::string& out)
    {
        appendU32(out, record.mRecordFlags);
        appendI32(out, record.mData.mAttribute);
        appendI32(out, record.mData.mSpecialization);
        for (float useValue : record.mData.mUseValue)
            appendFloat(out, useValue);
        appendString(out, record.mDescription);
        appendString(out, record.mName);
        appendString(out, record.mIcon);
    }

    void appendClass(const ESM::Class& record, std::string& out)
    {
        appendU32(out, record.mRecordFlags);
        appendString(out, record.mName);
        appendString(out, record.mDescription);
        for (int32_t attribute : record.mData.mAttribute)
            appendI32(out, attribute);
        appendI32(out, record.mData.mSpecialization);
        for (const auto& skillPair : record.mData.mSkills)
            for (int32_t skill : skillPair)
                appendI32(out, skill);
        appendI32(out, record.mData.mIsPlayable);
        appendI32(out, record.mData.mServices);
    }

    void appendCreature(const ESM::Creature& record, std::string& out)
    {
        appendU32(out, record.mRecordFlags);
        appendI32(out, record.mData.mType);
        appendI32(out, record.mData.mLevel);
        appendI32(out, record.mData.mSoul);
        appendI32(out, record.mData.mHealth);
        appendI32(out, record.mData.mMana);
        appendI32(out, record.mData.mFatigue);
        appendI32(out, record.mData.mCombat);
        appendI32(out, record.mData.mMagic);
        appendI32(out, record.mData.mStealth);
        appendI32(out, record.mAiData.mServices);
        appendString(out, record.mName);
        appendPath(out, record.mModel);
    }

    void appendNpc(const ESM::NPC& record, std::string& out)
    {
        appendU32(out, record.mRecordFlags);
        appendU32(out, record.mNpdtType);
        appendI32(out, record.mNpdt.mLevel);
        for (unsigned char attribute : record.mNpdt.mAttributes)
            appendI32(out, attribute);
        for (unsigned char skill : record.mNpdt.mSkills)
            appendI32(out, skill);
        appendI32(out, record.mNpdt.mHealth);
        appendI32(out, record.mNpdt.mMana);
        appendI32(out, record.mNpdt.mFatigue);
        appendI32(out, record.mNpdt.mDisposition);
        appendI32(out, record.mNpdt.mReputation);
        appendI32(out, record.mNpdt.mRank);
        appendI32(out, record.mNpdt.mGold);
        appendRefId(out, record.mRace);
        appendRefId(out, record.mClass);
        appendRefId(out, record.mFaction);
        appendI32(out, record.mAiData.mServices);
        appendU32(out, record.mFlags);
        appendString(out, record.mName);
    }

    void updateU32(mwmp::crypto::Sha256& hash, std::uint32_t value)
    {
        const std::array<std::uint8_t, 4> bytes = { static_cast<std::uint8_t>(value),
            static_cast<std::uint8_t>(value >> 8), static_cast<std::uint8_t>(value >> 16),
            static_cast<std::uint8_t>(value >> 24) };
        hash.update(bytes.data(), bytes.size());
    }

    void updateBytes(mwmp::crypto::Sha256& hash, const void* data, std::size_t size)
    {
        hash.update(static_cast<const std::uint8_t*>(data), size);
    }
}

std::string MWMP::resolvedContentFingerprint(const MWWorld::ESMStore& store)
{
    std::vector<Entry> entries;
    appendTypedRecords<ESM::Potion>(store, mwmp::records::RecordType::Potion, entries);
    appendTypedRecords<ESM::Enchantment>(store, mwmp::records::RecordType::Enchantment, entries);
    appendTypedRecords<ESM::Weapon>(store, mwmp::records::RecordType::Weapon, entries);
    appendTypedRecords<ESM::Armor>(store, mwmp::records::RecordType::Armor, entries);
    appendTypedRecords<ESM::Clothing>(store, mwmp::records::RecordType::Clothing, entries);
    appendTypedRecords<ESM::Book>(store, mwmp::records::RecordType::Book, entries);
    appendTypedRecords<ESM::Dialogue>(store, mwmp::records::RecordType::Dialogue, entries);
    appendTypedRecords<ESM::Script>(store, mwmp::records::RecordType::Script, entries);

    // These are authoritative mechanics inputs rather than runtime-create DTOs.
    // Load scripts can mutate several of them, so they must participate in the
    // post-load fingerprint used by server-authoritative crafting.
    appendMechanicsRecords<ESM::Ingredient>(store, ResolvedRecordType::Ingredient, entries, appendIngredient);
    appendMechanicsRecords<ESM::Apparatus>(store, ResolvedRecordType::Apparatus, entries, appendApparatus);
    appendMechanicsRecords<ESM::MagicEffect>(store, ResolvedRecordType::MagicEffect, entries, appendMagicEffect);
    appendMechanicsRecords<ESM::GameSetting>(store, ResolvedRecordType::GameSetting, entries, appendGameSetting);
    // Alchemy skill progression consumes the Alchemy skill record's use values
    // and the character class's major/minor skills and specialization, so both
    // participate in the authoritative crafting identity.
    appendMechanicsRecords<ESM::Skill>(store, ResolvedRecordType::Skill, entries, appendSkill);
    appendMechanicsRecords<ESM::Class>(store, ResolvedRecordType::Class, entries, appendClass);
    // Enchanting consumes the creature soul value of the soul trapped in a
    // gem (charge/cost/count/chance), the enchanter's NPC record statistics
    // (paid services), and the NPC/creature service bits, so those records
    // participate in the authoritative crafting identity too.
    appendMechanicsRecords<ESM::Creature>(store, ResolvedRecordType::Creature, entries, appendCreature);
    appendMechanicsRecords<ESM::NPC>(store, ResolvedRecordType::NPC, entries, appendNpc);

    std::sort(entries.begin(), entries.end(), [](const Entry& left, const Entry& right) {
        return std::tie(left.type, left.id) < std::tie(right.type, right.id);
    });

    mwmp::crypto::Sha256 hash;
    static constexpr std::array<std::uint8_t, 8> header = { 'O', 'M', 'R', 'C', 5, 0, 0, 0 };
    hash.update(header.data(), header.size());
    updateU32(hash, static_cast<std::uint32_t>(entries.size()));
    for (const Entry& entry : entries)
    {
        hash.update(&entry.type, 1);
        updateU32(hash, static_cast<std::uint32_t>(entry.id.size()));
        updateBytes(hash, entry.id.data(), entry.id.size());
        updateU32(hash, static_cast<std::uint32_t>(entry.definition.size()));
        updateBytes(hash, entry.definition.data(), entry.definition.size());
    }
    return hash.finish();
}
