#include "DynamicRecordValidation.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace mwmp::records
{
    namespace
    {
        bool isValidUtf8(std::string_view value)
        {
            for (std::size_t i = 0; i < value.size();)
            {
                const auto first = static_cast<unsigned char>(value[i]);
                std::size_t count = 0;
                std::uint32_t codepoint = 0;
                if (first <= 0x7f)
                {
                    ++i;
                    continue;
                }
                if ((first & 0xe0) == 0xc0)
                {
                    count = 1;
                    codepoint = first & 0x1f;
                }
                else if ((first & 0xf0) == 0xe0)
                {
                    count = 2;
                    codepoint = first & 0x0f;
                }
                else if ((first & 0xf8) == 0xf0)
                {
                    count = 3;
                    codepoint = first & 0x07;
                }
                else
                    return false;
                if (i + count >= value.size())
                    return false;
                for (std::size_t offset = 1; offset <= count; ++offset)
                {
                    const auto next = static_cast<unsigned char>(value[i + offset]);
                    if ((next & 0xc0) != 0x80)
                        return false;
                    codepoint = (codepoint << 6) | (next & 0x3f);
                }
                if ((count == 1 && codepoint < 0x80) || (count == 2 && codepoint < 0x800)
                    || (count == 3 && codepoint < 0x10000) || codepoint > 0x10ffff
                    || (codepoint >= 0xd800 && codepoint <= 0xdfff))
                    return false;
                i += count + 1;
            }
            return true;
        }

        std::string lowerAscii(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c);
            });
            return value;
        }

        std::string normalizePath(std::string value)
        {
            value = lowerAscii(std::move(value));
            std::replace(value.begin(), value.end(), '\\', '/');
            std::string result;
            result.reserve(value.size());
            bool previousSlash = false;
            for (char c : value)
            {
                if (c == '/')
                {
                    if (previousSlash)
                        continue;
                    previousSlash = true;
                }
                else
                    previousSlash = false;
                result.push_back(c);
            }
            while (result.starts_with("./"))
                result.erase(0, 2);
            return result;
        }

        void add(std::vector<ValidationError>& errors, std::string code, std::string path, std::string message)
        {
            errors.push_back({ std::move(code), std::move(path), std::move(message) });
        }

        void checkString(std::vector<ValidationError>& errors, std::string_view value, std::size_t maximum,
            std::string path, bool required = false)
        {
            if (required && value.empty())
                add(errors, "required_field", path, "Value must not be empty");
            if (value.size() > maximum)
                add(errors, "string_too_long", path, "Value exceeds its length limit");
            if (!isValidUtf8(value) || value.find('\0') != std::string_view::npos)
                add(errors, "invalid_utf8", std::move(path), "Value must be valid UTF-8 without NUL bytes");
        }

        void checkId(std::vector<ValidationError>& errors, std::string_view value, const ValidationLimits& limits,
            std::string path, bool required = false)
        {
            checkString(errors, value, limits.maxIdLength, std::move(path), required);
        }

        void checkPath(std::vector<ValidationError>& errors, std::string_view value, const ValidationLimits& limits,
            std::string path)
        {
            checkString(errors, value, limits.maxPathLength, path);
            if (value.empty())
                return;
            const std::string normalized = normalizePath(std::string(value));
            if (normalized.starts_with('/') || normalized.find(':') != std::string::npos)
                add(errors, "invalid_asset_path", path, "Asset path must be relative to the VFS");
            std::size_t start = 0;
            while (start <= normalized.size())
            {
                const std::size_t end = normalized.find('/', start);
                if (normalized.substr(start, end - start) == "..")
                {
                    add(errors, "invalid_asset_path", path, "Asset path must not escape the VFS");
                    break;
                }
                if (end == std::string::npos)
                    break;
                start = end + 1;
            }
        }

        void checkReference(std::vector<ValidationError>& errors, const RecordReference& reference,
            const ValidationLimits& limits, const std::string& path)
        {
            if (reference.kind != ReferenceKind::None && reference.kind != ReferenceKind::ContentId
                && reference.kind != ReferenceKind::TemporaryKey)
                add(errors, "invalid_reference_kind", path + ".kind", "Unsupported reference kind");
            if (reference.kind == ReferenceKind::None && !reference.value.empty())
                add(errors, "invalid_reference", path, "An empty reference kind cannot have a value");
            if (reference.kind != ReferenceKind::None)
                checkId(errors, reference.value, limits, path + ".value", true);
        }

        void checkItem(std::vector<ValidationError>& errors, const ItemFields& item, const ValidationLimits& limits,
            const std::string& path)
        {
            checkString(errors, item.name, limits.maxNameLength, path + ".name");
            checkPath(errors, item.model, limits, path + ".model");
            checkPath(errors, item.icon, limits, path + ".icon");
            checkId(errors, item.scriptId, limits, path + ".scriptId");
            if (!std::isfinite(item.weight) || item.weight < 0.f)
                add(errors, "invalid_number", path + ".weight", "Weight must be finite and non-negative");
            if (item.value < 0)
                add(errors, "invalid_number", path + ".value", "Value must be non-negative");
        }

        void checkEffects(std::vector<ValidationError>& errors, const std::vector<MagicEffect>& effects,
            const ValidationLimits& limits, const std::string& path)
        {
            if (effects.size() > limits.maxEffectsPerRecord)
                add(errors, "too_many_effects", path, "Effect count exceeds the configured limit");
            for (std::size_t i = 0; i < effects.size(); ++i)
            {
                const MagicEffect& effect = effects[i];
                const std::string itemPath = path + "[" + std::to_string(i) + "]";
                checkId(errors, effect.effectId, limits, itemPath + ".effectId", true);
                checkId(errors, effect.skillId, limits, itemPath + ".skillId");
                checkId(errors, effect.attributeId, limits, itemPath + ".attributeId");
                if (effect.range < 0 || effect.range > 2)
                    add(errors, "invalid_effect_range", itemPath + ".range", "Effect range must be self, touch, or target");
                if (effect.area < 0 || effect.duration < 0 || effect.magnitudeMin < 0 || effect.magnitudeMax < 0
                    || effect.magnitudeMin > effect.magnitudeMax)
                    add(errors, "invalid_effect", itemPath, "Effect dimensions and magnitudes are inconsistent");
            }
        }

        void checkParts(std::vector<ValidationError>& errors, const std::vector<BodyPartReference>& parts,
            const ValidationLimits& limits, const std::string& path)
        {
            if (parts.size() > limits.maxBodyPartsPerRecord)
                add(errors, "too_many_body_parts", path, "Body part count exceeds the configured limit");
            std::unordered_set<std::uint8_t> seen;
            for (std::size_t i = 0; i < parts.size(); ++i)
            {
                const BodyPartReference& part = parts[i];
                const std::string itemPath = path + "[" + std::to_string(i) + "]";
                if (part.part > 26)
                    add(errors, "invalid_body_part", itemPath + ".part", "Body part index is out of range");
                if (!seen.insert(part.part).second)
                    add(errors, "duplicate_body_part", itemPath + ".part", "Body part index is duplicated");
                checkId(errors, part.maleId, limits, itemPath + ".maleId");
                checkId(errors, part.femaleId, limits, itemPath + ".femaleId");
            }
        }

        void normalizeReference(RecordReference& reference)
        {
            reference.value = lowerAscii(std::move(reference.value));
            if (reference.kind == ReferenceKind::None)
                reference.value.clear();
        }

        void normalizeItem(ItemFields& item)
        {
            item.model = normalizePath(std::move(item.model));
            item.icon = normalizePath(std::move(item.icon));
            item.scriptId = lowerAscii(std::move(item.scriptId));
            if (item.weight == 0.f)
                item.weight = 0.f;
        }

        void normalizeEffects(std::vector<MagicEffect>& effects)
        {
            for (MagicEffect& effect : effects)
            {
                effect.effectId = lowerAscii(std::move(effect.effectId));
                effect.skillId = lowerAscii(std::move(effect.skillId));
                effect.attributeId = lowerAscii(std::move(effect.attributeId));
            }
            std::sort(effects.begin(), effects.end(), [](const MagicEffect& lhs, const MagicEffect& rhs) {
                return std::tie(lhs.effectId, lhs.skillId, lhs.attributeId, lhs.range, lhs.area, lhs.duration,
                           lhs.magnitudeMin, lhs.magnitudeMax)
                    < std::tie(rhs.effectId, rhs.skillId, rhs.attributeId, rhs.range, rhs.area, rhs.duration,
                        rhs.magnitudeMin, rhs.magnitudeMax);
            });
        }

        void normalizeParts(std::vector<BodyPartReference>& parts)
        {
            for (BodyPartReference& part : parts)
            {
                part.maleId = lowerAscii(std::move(part.maleId));
                part.femaleId = lowerAscii(std::move(part.femaleId));
            }
            std::sort(parts.begin(), parts.end(), [](const auto& lhs, const auto& rhs) {
                return std::tie(lhs.part, lhs.maleId, lhs.femaleId) < std::tie(rhs.part, rhs.maleId, rhs.femaleId);
            });
        }
    }

    std::vector<ValidationError> validate(
        const DynamicRecordDefinition& definition, const ValidationLimits& limits)
    {
        std::vector<ValidationError> errors;
        if (definition.schemaVersion != CurrentSchemaVersion)
            add(errors, "unsupported_schema", "schemaVersion", "Dynamic record schema version is not supported");

        std::visit(
            [&](const auto& record) {
                using Record = std::decay_t<decltype(record)>;
                if constexpr (std::is_same_v<Record, Potion>)
                {
                    checkItem(errors, record.item, limits, "potion.item");
                    checkEffects(errors, record.effects, limits, "potion.effects");
                }
                else if constexpr (std::is_same_v<Record, Enchantment>)
                {
                    if (record.type < 0 || record.type > 3)
                        add(errors, "invalid_enchantment_type", "enchantment.type", "Enchantment type is out of range");
                    if (record.cost < 0 || record.charge < 0)
                        add(errors, "invalid_number", "enchantment", "Cost and charge must be non-negative");
                    checkEffects(errors, record.effects, limits, "enchantment.effects");
                }
                else if constexpr (std::is_same_v<Record, Weapon>)
                {
                    checkItem(errors, record.item, limits, "weapon.item");
                    checkReference(errors, record.enchantment, limits, "weapon.enchantment");
                    if (record.type < -4 || record.type > 13)
                        add(errors, "invalid_weapon_type", "weapon.type", "Weapon type is out of range");
                    if (!std::isfinite(record.speed) || !std::isfinite(record.reach) || record.speed < 0.f
                        || record.reach < 0.f)
                        add(errors, "invalid_number", "weapon", "Weapon speed and reach must be finite and non-negative");
                    if (record.chop[0] > record.chop[1] || record.slash[0] > record.slash[1]
                        || record.thrust[0] > record.thrust[1])
                        add(errors, "invalid_weapon_damage", "weapon", "Weapon minimum damage exceeds maximum damage");
                }
                else if constexpr (std::is_same_v<Record, Armor>)
                {
                    checkItem(errors, record.item, limits, "armor.item");
                    checkReference(errors, record.enchantment, limits, "armor.enchantment");
                    if (record.type < 0 || record.type > 10)
                        add(errors, "invalid_armor_type", "armor.type", "Armor type is out of range");
                    if (record.health < 0 || record.enchantCapacity < 0 || record.armorRating < 0)
                        add(errors, "invalid_number", "armor", "Armor statistics must be non-negative");
                    checkParts(errors, record.parts, limits, "armor.parts");
                }
                else if constexpr (std::is_same_v<Record, Clothing>)
                {
                    checkItem(errors, record.item, limits, "clothing.item");
                    checkReference(errors, record.enchantment, limits, "clothing.enchantment");
                    if (record.type < 0 || record.type > 9)
                        add(errors, "invalid_clothing_type", "clothing.type", "Clothing type is out of range");
                    checkParts(errors, record.parts, limits, "clothing.parts");
                }
                else if constexpr (std::is_same_v<Record, Book>)
                {
                    checkItem(errors, record.item, limits, "book.item");
                    checkReference(errors, record.enchantment, limits, "book.enchantment");
                    checkString(errors, record.text, limits.maxTextLength, "book.text");
                    if (record.skillId < -1 || record.skillId > 26 || record.enchantCapacity < 0)
                        add(errors, "invalid_number", "book", "Book skill or enchantment capacity is out of range");
                }
            },
            definition.data);
        return errors;
    }

    std::vector<ValidationError> validate(const DynamicRecordBundle& bundle, const ValidationLimits& limits)
    {
        std::vector<ValidationError> errors;
        if (bundle.wireVersion != CurrentWireVersion)
            add(errors, "unsupported_wire_version", "wireVersion", "Dynamic record wire version is not supported");
        if (bundle.records.empty())
            add(errors, "empty_bundle", "records", "At least one record is required");
        if (bundle.records.size() > limits.maxRecordsPerBundle)
            add(errors, "too_many_records", "records", "Record count exceeds the configured limit");
        if (bundle.dependencies.size() > limits.maxDependenciesPerBundle)
            add(errors, "too_many_dependencies", "dependencies", "Dependency count exceeds the configured limit");

        std::unordered_map<std::string, std::size_t> keys;
        for (std::size_t i = 0; i < bundle.records.size(); ++i)
        {
            const RecordDraft& draft = bundle.records[i];
            const std::string path = "records[" + std::to_string(i) + "]";
            checkId(errors, draft.temporaryKey, limits, path + ".temporaryKey", true);
            if (!draft.temporaryKey.empty() && !keys.emplace(draft.temporaryKey, i).second)
                add(errors, "duplicate_temporary_key", path + ".temporaryKey", "Temporary record key is duplicated");
            for (ValidationError error : validate(draft.definition, limits))
            {
                error.path = path + ".definition." + error.path;
                errors.push_back(std::move(error));
            }
        }

        std::unordered_map<std::string, std::vector<std::string>> graph;
        std::unordered_set<std::string> dependencyPairs;
        for (std::size_t i = 0; i < bundle.dependencies.size(); ++i)
        {
            const RecordDependency& dependency = bundle.dependencies[i];
            const std::string path = "dependencies[" + std::to_string(i) + "]";
            if (!keys.contains(dependency.ownerKey) || !keys.contains(dependency.dependencyKey))
                add(errors, "missing_dependency_record", path, "Dependency endpoint does not exist in the bundle");
            if (dependency.ownerKey == dependency.dependencyKey)
                add(errors, "dependency_cycle", path, "A record cannot depend on itself");
            const std::string pair = dependency.ownerKey + '\0' + dependency.dependencyKey;
            if (!dependencyPairs.insert(pair).second)
                add(errors, "duplicate_dependency", path, "Dependency is duplicated");
            graph[dependency.ownerKey].push_back(dependency.dependencyKey);
        }

        enum class Visit : std::uint8_t { None, Active, Complete };
        std::unordered_map<std::string, Visit> visits;
        std::function<bool(const std::string&)> hasCycle = [&](const std::string& key) {
            Visit& visit = visits[key];
            if (visit == Visit::Active)
                return true;
            if (visit == Visit::Complete)
                return false;
            visit = Visit::Active;
            for (const std::string& dependency : graph[key])
                if (hasCycle(dependency))
                    return true;
            visit = Visit::Complete;
            return false;
        };
        for (const auto& [key, index] : keys)
        {
            (void)index;
            if (hasCycle(key))
            {
                add(errors, "dependency_cycle", "dependencies", "Dependency graph contains a cycle");
                break;
            }
        }
        return errors;
    }

    DynamicRecordDefinition canonicalize(DynamicRecordDefinition definition)
    {
        std::visit(
            [](auto& record) {
                using Record = std::decay_t<decltype(record)>;
                if constexpr (!std::is_same_v<Record, Enchantment>)
                    normalizeItem(record.item);
                if constexpr (std::is_same_v<Record, Potion> || std::is_same_v<Record, Enchantment>)
                    normalizeEffects(record.effects);
                if constexpr (std::is_same_v<Record, Weapon> || std::is_same_v<Record, Armor>
                    || std::is_same_v<Record, Clothing> || std::is_same_v<Record, Book>)
                    normalizeReference(record.enchantment);
                if constexpr (std::is_same_v<Record, Armor> || std::is_same_v<Record, Clothing>)
                    normalizeParts(record.parts);
                if constexpr (std::is_same_v<Record, Weapon>)
                {
                    if (record.speed == 0.f)
                        record.speed = 0.f;
                    if (record.reach == 0.f)
                        record.reach = 0.f;
                }
            },
            definition.data);
        return definition;
    }

    DynamicRecordBundle canonicalize(DynamicRecordBundle bundle)
    {
        for (RecordDraft& record : bundle.records)
        {
            record.temporaryKey = lowerAscii(std::move(record.temporaryKey));
            record.definition = canonicalize(std::move(record.definition));
        }
        for (RecordDependency& dependency : bundle.dependencies)
        {
            dependency.ownerKey = lowerAscii(std::move(dependency.ownerKey));
            dependency.dependencyKey = lowerAscii(std::move(dependency.dependencyKey));
        }
        std::sort(bundle.records.begin(), bundle.records.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.temporaryKey < rhs.temporaryKey;
        });
        std::sort(bundle.dependencies.begin(), bundle.dependencies.end(), [](const auto& lhs, const auto& rhs) {
            return std::tie(lhs.ownerKey, lhs.dependencyKey) < std::tie(rhs.ownerKey, rhs.dependencyKey);
        });
        return bundle;
    }
}
