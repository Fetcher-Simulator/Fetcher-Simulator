#include <apps/openmw/mwmp/records/TypedDynamicRecordInstaller.hpp>
#include <apps/openmw/mwscript/globalscripts.hpp>
#include <apps/openmw/mwscript/scriptmanagerimp.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>

#include <components/compiler/extensions.hpp>
#include <components/compiler/extensions0.hpp>
#include <components/esm3/loaddial.hpp>
#include <components/esm3/loadscpt.hpp>

#include <gtest/gtest.h>

#include "../mwscript/testutils.hpp"

namespace
{
    ESM::Script makeScript(std::string_view id, std::string source)
    {
        ESM::Script script;
        script.blank();
        script.mId = ESM::RefId::stringRefId(id);
        script.mScriptText = std::move(source);
        return script;
    }
}

TEST(TypedDynamicRecordInstaller, ScriptOverrideInvalidatesCacheWithoutStartingOrStoppingGlobalScripts)
{
    const ESM::RefId scriptId = ESM::RefId::stringRefId("runtime_cache_test");

    MWWorld::ESMStore store;
    store.insertStatic(
        makeScript("runtime_cache_test", "Begin runtime_cache_test\nshort source_a\nEnd runtime_cache_test\n"));
    store.setUp();

    Compiler::Extensions extensions;
    Compiler::registerExtensions(extensions);
    TestCompilerContext compilerContext;
    compilerContext.setExtensions(&extensions);
    MWScript::ScriptManager scriptManager(store, compilerContext, 1);

    ASSERT_TRUE(scriptManager.compile(scriptId));
    ASSERT_TRUE(scriptManager.getLocals(scriptId).search('s', "source_a"));
    ASSERT_FALSE(scriptManager.getGlobalScripts().isRunning(scriptId));
    const std::size_t globalScriptCount = scriptManager.getGlobalScripts().getScripts().size();

    mwmp::records::Script overrideScript;
    overrideScript.sourceText = "Begin runtime_cache_test\nshort source_b\nEnd runtime_cache_test\n";
    mwmp::records::DynamicRecordDefinition definition;
    definition.authoringMode = mwmp::records::AuthoringMode::Override;
    definition.data = std::move(overrideScript);

    mwmp::installTypedDynamicRecord(
        store, "runtime_cache_test", definition, false, [&](const ESM::RefId& id) { scriptManager.invalidate(id); });

    EXPECT_EQ(store.get<ESM::Script>().search(scriptId)->mScriptText,
        "Begin runtime_cache_test\nshort source_b\nEnd runtime_cache_test\n");
    const Compiler::Locals& refreshed = scriptManager.getLocals(scriptId);
    EXPECT_FALSE(refreshed.search('s', "source_a"));
    EXPECT_TRUE(refreshed.search('s', "source_b"));
    EXPECT_FALSE(scriptManager.getGlobalScripts().isRunning(scriptId));
    EXPECT_EQ(scriptManager.getGlobalScripts().getScripts().size(), globalScriptCount);
}

TEST(TypedDynamicRecordInstaller, ExplicitOverrideModeControlsStaticDialogueOverlay)
{
    const ESM::RefId dialogueId = ESM::RefId::stringRefId("durable_override_dialogue");
    MWWorld::ESMStore store;
    ESM::Dialogue baseline;
    baseline.blank();
    baseline.mId = dialogueId;
    baseline.mStringId = "Static baseline";
    baseline.mType = ESM::Dialogue::Journal;
    store.insertStatic(baseline);
    store.setUp();

    mwmp::records::Dialogue runtimeDialogue;
    runtimeDialogue.stringId = "Authoritative override";
    runtimeDialogue.type = ESM::Dialogue::Journal;
    mwmp::records::DynamicRecordDefinition definition;
    definition.authoringMode = mwmp::records::AuthoringMode::Override;
    definition.data = runtimeDialogue;

    mwmp::installTypedDynamicRecord(store, "durable_override_dialogue", definition, false, {});
    EXPECT_EQ(store.get<ESM::Dialogue>().search(dialogueId)->mStringId, "Authoritative override");
    EXPECT_EQ(store.get<ESM::Dialogue>().searchStatic(dialogueId)->mStringId, "Static baseline");

    definition.authoringMode = mwmp::records::AuthoringMode::New;
    EXPECT_THROW(
        mwmp::installTypedDynamicRecord(store, "durable_override_dialogue", definition, false, {}), std::runtime_error);
}
