#ifndef OPENMW_MP_ENCHANTING_PROTOCOL_HPP
#define OPENMW_MP_ENCHANTING_PROTOCOL_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mwmp::records
{
    inline constexpr std::uint16_t CurrentEnchantingProtocolVersion = 1;

    /// Maximum number of effects the native enchant UI accepts.
    inline constexpr std::size_t MaxEnchantingEffects = 8;

    /// Maximum custom item name length accepted from the enchanting UI.
    inline constexpr std::size_t MaxEnchantingItemNameLength = 64;

    /// Maximum effect id length accepted from the client.
    inline constexpr std::size_t MaxEnchantingEffectIdLength = 255;

    /// Bounds for the effect fields the client may select. The native UI
    /// sliders never exceed these; the packet bounds hostile requests.
    inline constexpr std::int32_t MaxEnchantingMagnitude = 1000;
    inline constexpr std::int32_t MaxEnchantingDuration = 3600;
    inline constexpr std::int32_t MaxEnchantingArea = 1000;

    /// Machine-readable enchanting error codes. The journal persists terminal
    /// outcomes, so codes are stable and never reused with different meaning.
    enum class EnchantingError : std::uint16_t
    {
        None = 0,
        InvalidRequest = 1,
        RequestPending = 2,
        DuplicateRequestConflict = 3,
        StaleInventoryRevision = 4,
        TargetItemNotFound = 5,
        TargetItemNotOwned = 6,
        InvalidTargetItem = 7,
        SoulGemNotFound = 8,
        SoulGemNotOwned = 9,
        InvalidSoulGem = 10,
        EmptySoul = 11,
        InvalidSoul = 12,
        DuplicateSourceInstance = 13,
        InvalidEffect = 14,
        EffectNotAllowed = 15,
        InvalidMagnitude = 16,
        InvalidDuration = 17,
        InvalidArea = 18,
        CapacityExceeded = 19,
        InvalidCastStyle = 20,
        InsufficientGold = 21,
        InvalidEnchanter = 22,
        EnchanterUnavailable = 23,
        MechanicsValidationFailed = 24,
        ContentMismatch = 25,
        RateLimited = 26,
        QuotaExceeded = 27,
        UnsupportedProtocol = 28,
        ServerError = 29,
    };

    /// One effect as selected in the enchant UI. Only user choices travel to
    /// the server; the server derives cost, charge, and record identity.
    struct EnchantingEffectChoice
    {
        std::string effectId;
        std::string skillId;    // target skill for TargetSkill effects
        std::string attributeId; // target attribute for TargetAttribute effects
        std::int32_t range = 0; // ESM::RangeType: self/touch/target
        std::int32_t magnitudeMin = 0;
        std::int32_t magnitudeMax = 0;
        std::int32_t duration = 0;
        std::int32_t area = 0;
        bool operator==(const EnchantingEffectChoice&) const = default;
    };

    /// Client → server semantic enchanting request. Contains only player
    /// choices: the expected inventory revision, the exact target/soul-gem
    /// inventory instances, the cast style, the custom item name, and the
    /// selected effects. No calculated result, record, or statistic is
    /// accepted from clients.
    struct EnchantingRequest
    {
        std::uint16_t protocolVersion = CurrentEnchantingProtocolVersion;
        std::string requestId;
        std::uint64_t inventoryRevision = 0;
        std::uint32_t targetInstanceId = 0;
        std::uint32_t soulGemInstanceId = 0;
        std::int32_t castStyle = 0;
        std::string itemName;
        bool selfEnchanting = true;
        std::uint64_t enchanterNetId = 0; // server-issued actor net id for paid services
        std::vector<EnchantingEffectChoice> effects;
        bool operator==(const EnchantingRequest&) const = default;
    };

    /// Server → client terminal enchanting result. `accepted` means the
    /// request was processed and its gameplay mutations committed atomically
    /// (a failed self-enchant roll still consumes the soul gem per native
    /// semantics). Rejected requests carry a machine-readable `error` and
    /// mutate nothing.
    struct EnchantingResult
    {
        std::uint16_t protocolVersion = CurrentEnchantingProtocolVersion;
        std::string requestId;
        bool accepted = false;
        EnchantingError error = EnchantingError::None;
        bool success = false; // the enchantment attempt itself (roll/paid)
        std::uint64_t inventoryRevision = 0;
        std::uint64_t commitSequence = 0;
        std::string enchantmentRecordId; // canonical; empty on failed attempt
        std::string itemRecordId;        // canonical; empty on failed attempt
        bool enchantmentReused = false;
        bool itemReused = false;
        bool operator==(const EnchantingResult&) const = default;
    };

    std::string_view getEnchantingErrorCode(EnchantingError error);
}

#endif
