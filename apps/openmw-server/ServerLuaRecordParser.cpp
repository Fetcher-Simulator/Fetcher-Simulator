#include "ServerLuaRecordParser.hpp"

#include <stdexcept>
#include <string>

#include <sol/sol.hpp>

#include <components/lua/serialization.hpp>
#include <components/esm/attr.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadskil.hpp>
#include <components/openmw-mp/Records/EsmDynamicRecordConversion.hpp>

#include <apps/openmw/mwlua/magictypebindings.hpp>
#include <apps/openmw/mwlua/types/types.hpp>

namespace
{
    sol::table deserializeTable(sol::state& lua, std::string_view bytes)
    {
        const sol::object value = LuaUtil::deserialize(lua.lua_state(), bytes);
        if (!value.is<sol::table>())
            throw std::runtime_error("Server-Lua dynamic record payload is not a table");
        return value.as<sol::table>();
    }

    template <class Definition>
    Definition restoreTextReferences(Definition definition, const sol::table& table)
    {
        if constexpr (requires { definition.item; })
        {
            if (const auto script = table.get<sol::optional<std::string>>("mwscript"))
                definition.item.scriptId = *script;
        }
        if constexpr (requires { definition.enchantment; })
        {
            if (const auto enchantment = table.get<sol::optional<std::string>>("enchant"))
                definition.enchantment = { mwmp::records::ReferenceKind::ContentId, *enchantment };
        }
        return definition;
    }

    void normalizeLegacyEffects(sol::table table)
    {
        const sol::object effectsObject = table["effects"];
        if (!effectsObject.is<sol::table>())
            return;
        sol::table effects = effectsObject.as<sol::table>();
        for (std::size_t i = 1; i <= effects.size(); ++i)
        {
            const sol::object value = effects[i];
            if (!value.is<sol::table>())
                continue;
            sol::table effect = value.as<sol::table>();
            const sol::object id = effect["id"];
            if (id.is<int>())
                effect["id"] = ESM::MagicEffect::indexToRefId(id.as<int>()).serializeText();
            const sol::object skill = effect["affectedSkill"];
            if (skill.is<int>())
                effect["affectedSkill"] = ESM::Skill::indexToRefId(skill.as<int>()).serializeText();
            const sol::object attribute = effect["affectedAttribute"];
            if (attribute.is<int>())
                effect["affectedAttribute"] = ESM::Attribute::indexToRefId(attribute.as<int>()).serializeText();
            if (effect["range"] == sol::nil && effect["rangeType"] != sol::nil)
                effect["range"] = effect["rangeType"];
        }
    }
}

bool mwmp::isCanonicalServerLuaRecordType(std::string_view recordType)
{
    return recordType == "potion" || recordType == "enchantment" || recordType == "weapon"
        || recordType == "armor" || recordType == "clothing" || recordType == "book";
}

mwmp::records::DynamicRecordDefinition mwmp::parseServerLuaRecord(
    std::string_view recordType, std::string_view serializedTable)
{
    if (!isCanonicalServerLuaRecordType(recordType))
        throw std::runtime_error("Unsupported canonical server-Lua dynamic record type: "
            + std::string(recordType));

    sol::state lua;
    const sol::table table = deserializeTable(lua, serializedTable);
    normalizeLegacyEffects(table);
    if (recordType == "potion")
        return { records::CurrentSchemaVersion,
            restoreTextReferences(
                std::get<records::Potion>(records::fromEsmRecord(MWLua::tableToPotion(table)).data), table) };
    if (recordType == "enchantment")
        return records::fromEsmRecord(MWLua::tableToEnchantment(table));
    if (recordType == "weapon")
        return { records::CurrentSchemaVersion,
            restoreTextReferences(
                std::get<records::Weapon>(records::fromEsmRecord(MWLua::tableToWeapon(table)).data), table) };
    if (recordType == "armor")
        return { records::CurrentSchemaVersion,
            restoreTextReferences(
                std::get<records::Armor>(records::fromEsmRecord(MWLua::tableToArmor(table)).data), table) };
    if (recordType == "clothing")
        return { records::CurrentSchemaVersion,
            restoreTextReferences(
                std::get<records::Clothing>(records::fromEsmRecord(MWLua::tableToClothing(table)).data), table) };
    return { records::CurrentSchemaVersion,
        restoreTextReferences(
            std::get<records::Book>(records::fromEsmRecord(MWLua::tableToBook(table)).data), table) };
}
