#ifndef OPENMW_LUA_APPEARANCE_HPP
#define OPENMW_LUA_APPEARANCE_HPP

#include <components/openmw-mp/PlayerAppearance.hpp>
#include <sol/sol.hpp>
#include <stdexcept>

namespace LuaUtil
{
    inline constexpr std::array<const char*, 15> AppearancePartNames = { "head", "hair", "neck", "chest", "groin",
        "hands", "wrists", "forearms", "upperarms", "feet", "ankles", "knees", "upperlegs", "clavicles", "tail" };

    inline ESM::NpcAppearance readAppearanceBody(const sol::table& table)
    {
        ESM::NpcAppearance body;
        auto recordId = [](const std::string& value) {
            if (!mwmp::validAppearanceId(value, true))
                throw std::runtime_error("Invalid appearance body record ID");
            return ESM::RefId::deserializeText(value);
        };
        body.bodyRace = recordId(table["bodyRace"].get_or(std::string()));
        const int rig = table["rig"].get_or(0);
        if (rig < 0 || rig > 2)
            throw std::runtime_error("Invalid appearance animation rig");
        body.animationRig = static_cast<uint8_t>(rig);
        body.tail = table["tail"].get_or(true);
        const sol::optional<sol::table> parts = table["parts"];
        if (parts)
            for (size_t i = 2; i < body.parts.size(); ++i)
                body.parts[i] = recordId((*parts)[AppearancePartNames[i]].get_or(std::string()));
        return body;
    }

    inline sol::table appearanceBodyTable(const ESM::NpcAppearance& body, sol::this_state ts)
    {
        sol::state_view lua(ts);
        sol::table result = lua.create_table();
        result["bodyRace"] = body.bodyRace.serializeText();
        result["rig"] = body.animationRig;
        result["tail"] = body.tail;
        sol::table parts = lua.create_table();
        for (size_t i = 2; i < body.parts.size(); ++i)
            parts[AppearancePartNames[i]] = body.parts[i].serializeText();
        result["parts"] = parts;
        return result;
    }
}
#endif
