#ifndef OPENMW_MP_DYNAMIC_RECORD_TYPES_HPP
#define OPENMW_MP_DYNAMIC_RECORD_TYPES_HPP

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace mwmp::records
{
    inline constexpr std::uint16_t CurrentSchemaVersion = 1;
    inline constexpr std::uint16_t CurrentWireVersion = 1;

    enum class RecordType : std::uint8_t
    {
        Potion = 1,
        Enchantment = 2,
        Weapon = 3,
        Armor = 4,
        Clothing = 5,
        Book = 6,
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

    using DefinitionData = std::variant<Potion, Enchantment, Weapon, Armor, Clothing, Book>;

    struct DynamicRecordDefinition
    {
        std::uint16_t schemaVersion = CurrentSchemaVersion;
        DefinitionData data;
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
