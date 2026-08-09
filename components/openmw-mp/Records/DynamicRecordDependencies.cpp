#include "DynamicRecordDependencies.hpp"

#include <algorithm>
#include <type_traits>

namespace mwmp::records
{
    namespace
    {
        void add(std::vector<std::string>& result, std::string_view value)
        {
            if (value.empty())
                return;
            std::string normalized(value);
            std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
                return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c);
            });
            result.push_back(std::move(normalized));
        }

        void addItem(std::vector<std::string>& result, const ItemFields& item)
        {
            add(result, item.scriptId);
        }

        void addEffects(std::vector<std::string>& result, const std::vector<MagicEffect>& effects)
        {
            for (const MagicEffect& effect : effects)
            {
                add(result, effect.effectId);
                add(result, effect.skillId);
                add(result, effect.attributeId);
            }
        }

        void addEnchantment(std::vector<std::string>& result, const RecordReference& enchantment)
        {
            if (enchantment.kind == ReferenceKind::ContentId)
                add(result, enchantment.value);
        }

        void addParts(std::vector<std::string>& result, const std::vector<BodyPartReference>& parts)
        {
            for (const BodyPartReference& part : parts)
            {
                add(result, part.maleId);
                add(result, part.femaleId);
            }
        }

        void addDialogueInfo(std::vector<std::string>& result, const DialogueInfo& info)
        {
            add(result, info.actorId);
            add(result, info.raceId);
            add(result, info.classId);
            add(result, info.factionId);
            add(result, info.pcFactionId);
            add(result, info.cellId);
            for (const DialogueCondition& condition : info.conditions)
            {
                // Global/Local/Journal/Item/Dead/NotId/NotFaction/NotClass/
                // NotRace/NotCell/NotLocal are ESM functions 74..84. Local
                // variable names (75 and 84) are not content identities.
                if (condition.function >= 74 && condition.function <= 83 && condition.function != 75)
                    add(result, condition.variable);
            }
        }
    }

    std::vector<std::string> extractContentDependencies(const DynamicRecordDefinition& definition)
    {
        std::vector<std::string> result;
        std::visit(
            [&](const auto& record) {
                using Record = std::decay_t<decltype(record)>;
                if constexpr (std::is_same_v<Record, Potion>)
                {
                    addItem(result, record.item);
                    addEffects(result, record.effects);
                }
                else if constexpr (std::is_same_v<Record, Enchantment>)
                    addEffects(result, record.effects);
                else if constexpr (std::is_same_v<Record, Weapon> || std::is_same_v<Record, Armor>
                    || std::is_same_v<Record, Clothing> || std::is_same_v<Record, Book>)
                {
                    addItem(result, record.item);
                    addEnchantment(result, record.enchantment);
                    if constexpr (std::is_same_v<Record, Armor> || std::is_same_v<Record, Clothing>)
                        addParts(result, record.parts);
                }
                else if constexpr (std::is_same_v<Record, Dialogue>)
                {
                    for (const DialogueInfo& info : record.infos)
                        addDialogueInfo(result, info);
                    for (const std::string& dependency : record.declaredDependencies)
                        add(result, dependency);
                }
                else if constexpr (std::is_same_v<Record, Script>)
                {
                    // Script source is opaque. Explicit declarations are the
                    // only safe ordering/lifetime metadata for SCPT v1.
                    for (const std::string& dependency : record.declaredDependencies)
                        add(result, dependency);
                }
            },
            definition.data);
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }
}
