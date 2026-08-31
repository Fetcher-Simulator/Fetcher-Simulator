#ifndef OPENMW_MP_DYNAMIC_RECORD_TYPES_HPP
#define OPENMW_MP_DYNAMIC_RECORD_TYPES_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace mwmp::records
{
    inline constexpr std::uint16_t CurrentSchemaVersion = 3;
    inline constexpr std::uint16_t CurrentWireVersion = 3;
    inline constexpr std::size_t MaximumDefinitionBytes = 8 * 1024 * 1024;
    inline constexpr std::size_t MaximumBundleBytes = 16 * 1024 * 1024;

    enum class RecordType : std::uint8_t
    {
        Potion = 1,
        Enchantment = 2,
        Weapon = 3,
        Armor = 4,
        Clothing = 5,
        Book = 6,
        Dialogue = 7,
        Script = 8,
        Spell = 9,
    };

    /// Durable authoring intent. Generated is the existing content-addressed
    /// record path; New and Override are explicit fixed-identity server
    /// content modes and must never be inferred from a store collision.
    enum class AuthoringMode : std::uint8_t
    {
        Generated = 0,
        New = 1,
        Override = 2,
    };

    enum class ReferenceKind : std::uint8_t
    {
        None = 0,
        ContentId = 1,
        TemporaryKey = 2,
    };

    struct RecordReference
    {
        ReferenceKind kind = ReferenceKind::None;
        std::string value;
        bool operator==(const RecordReference&) const = default;
    };

    struct MagicEffect
    {
        std::string effectId;
        std::string skillId;
        std::string attributeId;
        std::int32_t range = 0;
        std::int32_t area = 0;
        std::int32_t duration = 0;
        std::int32_t magnitudeMin = 0;
        std::int32_t magnitudeMax = 0;
        bool operator==(const MagicEffect&) const = default;
    };

    struct BodyPartReference
    {
        std::uint8_t part = 0;
        std::string maleId;
        std::string femaleId;
        bool operator==(const BodyPartReference&) const = default;
    };

    struct ItemFields
    {
        std::uint32_t recordFlags = 0;
        std::string name;
        std::string model;
        std::string icon;
        std::string scriptId;
        float weight = 0.f;
        std::int32_t value = 0;
        bool operator==(const ItemFields&) const = default;
    };

    struct Potion
    {
        ItemFields item;
        std::int32_t flags = 0;
        std::vector<MagicEffect> effects;
        bool operator==(const Potion&) const = default;
    };

    struct Enchantment
    {
        std::uint32_t recordFlags = 0;
        std::int32_t type = 0;
        std::int32_t cost = 0;
        std::int32_t charge = 0;
        std::int32_t flags = 0;
        std::vector<MagicEffect> effects;
        bool operator==(const Enchantment&) const = default;
    };

    struct Weapon
    {
        ItemFields item;
        RecordReference enchantment;
        std::int16_t type = 0;
        std::uint16_t health = 0;
        float speed = 0.f;
        float reach = 0.f;
        std::uint16_t enchantCapacity = 0;
        std::array<std::uint8_t, 2> chop{};
        std::array<std::uint8_t, 2> slash{};
        std::array<std::uint8_t, 2> thrust{};
        std::int32_t flags = 0;
        bool operator==(const Weapon&) const = default;
    };

    struct Armor
    {
        ItemFields item;
        RecordReference enchantment;
        std::int32_t type = 0;
        std::int32_t health = 0;
        std::int32_t enchantCapacity = 0;
        std::int32_t armorRating = 0;
        std::vector<BodyPartReference> parts;
        bool operator==(const Armor&) const = default;
    };

    struct Clothing
    {
        ItemFields item;
        RecordReference enchantment;
        std::int32_t type = 0;
        std::uint16_t enchantCapacity = 0;
        std::vector<BodyPartReference> parts;
        bool operator==(const Clothing&) const = default;
    };

    struct Book
    {
        ItemFields item;
        RecordReference enchantment;
        std::string text;
        bool isScroll = false;
        std::int32_t skillId = -1;
        std::int32_t enchantCapacity = 0;
        bool operator==(const Book&) const = default;
    };

    struct Spell
    {
        std::uint32_t recordFlags = 0;
        std::string name;
        std::int32_t type = 0;
        std::int32_t cost = 0;
        std::int32_t flags = 0;
        std::vector<MagicEffect> effects;
        bool operator==(const Spell&) const = default;
    };

    struct DialogueCondition
    {
        std::string variable;
        std::variant<std::int32_t, float> value = std::int32_t{ 0 };
        std::uint8_t index = 0;
        std::int8_t function = 0;
        char comparison = '0';
        bool operator==(const DialogueCondition&) const = default;
    };

    struct DialogueInfo
    {
        std::string infoId;
        std::int32_t dialogueType = 0;
        std::int32_t dispositionOrJournalIndex = 0;
        std::int8_t rank = -1;
        std::int8_t gender = -1;
        std::int8_t pcRank = -1;
        std::vector<DialogueCondition> conditions;
        std::string actorId;
        std::string raceId;
        std::string classId;
        std::string factionId;
        std::string pcFactionId;
        std::string cellId;
        std::string sound;
        std::string response;
        std::string resultScript;
        bool factionLess = false;
        std::int8_t questStatus = 0;
        bool operator==(const DialogueInfo&) const = default;
    };

    /// Dialogue is persisted and replicated atomically with its complete,
    /// authoritative INFO order. mPrev/mNext are derived during ESM conversion.
    struct Dialogue
    {
        std::string stringId;
        std::int8_t type = 0;
        std::vector<DialogueInfo> infos;
        std::vector<std::string> declaredDependencies;
        bool operator==(const Dialogue&) const = default;
    };

    /// MWScript definitions are source-authoritative. Compiled bytecode,
    /// locals, and interpreter state are deliberately not part of OMDR.
    struct Script
    {
        std::uint32_t recordFlags = 0;
        std::string sourceText;
        std::vector<std::string> declaredDependencies;
        bool operator==(const Script&) const = default;
    };

    using DefinitionData
        = std::variant<Potion, Enchantment, Weapon, Armor, Clothing, Book, Dialogue, Script, Spell>;

    struct DynamicRecordDefinition
    {
        std::uint16_t schemaVersion = CurrentSchemaVersion;
        DefinitionData data;
        AuthoringMode authoringMode = AuthoringMode::Generated;
        bool operator==(const DynamicRecordDefinition&) const = default;
    };

    struct RecordDraft
    {
        std::string temporaryKey;
        DynamicRecordDefinition definition;
        bool operator==(const RecordDraft&) const = default;
    };

    struct RecordDependency
    {
        std::string ownerKey;
        std::string dependencyKey;
        bool operator==(const RecordDependency&) const = default;
    };

    struct DynamicRecordBundle
    {
        std::uint16_t wireVersion = CurrentWireVersion;
        std::vector<RecordDraft> records;
        std::vector<RecordDependency> dependencies;
        bool operator==(const DynamicRecordBundle&) const = default;
    };

    RecordType getRecordType(const DynamicRecordDefinition& definition);
    std::string_view getRecordTypeName(RecordType type);
}

#endif
