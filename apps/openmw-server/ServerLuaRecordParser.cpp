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

    mwmp::records::DialogueCondition parseDialogueCondition(const sol::table& table)
    {
        mwmp::records::DialogueCondition condition;
        condition.variable = table.get_or("variable", std::string{});
        const sol::object value = table["value"];
        if (value.is<int>())
            condition.value = static_cast<std::int32_t>(value.as<int>());
        else if (value.is<double>())
            condition.value = static_cast<float>(value.as<double>());
        else
            throw std::runtime_error("Dialogue condition requires a numeric value");
        condition.index = static_cast<std::uint8_t>(table.get_or("index", 0));
        condition.function = static_cast<std::int8_t>(table.get_or("function", 0));
        const std::string comparison = table.get_or("comparison", std::string("0"));
        if (comparison.size() != 1)
            throw std::runtime_error("Dialogue condition comparison must be one character");
        condition.comparison = comparison.front();
        return condition;
    }

    mwmp::records::DialogueInfo parseDialogueInfo(const sol::table& table)
    {
        mwmp::records::DialogueInfo info;
        info.infoId = table.get_or("id", table.get_or("infoId", std::string{}));
        info.dialogueType = table.get_or("dialogueType", table.get_or("type", 0));
        info.dispositionOrJournalIndex
            = table.get_or("dispositionOrJournalIndex", table.get_or("index", 0));
        info.rank = static_cast<std::int8_t>(table.get_or("rank", -1));
        info.gender = static_cast<std::int8_t>(table.get_or("gender", -1));
        info.pcRank = static_cast<std::int8_t>(table.get_or("pcRank", -1));
        if (const sol::object value = table["conditions"]; value.is<sol::table>())
        {
            const sol::table conditions = value.as<sol::table>();
            for (std::size_t i = 1; i <= conditions.size(); ++i)
            {
                const sol::object entry = conditions[i];
                if (!entry.is<sol::table>())
                    throw std::runtime_error("Dialogue conditions must be tables");
                info.conditions.push_back(parseDialogueCondition(entry.as<sol::table>()));
            }
        }
        info.actorId = table.get_or("actorId", std::string{});
        info.raceId = table.get_or("raceId", std::string{});
        info.classId = table.get_or("classId", std::string{});
        info.factionId = table.get_or("factionId", std::string{});
        info.pcFactionId = table.get_or("pcFactionId", std::string{});
        info.cellId = table.get_or("cellId", std::string{});
        info.sound = table.get_or("sound", std::string{});
        info.response = table.get_or("response", std::string{});
        info.resultScript = table.get_or("resultScript", std::string{});
        info.factionLess = table.get_or("factionLess", false);
        info.questStatus = static_cast<std::int8_t>(table.get_or("questStatus", 0));
        return info;
    }

    mwmp::records::Dialogue parseDialogue(const sol::table& table)
    {
        mwmp::records::Dialogue dialogue;
        dialogue.stringId = table.get_or("stringId", std::string{});
        dialogue.type = static_cast<std::int8_t>(table.get_or("type", 0));
        const sol::object infosObject = table["infos"];
        if (!infosObject.is<sol::table>())
            throw std::runtime_error("Dialogue definition requires an infos array");
        const sol::table infos = infosObject.as<sol::table>();
        for (std::size_t i = 1; i <= infos.size(); ++i)
        {
            const sol::object entry = infos[i];
            if (!entry.is<sol::table>())
                throw std::runtime_error("Dialogue INFO definitions must be tables");
            dialogue.infos.push_back(parseDialogueInfo(entry.as<sol::table>()));
        }
        if (const sol::object value = table["dependencies"]; value.is<sol::table>())
        {
            for (const auto& entry : value.as<sol::table>())
                if (entry.second.is<std::string>())
                    dialogue.declaredDependencies.push_back(entry.second.as<std::string>());
        }
        return dialogue;
    }

    mwmp::records::Script parseScript(const sol::table& table)
    {
        mwmp::records::Script script;
        script.recordFlags = static_cast<std::uint32_t>(table.get_or("recordFlags", 0));
        script.sourceText = table.get_or("sourceText", table.get_or("source", std::string{}));
        if (const sol::object value = table["dependencies"]; value.is<sol::table>())
        {
            for (const auto& entry : value.as<sol::table>())
                if (entry.second.is<std::string>())
                    script.declaredDependencies.push_back(entry.second.as<std::string>());
        }
        return script;
    }
}

bool mwmp::isCanonicalServerLuaRecordType(std::string_view recordType)
{
    return recordType == "potion" || recordType == "enchantment" || recordType == "weapon"
        || recordType == "armor" || recordType == "clothing" || recordType == "book"
        || recordType == "dialogue" || recordType == "script";
}

mwmp::records::DynamicRecordDefinition mwmp::parseServerLuaRecord(
    std::string_view recordType, std::string_view serializedTable, records::AuthoringMode authoringMode)
{
    if (!isCanonicalServerLuaRecordType(recordType))
        throw std::runtime_error("Unsupported canonical server-Lua dynamic record type: "
            + std::string(recordType));

    sol::state lua;
    const sol::table table = deserializeTable(lua, serializedTable);
    if (recordType == "dialogue")
        return { records::CurrentSchemaVersion, parseDialogue(table), authoringMode };
    if (recordType == "script")
        return { records::CurrentSchemaVersion, parseScript(table), authoringMode };
    normalizeLegacyEffects(table);
    records::DynamicRecordDefinition definition;
    if (recordType == "potion")
        definition = { records::CurrentSchemaVersion,
            restoreTextReferences(
                std::get<records::Potion>(records::fromEsmRecord(MWLua::tableToPotion(table)).data), table) };
    else if (recordType == "enchantment")
        definition = records::fromEsmRecord(MWLua::tableToEnchantment(table));
    else if (recordType == "weapon")
        definition = { records::CurrentSchemaVersion,
            restoreTextReferences(
                std::get<records::Weapon>(records::fromEsmRecord(MWLua::tableToWeapon(table)).data), table) };
    else if (recordType == "armor")
        definition = { records::CurrentSchemaVersion,
            restoreTextReferences(
                std::get<records::Armor>(records::fromEsmRecord(MWLua::tableToArmor(table)).data), table) };
    else if (recordType == "clothing")
        definition = { records::CurrentSchemaVersion,
            restoreTextReferences(
                std::get<records::Clothing>(records::fromEsmRecord(MWLua::tableToClothing(table)).data), table) };
    else
        definition = { records::CurrentSchemaVersion,
            restoreTextReferences(
                std::get<records::Book>(records::fromEsmRecord(MWLua::tableToBook(table)).data), table) };
    definition.authoringMode = authoringMode;
    return definition;
}
