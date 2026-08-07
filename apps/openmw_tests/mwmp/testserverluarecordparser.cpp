#include <gtest/gtest.h>

#include <sol/sol.hpp>

#include <components/esm3/loadclot.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/lua/serialization.hpp>
#include <components/openmw-mp/Records/DynamicRecordCodec.hpp>
#include <components/openmw-mp/Records/DynamicRecordFingerprint.hpp>

#include <apps/openmw-server/ServerLuaRecordParser.hpp>
#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>

namespace
{
    LuaUtil::BinaryData legacyClothingPayload(std::string_view name)
    {
        sol::state lua;
        sol::table value = lua.create_table();
        value["baseId"] = "base_amulet";
        value["name"] = name;
        value["enchant"] = "server_enchantment";
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

    const std::string encoded = mwmp::records::encodeDefinition(first);
    EXPECT_EQ(mwmp::records::decodeDefinition(encoded), first);
}

TEST(ServerLuaRecordParser, RejectsUnsupportedAndMalformedLegacyPayloads)
{
    EXPECT_FALSE(mwmp::isCanonicalServerLuaRecordType("npc"));
    EXPECT_THROW(mwmp::parseServerLuaRecord("npc", legacyClothingPayload("NPC")), std::runtime_error);
    EXPECT_THROW(mwmp::parseServerLuaRecord("clothing", "not-lua-serialization"), std::runtime_error);
}
