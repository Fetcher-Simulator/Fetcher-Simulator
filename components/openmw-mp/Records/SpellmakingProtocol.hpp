#ifndef OPENMW_MP_SPELLMAKING_PROTOCOL_HPP
#define OPENMW_MP_SPELLMAKING_PROTOCOL_HPP

#include "DynamicRecordTypes.hpp"

namespace mwmp::records
{
    inline constexpr std::uint16_t SpellmakingProtocolVersion = 1;
    inline constexpr std::size_t MaxSpellmakingEffects = 8;
    enum class SpellmakingError : std::uint16_t
    {
        None,
        InvalidRequest,
        UnsupportedProtocol,
        DuplicateRequestConflict,
        RequestPending,
        StaleInventoryRevision,
        StaleSpellbookRevision,
        InvalidService,
        UnknownEffect,
        InvalidEffect,
        InsufficientGold,
        PriceChanged,
        AlreadyKnown,
        TooManySpells,
        RateLimited,
        QuotaExceeded,
        ServerError
    };

    struct SpellmakingRequest
    {
        std::uint16_t protocolVersion = SpellmakingProtocolVersion;
        std::string requestId;
        std::uint64_t inventoryRevision = 0;
        std::uint64_t spellbookRevision = 0;
        std::uint64_t actorNetId = 0;
        std::string name;
        std::int32_t maximumPrice = 0;
        std::vector<MagicEffect> effects;
        bool operator==(const SpellmakingRequest&) const = default;
    };

    struct SpellmakingResult
    {
        std::uint16_t protocolVersion = SpellmakingProtocolVersion;
        std::string requestId;
        bool accepted = false;
        SpellmakingError error = SpellmakingError::None;
        std::uint64_t inventoryRevision = 0;
        std::uint64_t spellbookRevision = 0;
        std::uint64_t commitSequence = 0;
        std::string recordId;
        std::int32_t price = 0;
        bool operator==(const SpellmakingResult&) const = default;
    };
}
#endif
