#include <gtest/gtest.h>

#include <sol/sol.hpp>

#include <components/esm3/loadclot.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/lua/serialization.hpp>
#include <components/openmw-mp/Records/DynamicRecordCodec.hpp>
#include <components/openmw-mp/Records/DynamicRecordFingerprint.hpp>
#include <components/openmw-mp/Records/DynamicRecordValidation.hpp>
#include <components/openmw-mp/Records/EsmDynamicRecordConversion.hpp>

#include <apps/openmw-server/ServerLuaRecordParser.hpp>
#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>

namespace
{
    LuaUtil::BinaryData legacyClothingPayload(
        std::string_view name, std::string_view enchantment = "server_enchantment")
    {
        sol::state lua;
        sol::table value = lua.create_table();
        value["baseId"] = "base_amulet";
        value["name"] = name;
        value["enchant"] = enchantment;
        return LuaUtil::serialize(value);
    }

    LuaUtil::BinaryData legacyEnchantmentPayload()
    {
        sol::state lua;
        sol::table value = lua.create_table();
        value["type"] = 3;
        value["cost"] = 1;
        value["charge"] = 1;
        sol::table effects = lua.create_table();
        sol::table effect = lua.create_table();
        effect["id"] = 2;
        effect["rangeType"] = 0;
        effect["duration"] = 1;
        effect["magnitudeMin"] = 1;
        effect["magnitudeMax"] = 1;
        effects[1] = effect;
        value["effects"] = effects;
        return LuaUtil::serialize(value);
    }

    LuaUtil::BinaryData dialoguePayload()
    {
        sol::state lua;
        sol::table value = lua.create_table();
        value["stringId"] = "Runtime Quest";
        value["type"] = 4;
        sol::table infos = lua.create_table();
        sol::table info = lua.create_table();
        info["id"] = "runtime_quest_10";
        info["type"] = 4;
        info["index"] = 10;
        info["response"] = "The definition was installed before journal restore.";
        info["resultScript"] = "set runtime_result to 1";
        sol::table conditions = lua.create_table();
        sol::table condition = lua.create_table();
        condition["variable"] = "runtime_dependency";
        condition["value"] = 10;
        condition["index"] = 0;
        condition["function"] = 76;
        condition["comparison"] = "3";
        conditions[1] = condition;
        info["conditions"] = conditions;
        infos[1] = info;
        value["infos"] = infos;
        sol::table dependencies = lua.create_table();
        dependencies[1] = "runtime_script";
        value["dependencies"] = dependencies;
        return LuaUtil::serialize(value);
    }

    LuaUtil::BinaryData scriptPayload()
    {
        sol::state lua;
        sol::table value = lua.create_table();
        value["recordFlags"] = 32;
        value["source"] = "Begin runtime_script\r\nshort state\r\nEnd runtime_script\r\n";
        return LuaUtil::serialize(value);
    }
}

TEST(ServerLuaRecordParser, ConvertsNumericLegacyMagicEffectIds)
{
    MWBase::Environment environment;
    MWWorld::ESMStore store;
    environment.setESMStore(store);
    ESM::MagicEffect effect;
    effect.blank();
    effect.mId = ESM::MagicEffect::indexToRefId(2);
    store.insertStatic(effect);

    const auto definition = mwmp::parseServerLuaRecord("enchantment", legacyEnchantmentPayload());
    const auto& enchantment = std::get<mwmp::records::Enchantment>(definition.data);
    ASSERT_EQ(enchantment.effects.size(), 1u);
    EXPECT_EQ(enchantment.effects.front().effectId, ESM::MagicEffect::indexToRefId(2).serializeText());
    EXPECT_EQ(enchantment.effects.front().range, 0);
}

TEST(ServerLuaRecordParser, ConvertsLegacyTableToCanonicalTypedDefinition)
{
    MWBase::Environment environment;
    MWWorld::ESMStore store;
    environment.setESMStore(store);

    ESM::Clothing base;
    base.blank();
    base.mId = ESM::RefId::stringRefId("base_amulet");
    base.mName = "Base Amulet";
    base.mModel = "m\\amulet.nif";
    base.mIcon = "m\\amulet.dds";
    base.mData.mType = ESM::Clothing::Amulet;
    base.mData.mWeight = 1.f;
    base.mData.mValue = 100;
    store.insertStatic(base);

    const auto first = mwmp::parseServerLuaRecord("clothing", legacyClothingPayload("Canonical Amulet"));
    const auto second = mwmp::parseServerLuaRecord("clothing", legacyClothingPayload("Canonical Amulet"));
    EXPECT_EQ(mwmp::records::fingerprint(first), mwmp::records::fingerprint(second));
    const auto& clothing = std::get<mwmp::records::Clothing>(first.data);
    EXPECT_EQ(clothing.item.name, "Canonical Amulet");
    EXPECT_EQ(clothing.item.model, "m\\amulet.nif");
    EXPECT_EQ(clothing.enchantment.kind, mwmp::records::ReferenceKind::ContentId);
    EXPECT_EQ(clothing.enchantment.value, "server_enchantment");

    const auto cleared = mwmp::parseServerLuaRecord(
        "clothing", legacyClothingPayload("Canonical Amulet", ""),
        mwmp::records::AuthoringMode::Override);
    const auto& clearedClothing = std::get<mwmp::records::Clothing>(cleared.data);
    EXPECT_EQ(clearedClothing.enchantment.kind, mwmp::records::ReferenceKind::None);
    EXPECT_TRUE(clearedClothing.enchantment.value.empty());
    EXPECT_EQ(clearedClothing.item.model, "m\\amulet.nif");

    const std::string encoded = mwmp::records::encodeDefinition(first);
    EXPECT_EQ(mwmp::records::decodeDefinition(encoded), first);
}

TEST(ServerLuaRecordParser, RejectsUnsupportedAndMalformedLegacyPayloads)
{
    EXPECT_FALSE(mwmp::isCanonicalServerLuaRecordType("npc"));
    EXPECT_THROW(mwmp::parseServerLuaRecord("npc", legacyClothingPayload("NPC")), std::runtime_error);
    EXPECT_THROW(mwmp::parseServerLuaRecord("clothing", "not-lua-serialization"), std::runtime_error);
}

TEST(ServerLuaRecordParser, ParsesTypedDialogueAndSourceOnlyScriptDefinitions)
{
    const auto dialogueDefinition = mwmp::records::canonicalize(mwmp::parseServerLuaRecord(
        "dialogue", dialoguePayload(), mwmp::records::AuthoringMode::Override));
    EXPECT_EQ(dialogueDefinition.authoringMode, mwmp::records::AuthoringMode::Override);
    const auto& dialogue = std::get<mwmp::records::Dialogue>(dialogueDefinition.data);
    ASSERT_EQ(dialogue.infos.size(), 1u);
    EXPECT_EQ(dialogue.infos.front().infoId, "runtime_quest_10");
    EXPECT_EQ(dialogue.infos.front().dispositionOrJournalIndex, 10);
    ASSERT_EQ(dialogue.infos.front().conditions.size(), 1u);
    EXPECT_EQ(dialogue.declaredDependencies, (std::vector<std::string>{ "runtime_script" }));

    const auto scriptDefinition = mwmp::records::canonicalize(mwmp::parseServerLuaRecord(
        "script", scriptPayload(), mwmp::records::AuthoringMode::New));
    EXPECT_EQ(scriptDefinition.authoringMode, mwmp::records::AuthoringMode::New);
    const auto& script = std::get<mwmp::records::Script>(scriptDefinition.data);
    EXPECT_EQ(script.recordFlags, 32u);
    EXPECT_EQ(script.sourceText, "Begin runtime_script\nshort state\nEnd runtime_script\n");
    const auto& esm = std::get<ESM::Script>(mwmp::records::toEsmRecord(scriptDefinition));
    EXPECT_TRUE(esm.mScriptData.empty());
    EXPECT_TRUE(esm.mVarNames.empty());
    EXPECT_EQ(esm.mNumShorts, 0u);
}
