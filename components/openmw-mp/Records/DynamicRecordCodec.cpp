#include "DynamicRecordCodec.hpp"

#include <bit>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace mwmp::records
{
    namespace
    {
        constexpr std::string_view sDefinitionMagic = "OMDR";
        constexpr std::string_view sBundleMagic = "OMDB";
        constexpr std::uint32_t sMaximumDecodedString = 4 * 1024 * 1024;
        constexpr std::uint32_t sMaximumDecodedCollection = 4096;

        class Writer
        {
        public:
            void u8(std::uint8_t value) { mBytes.push_back(static_cast<char>(value)); }
            void u16(std::uint16_t value)
            {
                u8(static_cast<std::uint8_t>(value));
                u8(static_cast<std::uint8_t>(value >> 8));
            }
            void u32(std::uint32_t value)
            {
                for (int shift = 0; shift < 32; shift += 8)
                    u8(static_cast<std::uint8_t>(value >> shift));
            }
            void i16(std::int16_t value) { u16(std::bit_cast<std::uint16_t>(value)); }
            void i8(std::int8_t value) { u8(std::bit_cast<std::uint8_t>(value)); }
            void i32(std::int32_t value) { u32(std::bit_cast<std::uint32_t>(value)); }
            void f32(float value) { u32(std::bit_cast<std::uint32_t>(value)); }
            void string(std::string_view value)
            {
                if (value.size() > std::numeric_limits<std::uint32_t>::max())
                    throw std::length_error("Dynamic record string is too long");
                u32(static_cast<std::uint32_t>(value.size()));
                mBytes.append(value);
            }
            void fixed(std::string_view value) { mBytes.append(value); }
            std::string take() { return std::move(mBytes); }

        private:
            std::string mBytes;
        };

        class Reader
        {
        public:
            explicit Reader(std::string_view bytes)
                : mBytes(bytes)
            {
            }

            std::uint8_t u8()
            {
                need(1);
                return static_cast<std::uint8_t>(mBytes[mPosition++]);
            }
            std::uint16_t u16()
            {
                const std::uint16_t a = u8();
                return static_cast<std::uint16_t>(a | (static_cast<std::uint16_t>(u8()) << 8));
            }
            std::uint32_t u32()
            {
                std::uint32_t value = 0;
                for (int shift = 0; shift < 32; shift += 8)
                    value |= static_cast<std::uint32_t>(u8()) << shift;
                return value;
            }
            std::int16_t i16() { return std::bit_cast<std::int16_t>(u16()); }
            std::int8_t i8() { return std::bit_cast<std::int8_t>(u8()); }
            std::int32_t i32() { return std::bit_cast<std::int32_t>(u32()); }
            float f32() { return std::bit_cast<float>(u32()); }
            std::string string()
            {
                const std::uint32_t size = u32();
                if (size > sMaximumDecodedString)
                    throw std::runtime_error("Dynamic record string exceeds decoding limit");
                need(size);
                std::string result(mBytes.substr(mPosition, size));
                mPosition += size;
                return result;
            }
            void expect(std::string_view expected)
            {
                need(expected.size());
                if (mBytes.substr(mPosition, expected.size()) != expected)
                    throw std::runtime_error("Invalid dynamic record payload magic");
                mPosition += expected.size();
            }
            std::string_view bytes(std::size_t size)
            {
                need(size);
                const std::string_view result = mBytes.substr(mPosition, size);
                mPosition += size;
                return result;
            }
            void finish() const
            {
                if (mPosition != mBytes.size())
                    throw std::runtime_error("Trailing bytes in dynamic record payload");
            }

        private:
            void need(std::size_t size) const
            {
                if (size > mBytes.size() - mPosition)
                    throw std::runtime_error("Truncated dynamic record payload");
            }
            std::string_view mBytes;
            std::size_t mPosition = 0;
        };

        void writeReference(Writer& out, const RecordReference& value)
        {
            out.u8(static_cast<std::uint8_t>(value.kind));
            out.string(value.value);
        }

        RecordReference readReference(Reader& in)
        {
            RecordReference result;
            result.kind = static_cast<ReferenceKind>(in.u8());
            result.value = in.string();
            return result;
        }

        void writeItem(Writer& out, const ItemFields& value)
        {
            out.u32(value.recordFlags);
            out.string(value.name);
            out.string(value.model);
            out.string(value.icon);
            out.string(value.scriptId);
            out.f32(value.weight);
            out.i32(value.value);
        }

        ItemFields readItem(Reader& in)
        {
            ItemFields value;
            value.recordFlags = in.u32();
            value.name = in.string();
            value.model = in.string();
            value.icon = in.string();
            value.scriptId = in.string();
            value.weight = in.f32();
            value.value = in.i32();
            return value;
        }

        void writeEffects(Writer& out, const std::vector<MagicEffect>& effects)
        {
            out.u32(static_cast<std::uint32_t>(effects.size()));
            for (const MagicEffect& effect : effects)
            {
                out.string(effect.effectId);
                out.string(effect.skillId);
                out.string(effect.attributeId);
                out.i32(effect.range);
                out.i32(effect.area);
                out.i32(effect.duration);
                out.i32(effect.magnitudeMin);
                out.i32(effect.magnitudeMax);
            }
        }

        std::vector<MagicEffect> readEffects(Reader& in)
        {
            const std::uint32_t count = in.u32();
            if (count > sMaximumDecodedCollection)
                throw std::runtime_error("Too many effects in dynamic record payload");
            std::vector<MagicEffect> effects(count);
            for (MagicEffect& effect : effects)
            {
                effect.effectId = in.string();
                effect.skillId = in.string();
                effect.attributeId = in.string();
                effect.range = in.i32();
                effect.area = in.i32();
                effect.duration = in.i32();
                effect.magnitudeMin = in.i32();
                effect.magnitudeMax = in.i32();
            }
            return effects;
        }

        void writeParts(Writer& out, const std::vector<BodyPartReference>& parts)
        {
            out.u32(static_cast<std::uint32_t>(parts.size()));
            for (const BodyPartReference& part : parts)
            {
                out.u8(part.part);
                out.string(part.maleId);
                out.string(part.femaleId);
            }
        }

        std::vector<BodyPartReference> readParts(Reader& in)
        {
            const std::uint32_t count = in.u32();
            if (count > sMaximumDecodedCollection)
                throw std::runtime_error("Too many body parts in dynamic record payload");
            std::vector<BodyPartReference> parts(count);
            for (BodyPartReference& part : parts)
            {
                part.part = in.u8();
                part.maleId = in.string();
                part.femaleId = in.string();
            }
            return parts;
        }

        void writeConditions(Writer& out, const std::vector<DialogueCondition>& conditions)
        {
            out.u32(static_cast<std::uint32_t>(conditions.size()));
            for (const DialogueCondition& condition : conditions)
            {
                out.string(condition.variable);
                if (const auto* value = std::get_if<std::int32_t>(&condition.value))
                {
                    out.u8(0);
                    out.i32(*value);
                }
                else
                {
                    out.u8(1);
                    out.f32(std::get<float>(condition.value));
                }
                out.u8(condition.index);
                out.i8(condition.function);
                out.u8(static_cast<std::uint8_t>(condition.comparison));
            }
        }

        std::vector<DialogueCondition> readConditions(Reader& in)
        {
            const std::uint32_t count = in.u32();
            if (count > sMaximumDecodedCollection)
                throw std::runtime_error("Too many conditions in dynamic Dialogue payload");
            std::vector<DialogueCondition> conditions(count);
            for (DialogueCondition& condition : conditions)
            {
                condition.variable = in.string();
                const std::uint8_t valueType = in.u8();
                if (valueType == 0)
                    condition.value = in.i32();
                else if (valueType == 1)
                    condition.value = in.f32();
                else
                    throw std::runtime_error("Unsupported dynamic Dialogue condition value type");
                condition.index = in.u8();
                condition.function = in.i8();
                condition.comparison = static_cast<char>(in.u8());
            }
            return conditions;
        }

        void writeDialogueInfo(Writer& out, const DialogueInfo& info)
        {
            out.string(info.infoId);
            out.i32(info.dialogueType);
            out.i32(info.dispositionOrJournalIndex);
            out.i8(info.rank);
            out.i8(info.gender);
            out.i8(info.pcRank);
            writeConditions(out, info.conditions);
            out.string(info.actorId);
            out.string(info.raceId);
            out.string(info.classId);
            out.string(info.factionId);
            out.string(info.pcFactionId);
            out.string(info.cellId);
            out.string(info.sound);
            out.string(info.response);
            out.string(info.resultScript);
            out.u8(info.factionLess ? 1 : 0);
            out.i8(info.questStatus);
        }

        DialogueInfo readDialogueInfo(Reader& in)
        {
            DialogueInfo info;
            info.infoId = in.string();
            info.dialogueType = in.i32();
            info.dispositionOrJournalIndex = in.i32();
            info.rank = in.i8();
            info.gender = in.i8();
            info.pcRank = in.i8();
            info.conditions = readConditions(in);
            info.actorId = in.string();
            info.raceId = in.string();
            info.classId = in.string();
            info.factionId = in.string();
            info.pcFactionId = in.string();
            info.cellId = in.string();
            info.sound = in.string();
            info.response = in.string();
            info.resultScript = in.string();
            info.factionLess = in.u8() != 0;
            info.questStatus = in.i8();
            return info;
        }

        void writeDefinitionBody(Writer& out, const DynamicRecordDefinition& definition)
        {
            out.u16(definition.schemaVersion);
            out.u8(static_cast<std::uint8_t>(getRecordType(definition)));
            if (definition.schemaVersion >= 2)
                out.u8(static_cast<std::uint8_t>(definition.authoringMode));
            std::visit(
                [&out](const auto& record) {
                    using Record = std::decay_t<decltype(record)>;
                    if constexpr (std::is_same_v<Record, Potion>)
                    {
                        writeItem(out, record.item);
                        out.i32(record.flags);
                        writeEffects(out, record.effects);
                    }
                    else if constexpr (std::is_same_v<Record, Enchantment>)
                    {
                        out.u32(record.recordFlags);
                        out.i32(record.type);
                        out.i32(record.cost);
                        out.i32(record.charge);
                        out.i32(record.flags);
                        writeEffects(out, record.effects);
                    }
                    else if constexpr (std::is_same_v<Record, Weapon>)
                    {
                        writeItem(out, record.item);
                        writeReference(out, record.enchantment);
                        out.i16(record.type);
                        out.u16(record.health);
                        out.f32(record.speed);
                        out.f32(record.reach);
                        out.u16(record.enchantCapacity);
                        for (auto value : record.chop)
                            out.u8(value);
                        for (auto value : record.slash)
                            out.u8(value);
                        for (auto value : record.thrust)
                            out.u8(value);
                        out.i32(record.flags);
                    }
                    else if constexpr (std::is_same_v<Record, Armor>)
                    {
                        writeItem(out, record.item);
                        writeReference(out, record.enchantment);
                        out.i32(record.type);
                        out.i32(record.health);
                        out.i32(record.enchantCapacity);
                        out.i32(record.armorRating);
                        writeParts(out, record.parts);
                    }
                    else if constexpr (std::is_same_v<Record, Clothing>)
                    {
                        writeItem(out, record.item);
                        writeReference(out, record.enchantment);
                        out.i32(record.type);
                        out.u16(record.enchantCapacity);
                        writeParts(out, record.parts);
                    }
                    else if constexpr (std::is_same_v<Record, Book>)
                    {
                        writeItem(out, record.item);
                        writeReference(out, record.enchantment);
                        out.string(record.text);
                        out.u8(record.isScroll ? 1 : 0);
                        out.i32(record.skillId);
                        out.i32(record.enchantCapacity);
                    }
                    else if constexpr (std::is_same_v<Record, Spell>)
                    {
                        out.u32(record.recordFlags);
                        out.string(record.name);
                        out.i32(record.type);
                        out.i32(record.cost);
                        out.i32(record.flags);
                        writeEffects(out, record.effects);
                    }
                    else if constexpr (std::is_same_v<Record, Dialogue>)
                    {
                        out.string(record.stringId);
                        out.i8(record.type);
                        out.u32(static_cast<std::uint32_t>(record.infos.size()));
                        for (const DialogueInfo& info : record.infos)
                            writeDialogueInfo(out, info);
                        out.u32(static_cast<std::uint32_t>(record.declaredDependencies.size()));
                        for (const std::string& dependency : record.declaredDependencies)
                            out.string(dependency);
                    }
                    else if constexpr (std::is_same_v<Record, Script>)
                    {
                        out.u32(record.recordFlags);
                        out.string(record.sourceText);
                        out.u32(static_cast<std::uint32_t>(record.declaredDependencies.size()));
                        for (const std::string& dependency : record.declaredDependencies)
                            out.string(dependency);
                    }
                },
                definition.data);
        }

        DynamicRecordDefinition readDefinitionBody(Reader& in)
        {
            DynamicRecordDefinition definition;
            definition.schemaVersion = in.u16();
            const RecordType type = static_cast<RecordType>(in.u8());
            if (definition.schemaVersion >= 2)
                definition.authoringMode = static_cast<AuthoringMode>(in.u8());
            switch (type)
            {
                case RecordType::Potion:
                {
                    Potion value;
                    value.item = readItem(in);
                    value.flags = in.i32();
                    value.effects = readEffects(in);
                    definition.data = std::move(value);
                    break;
                }
                case RecordType::Enchantment:
                {
                    Enchantment value;
                    value.recordFlags = in.u32();
                    value.type = in.i32();
                    value.cost = in.i32();
                    value.charge = in.i32();
                    value.flags = in.i32();
                    value.effects = readEffects(in);
                    definition.data = std::move(value);
                    break;
                }
                case RecordType::Weapon:
                {
                    Weapon value;
                    value.item = readItem(in);
                    value.enchantment = readReference(in);
                    value.type = in.i16();
                    value.health = in.u16();
                    value.speed = in.f32();
                    value.reach = in.f32();
                    value.enchantCapacity = in.u16();
                    for (auto& entry : value.chop)
                        entry = in.u8();
                    for (auto& entry : value.slash)
                        entry = in.u8();
                    for (auto& entry : value.thrust)
                        entry = in.u8();
                    value.flags = in.i32();
                    definition.data = std::move(value);
                    break;
                }
                case RecordType::Armor:
                {
                    Armor value;
                    value.item = readItem(in);
                    value.enchantment = readReference(in);
                    value.type = in.i32();
                    value.health = in.i32();
                    value.enchantCapacity = in.i32();
                    value.armorRating = in.i32();
                    value.parts = readParts(in);
                    definition.data = std::move(value);
                    break;
                }
                case RecordType::Clothing:
                {
                    Clothing value;
                    value.item = readItem(in);
                    value.enchantment = readReference(in);
                    value.type = in.i32();
                    value.enchantCapacity = in.u16();
                    value.parts = readParts(in);
                    definition.data = std::move(value);
                    break;
                }
                case RecordType::Book:
                {
                    Book value;
                    value.item = readItem(in);
                    value.enchantment = readReference(in);
                    value.text = in.string();
                    value.isScroll = in.u8() != 0;
                    value.skillId = in.i32();
                    value.enchantCapacity = in.i32();
                    definition.data = std::move(value);
                    break;
                }
                case RecordType::Spell:
                {
                    Spell value;
                    value.recordFlags = in.u32();
                    value.name = in.string();
                    value.type = in.i32();
                    value.cost = in.i32();
                    value.flags = in.i32();
                    value.effects = readEffects(in);
                    definition.data = std::move(value);
                    break;
                }
                case RecordType::Dialogue:
                {
                    Dialogue value;
                    value.stringId = in.string();
                    value.type = in.i8();
                    const std::uint32_t count = in.u32();
                    if (count > sMaximumDecodedCollection)
                        throw std::runtime_error("Too many INFOs in dynamic Dialogue payload");
                    value.infos.reserve(count);
                    for (std::uint32_t i = 0; i < count; ++i)
                        value.infos.push_back(readDialogueInfo(in));
                    const std::uint32_t dependencyCount = in.u32();
                    if (dependencyCount > sMaximumDecodedCollection)
                        throw std::runtime_error("Too many declared Dialogue dependencies");
                    value.declaredDependencies.reserve(dependencyCount);
                    for (std::uint32_t i = 0; i < dependencyCount; ++i)
                        value.declaredDependencies.push_back(in.string());
                    definition.data = std::move(value);
                    break;
                }
                case RecordType::Script:
                {
                    Script value;
                    value.recordFlags = in.u32();
                    value.sourceText = in.string();
                    const std::uint32_t dependencyCount = in.u32();
                    if (dependencyCount > sMaximumDecodedCollection)
                        throw std::runtime_error("Too many declared Script dependencies");
                    value.declaredDependencies.reserve(dependencyCount);
                    for (std::uint32_t i = 0; i < dependencyCount; ++i)
                        value.declaredDependencies.push_back(in.string());
                    definition.data = std::move(value);
                    break;
                }
                default:
                    throw std::runtime_error("Unsupported dynamic record type tag");
            }
            return definition;
        }
    }

    RecordType getRecordType(const DynamicRecordDefinition& definition)
    {
        return std::visit(
            [](const auto& value) {
                using Record = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Record, Potion>)
                    return RecordType::Potion;
                else if constexpr (std::is_same_v<Record, Enchantment>)
                    return RecordType::Enchantment;
                else if constexpr (std::is_same_v<Record, Weapon>)
                    return RecordType::Weapon;
                else if constexpr (std::is_same_v<Record, Armor>)
                    return RecordType::Armor;
                else if constexpr (std::is_same_v<Record, Clothing>)
                    return RecordType::Clothing;
                else if constexpr (std::is_same_v<Record, Book>)
                    return RecordType::Book;
                else if constexpr (std::is_same_v<Record, Spell>)
                    return RecordType::Spell;
                else if constexpr (std::is_same_v<Record, Dialogue>)
                    return RecordType::Dialogue;
                else
                    return RecordType::Script;
            },
            definition.data);
    }

    std::string_view getRecordTypeName(RecordType type)
    {
        switch (type)
        {
            case RecordType::Potion:
                return "potion";
            case RecordType::Enchantment:
                return "enchantment";
            case RecordType::Weapon:
                return "weapon";
            case RecordType::Armor:
                return "armor";
            case RecordType::Clothing:
                return "clothing";
            case RecordType::Book:
                return "book";
            case RecordType::Spell:
                return "spell";
            case RecordType::Dialogue:
                return "dialogue";
            case RecordType::Script:
                return "script";
        }
        return {};
    }

    std::string encodeDefinition(const DynamicRecordDefinition& definition)
    {
        Writer out;
        out.fixed(sDefinitionMagic);
        writeDefinitionBody(out, definition);
        std::string result = out.take();
        if (result.size() > MaximumDefinitionBytes)
            throw std::length_error("Dynamic record definition exceeds encoding limit");
        return result;
    }

    DynamicRecordDefinition decodeDefinition(std::string_view bytes)
    {
        if (bytes.size() > MaximumDefinitionBytes)
            throw std::runtime_error("Dynamic record definition exceeds decoding limit");
        Reader in(bytes);
        in.expect(sDefinitionMagic);
        DynamicRecordDefinition definition = readDefinitionBody(in);
        in.finish();
        return definition;
    }

    std::string encodeBundle(const DynamicRecordBundle& bundle)
    {
        Writer out;
        out.fixed(sBundleMagic);
        out.u16(bundle.wireVersion);
        out.u32(static_cast<std::uint32_t>(bundle.records.size()));
        for (const RecordDraft& draft : bundle.records)
        {
            out.string(draft.temporaryKey);
            out.string(encodeDefinition(draft.definition));
        }
        out.u32(static_cast<std::uint32_t>(bundle.dependencies.size()));
        for (const RecordDependency& dependency : bundle.dependencies)
        {
            out.string(dependency.ownerKey);
            out.string(dependency.dependencyKey);
        }
        std::string result = out.take();
        if (result.size() > MaximumBundleBytes)
            throw std::length_error("Dynamic record bundle exceeds encoding limit");
        return result;
    }

    DynamicRecordBundle decodeBundle(std::string_view bytes)
    {
        if (bytes.size() > MaximumBundleBytes)
            throw std::runtime_error("Dynamic record bundle exceeds decoding limit");
        Reader in(bytes);
        in.expect(sBundleMagic);
        DynamicRecordBundle bundle;
        bundle.wireVersion = in.u16();
        const std::uint32_t recordCount = in.u32();
        if (recordCount > sMaximumDecodedCollection)
            throw std::runtime_error("Too many records in dynamic record bundle");
        bundle.records.resize(recordCount);
        for (RecordDraft& draft : bundle.records)
        {
            draft.temporaryKey = in.string();
            draft.definition = decodeDefinition(in.string());
        }
        const std::uint32_t dependencyCount = in.u32();
        if (dependencyCount > sMaximumDecodedCollection)
            throw std::runtime_error("Too many dependencies in dynamic record bundle");
        bundle.dependencies.resize(dependencyCount);
        for (RecordDependency& dependency : bundle.dependencies)
        {
            dependency.ownerKey = in.string();
            dependency.dependencyKey = in.string();
        }
        in.finish();
        return bundle;
    }
}
