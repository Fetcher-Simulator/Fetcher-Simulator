#include <gtest/gtest.h>

#include <components/lua/storage.hpp>

#include <apps/openmw/mwlua/settingsstorage.hpp>

namespace
{
    template <typename T>
    T get(sol::state_view& lua, std::string luaCode)
    {
        return lua.safe_script("return " + luaCode).get<T>();
    }

    TEST(SettingsStorageTest, SelectsRealRegisteredSettingsShapeOnly)
    {
        LuaUtil::LuaState luaState{ nullptr, nullptr };
        luaState.protectedCall([](LuaUtil::LuaView& view) {
            LuaUtil::LuaStorage::initLuaBindings(view);
            auto& lua = view.sol();
            LuaUtil::LuaStorage storage;
            storage.setActive(true);
            lua["groups"] = storage.getMutableSection(lua, "OmwSettingGroups");
            lua["settings"] = storage.getMutableSection(lua, "SettingsExample");
            lua["arguments"] = storage.getMutableSection(lua, "SettingsExampleArguments");
            lua["unrelated"] = storage.getMutableSection(lua, "SettingsNotRegistered");
            lua.safe_script(R"(
                groups:set('SettingsExample', {
                    key = 'SettingsExample',
                    page = 'Example',
                    permanentStorage = true,
                    settings = {
                        chance = { key = 'chance', default = 100, argument = { min = 0 } },
                        enabled = { key = 'enabled', default = true },
                        declaredOnly = { key = 'declaredOnly', default = 12 },
                    },
                })
                settings:set('chance', 80)
                settings:set('enabled', true)
                arguments:set('chance', { min = 5 })
                unrelated:set('stale', 99)
            )");

            LuaUtil::LuaStorage::Overlay overlay;
            overlay.updateFallback(
                MWLua::selectRegisteredSettingValues(lua.lua_state(), storage.getSerializedValues()));
            overlay.applySnapshot(storage, lua.lua_state(), {});

            EXPECT_EQ(get<int>(lua, "settings:get('chance')"), 80);
            EXPECT_TRUE(get<bool>(lua, "settings:get('enabled')"));
            EXPECT_EQ(get<int>(lua, "settings:get('declaredOnly')"), 12);
            EXPECT_EQ(get<int>(lua, "arguments:get('chance').min"), 5);
            EXPECT_TRUE(get<bool>(lua, "unrelated:get('stale') == nil"));
            EXPECT_TRUE(get<bool>(lua, "groups:get('SettingsExample') ~= nil"));
        });
    }

    TEST(SettingsStorageTest, RegistrationMetadataIsRuntimeFallbackOnly)
    {
        EXPECT_FALSE(MWLua::shouldPersistClientGlobalSetting("OmwSettingGroups"));
        EXPECT_TRUE(MWLua::shouldPersistClientGlobalSetting("SettingsExample"));
        EXPECT_TRUE(MWLua::shouldPersistClientGlobalSetting("SettingsExampleArguments"));
    }

    TEST(SettingsStorageTest, MirroredStorageIsNeverSavedToClientFile)
    {
        EXPECT_TRUE(MWLua::shouldSaveClientGlobalStorage(true, false));
        EXPECT_FALSE(MWLua::shouldSaveClientGlobalStorage(true, true));
        EXPECT_FALSE(MWLua::shouldSaveClientGlobalStorage(false, false));
    }
}
