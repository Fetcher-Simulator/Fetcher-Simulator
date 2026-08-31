#include "settingsstorage.hpp"

#include <map>
#include <string>
#include <string_view>

#include <components/lua/serialization.hpp>

namespace MWLua
{
    namespace
    {
        constexpr std::string_view sGroupSection = "OmwSettingGroups";
        constexpr std::string_view sArgumentSectionPostfix = "Arguments";

        using SerializedValue = LuaUtil::LuaStorage::SerializedValue;
        using ValueIndex
            = std::map<std::string, std::map<std::string, const SerializedValue*, std::less<>>, std::less<>>;

        void addCurrentOrDeclared(std::vector<SerializedValue>& result, const ValueIndex& index,
            std::string_view section, std::string_view key, const sol::object& declaredValue,
            const LuaUtil::UserdataSerializer* serializer)
        {
            if (const auto sectionIt = index.find(section); sectionIt != index.end())
            {
                if (const auto valueIt = sectionIt->second.find(key); valueIt != sectionIt->second.end())
                {
                    result.push_back(*valueIt->second);
                    return;
                }
            }
            if (declaredValue != sol::nil)
                result.push_back(
                    { std::string(section), std::string(key), LuaUtil::serialize(declaredValue, serializer) });
        }
    }

    std::vector<LuaUtil::LuaStorage::SerializedValue> selectRegisteredSettingValues(lua_State* state,
        const std::vector<LuaUtil::LuaStorage::SerializedValue>& values, const LuaUtil::UserdataSerializer* serializer)
    {
        ValueIndex index;
        for (const SerializedValue& value : values)
            index[value.mSection][value.mKey] = &value;

        std::vector<SerializedValue> result;
        const auto groups = index.find(sGroupSection);
        if (groups == index.end())
            return result;

        for (const auto& [registeredGroupKey, serializedGroup] : groups->second)
        {
            sol::object groupObject = LuaUtil::deserialize(state, serializedGroup->mValue, serializer);
            if (groupObject.get_type() != sol::type::table)
                continue;
            sol::table group = groupObject.as<sol::table>();
            sol::optional<std::string> groupKey = group["key"];
            sol::optional<sol::table> settings = group["settings"];
            if (!groupKey || *groupKey != registeredGroupKey || !settings)
                continue;

            result.push_back(*serializedGroup);
            const std::string argumentSection = *groupKey + std::string(sArgumentSectionPostfix);
            for (const auto& [settingKeyObject, settingObject] : *settings)
            {
                if (settingKeyObject.get_type() != sol::type::string || settingObject.get_type() != sol::type::table)
                    continue;
                const std::string settingKey = settingKeyObject.as<std::string>();
                sol::table setting = settingObject.as<sol::table>();
                sol::object declaredDefault = setting["default"];
                sol::object declaredArgument = setting["argument"];
                addCurrentOrDeclared(result, index, *groupKey, settingKey, declaredDefault, serializer);
                addCurrentOrDeclared(result, index, argumentSection, settingKey, declaredArgument, serializer);
            }
        }
        return result;
    }

    bool shouldPersistClientGlobalSetting(std::string_view section) noexcept
    {
        return section != sGroupSection;
    }

    bool shouldSaveClientGlobalStorage(bool globalScriptsStarted, bool mirroredFromServer) noexcept
    {
        return globalScriptsStarted && !mirroredFromServer;
    }
}
