#ifndef MWLUA_SETTINGSSTORAGE_H
#define MWLUA_SETTINGSSTORAGE_H

#include <string_view>
#include <vector>

#include <components/lua/storage.hpp>

namespace MWLua
{
    // Selects only values owned by groups registered through OpenMW's settings
    // framework. The returned values form the local side of multiplayer global
    // storage's authority boundary.
    std::vector<LuaUtil::LuaStorage::SerializedValue> selectRegisteredSettingValues(lua_State* state,
        const std::vector<LuaUtil::LuaStorage::SerializedValue>& values,
        const LuaUtil::UserdataSerializer* serializer = nullptr);

    bool shouldPersistClientGlobalSetting(std::string_view section) noexcept;
    bool shouldSaveClientGlobalStorage(bool globalScriptsStarted, bool mirroredFromServer) noexcept;
}

#endif // MWLUA_SETTINGSSTORAGE_H
