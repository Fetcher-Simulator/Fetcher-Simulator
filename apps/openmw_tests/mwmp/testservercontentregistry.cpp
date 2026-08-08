#include <gtest/gtest.h>

#include <components/esm3/loadalch.hpp>
#include <components/esm3/loadappa.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadingr.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/lua/configuration.hpp>
#include <components/lua/luastate.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/testing/util.hpp>
#include <components/toutf8/toutf8.hpp>

#include <apps/openmw-server/ServerContentRegistry.hpp>
#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwlua/contentbindings.hpp>
#include <apps/openmw/mwlua/context.hpp>
#include <apps/openmw/mwmp/records/ResolvedContentFingerprint.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>

namespace
{
    ESM::Potion makePotion(std::string_view id, float weight)
    {
        ESM::Potion potion;
        potion.mId = ESM::RefId::stringRefId(id);
        potion.mName = "Content potion";
        potion.mModel = "m\\misc_potion_standard_01.nif";
        potion.mIcon = "m\\tx_potion_standard_01.dds";
        potion.mData.mWeight = weight;
        potion.mData.mValue = 10;
        return potion;
    }

    ESM::Ingredient makeIngredient(float weight)
    {
        ESM::Ingredient ingredient;
        ingredient.blank();
        ingredient.mId = ESM::RefId::stringRefId("content_ingredient");
        ingredient.mName = "Content ingredient";
        ingredient.mData.mWeight = weight;
        ingredient.mData.mValue = 10;
        ingredient.mData.mEffectID[0] = ESM::MagicEffect::FireDamage;
        return ingredient;
    }

    ESM::Apparatus makeApparatus(float quality)
    {
        ESM::Apparatus apparatus;
        apparatus.blank();
        apparatus.mId = ESM::RefId::stringRefId("content_apparatus");
        apparatus.mName = "Content apparatus";
        apparatus.mData.mType = ESM::Apparatus::MortarPestle;
        apparatus.mData.mQuality = quality;
        return apparatus;
    }

    ESM::MagicEffect makeMagicEffect(float baseCost)
    {
        ESM::MagicEffect effect;
        effect.blank();
        effect.mId = ESM::MagicEffect::FireDamage;
        effect.mData.mBaseCost = baseCost;
        effect.mName = "Fire Damage";
        return effect;
    }

    ESM::GameSetting makeGameSetting(float value)
    {
        ESM::GameSetting setting;
        setting.blank();
        setting.mId = ESM::RefId::stringRefId("fPotionT1MagMult");
        setting.mValue.setType(ESM::VT_Float);
        setting.mValue.setFloat(value);
        return setting;
    }

    std::string runLoadScript(float templateWeight, std::string script)
    {
        MWBase::Environment environment;
        MWWorld::ESMStore store;
        environment.setESMStore(store);
        store.insertStatic(makePotion("template_potion", templateWeight));

        TestingOpenMW::VFSTestFile scriptFile(std::move(script));
        auto vfs = TestingOpenMW::createTestVFS(
            { { VFS::Path::NormalizedView("scripts/content_test.lua"), &scriptFile } });
        ToUTF8::StatelessUtf8Encoder encoder(ToUTF8::WINDOWS_1252);
        Resource::ResourceSystem resources(vfs.get(), 0.0, &encoder);
        environment.setResourceSystem(resources);
        LuaUtil::ScriptsConfiguration configuration;
        LuaUtil::LuaState::disableProfiler();
        LuaUtil::LuaState lua(vfs.get(), &configuration);
        MWLua::Context context;
        context.mType = MWLua::Context::Load;
        context.mLua = &lua;
        lua.protectedCall([&](LuaUtil::LuaView&) {
            lua.addCommonPackage("openmw.content", MWLua::initContentPackage(context));
        });
        LuaUtil::LuaState::throwIfError(
            lua.runInNewSandbox(VFS::Path::Normalized("scripts/content_test.lua"), "ContentTest"));
        return MWMP::resolvedContentFingerprint(store);
    }
}

TEST(ServerContentRegistry, identicalResolvedContentHasIdenticalFingerprint)
{
    MWWorld::ESMStore first;
    MWWorld::ESMStore second;
    first.insertStatic(makePotion("potion_b", 2.f));
    first.insertStatic(makePotion("potion_a", 1.f));
    second.insertStatic(makePotion("potion_a", 1.f));
    second.insertStatic(makePotion("potion_b", 2.f));
    EXPECT_EQ(MWMP::resolvedContentFingerprint(first), MWMP::resolvedContentFingerprint(second));
}

TEST(ServerContentRegistry, orderedPluginManifestDetectsOrderChanges)
{
    using Entry = mwmp::ServerContentRegistry::ManifestEntry;
    const std::vector<Entry> first{ { "base.esm", "01" }, { "override.omwaddon", "02" } };
    const std::vector<Entry> second{ { "override.omwaddon", "02" }, { "base.esm", "01" } };
    EXPECT_NE(mwmp::ServerContentRegistry::orderedManifestFingerprint(first),
        mwmp::ServerContentRegistry::orderedManifestFingerprint(second));
}

TEST(ServerContentRegistry, changedLuaCreatedRecordChangesResolvedFingerprint)
{
    const std::string first = runLoadScript(1.f,
        "local c=require('openmw.content'); "
        "c.potions.records.lua_potion={template=c.potions.records.template_potion,name='First'}");
    const std::string second = runLoadScript(1.f,
        "local c=require('openmw.content'); "
        "c.potions.records.lua_potion={template=c.potions.records.template_potion,name='Second'}");
    EXPECT_NE(first, second);
}

TEST(ServerContentRegistry, changedTemplateInputChangesResolvedFingerprint)
{
    const std::string script =
        "local c=require('openmw.content'); "
        "c.potions.records.lua_potion={template=c.potions.records.template_potion,name='Derived'}";
    EXPECT_NE(runLoadScript(1.f, script), runLoadScript(2.f, script));
}

TEST(ServerContentRegistry, authoritativeCraftingInputsChangeResolvedFingerprint)
{
    {
        MWWorld::ESMStore first;
        MWWorld::ESMStore second;
        first.insertStatic(makeIngredient(1.f));
        second.insertStatic(makeIngredient(2.f));
        EXPECT_NE(MWMP::resolvedContentFingerprint(first), MWMP::resolvedContentFingerprint(second));
    }
    {
        MWWorld::ESMStore first;
        MWWorld::ESMStore second;
        first.insertStatic(makeApparatus(1.f));
        second.insertStatic(makeApparatus(2.f));
        EXPECT_NE(MWMP::resolvedContentFingerprint(first), MWMP::resolvedContentFingerprint(second));
    }
    {
        MWWorld::ESMStore first;
        MWWorld::ESMStore second;
        first.insertStatic(makeMagicEffect(1.f));
        second.insertStatic(makeMagicEffect(2.f));
        EXPECT_NE(MWMP::resolvedContentFingerprint(first), MWMP::resolvedContentFingerprint(second));
    }
    {
        MWWorld::ESMStore first;
        MWWorld::ESMStore second;
        first.insertStatic(makeGameSetting(1.f));
        second.insertStatic(makeGameSetting(2.f));
        EXPECT_NE(MWMP::resolvedContentFingerprint(first), MWMP::resolvedContentFingerprint(second));
    }
}

TEST(ServerContentRegistry, enchantingInputsChangeResolvedFingerprint)
{
    auto makeCreature = [](int soul) {
        ESM::Creature creature;
        creature.blank();
        creature.mId = ESM::RefId::stringRefId("content_soul");
        creature.mName = "Content soul";
        creature.mData.mSoul = soul;
        return creature;
    };
    auto makeNpc = [](int services, int disposition) {
        ESM::NPC npc;
        npc.blank();
        npc.mId = ESM::RefId::stringRefId("content_enchanter");
        npc.mName = "Content enchanter";
        npc.mNpdt.mDisposition = disposition;
        npc.mAiData.mServices = services;
        return npc;
    };
    {
        // The soul value stored in a Creature record determines the
        // enchantment charge, cost, and success chance.
        MWWorld::ESMStore first;
        MWWorld::ESMStore second;
        first.insertStatic(makeCreature(100));
        second.insertStatic(makeCreature(200));
        EXPECT_NE(MWMP::resolvedContentFingerprint(first), MWMP::resolvedContentFingerprint(second));
    }
    {
        // The paid enchanter's NPC record drives service availability and
        // the authoritative barter price.
        MWWorld::ESMStore first;
        MWWorld::ESMStore second;
        first.insertStatic(makeNpc(ESM::NPC::Enchanting, 50));
        second.insertStatic(makeNpc(0, 50));
        EXPECT_NE(MWMP::resolvedContentFingerprint(first), MWMP::resolvedContentFingerprint(second));
        MWWorld::ESMStore third;
        third.insertStatic(makeNpc(ESM::NPC::Enchanting, 80));
        EXPECT_NE(MWMP::resolvedContentFingerprint(first), MWMP::resolvedContentFingerprint(third));
    }
}

TEST(ServerContentRegistry, playerNpcPromotionDoesNotChangeResolvedFingerprint)
{
    ESM::NPC player;
    player.blank();
    player.mId = ESM::RefId::stringRefId("Player");
    player.mName = "Player";
    player.mNpdt.mLevel = 7;
    player.mNpdt.mGold = 123;

    MWWorld::ESMStore headless;
    MWWorld::ESMStore client;
    headless.insertStatic(player);
    client.insertStatic(player);

    // Normal client World::loadData() performs this promotion, while the
    // headless ServerContentRegistry uses initializeGameplayState=false.
    client.movePlayerRecord();

    EXPECT_EQ(MWMP::resolvedContentFingerprint(headless), MWMP::resolvedContentFingerprint(client));
}

TEST(ServerContentRegistry, invalidLoadScriptIsRejected)
{
    EXPECT_THROW(runLoadScript(1.f, "error('deterministic content failure')"), std::runtime_error);
}

TEST(ServerContentRegistry, runtimeDynamicRecordsDoNotChangeContentFingerprint)
{
    MWWorld::ESMStore store;
    store.insertStatic(makePotion("base_potion", 1.f));
    const std::string before = MWMP::resolvedContentFingerprint(store);

    ESM::Potion runtime = makePotion("ignored", 99.f);
    runtime.mId = ESM::RefId::stringRefId("$custom_potion_1");
    store.overrideRecord(runtime);
    EXPECT_EQ(before, MWMP::resolvedContentFingerprint(store));
}
