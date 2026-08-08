#ifndef OPENMW_MP_ALCHEMY_PROTOCOL_HPP
#define OPENMW_MP_ALCHEMY_PROTOCOL_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mwmp::records
{
    inline constexpr std::uint16_t CurrentAlchemyProtocolVersion = 1;

    /// Maximum number of ingredient slots native alchemy supports.
    inline constexpr std::size_t MaxAlchemyIngredients = 4;

    /// Maximum number of apparatus slots native alchemy supports.
    inline constexpr std::size_t MaxAlchemyApparatus = 4;

    /// Maximum potion name length accepted from the alchemy UI.
    inline constexpr std::size_t MaxAlchemyPotionNameLength = 64;

    /// Maximum brewing attempts accepted in one semantic request. Native
    /// alchemy caps attempts by the smallest ingredient stack; this bound
    /// only limits hostile request sizes.
    inline constexpr std::uint32_t MaxAlchemyAttempts = 100;

    /// Machine-readable alchemy error codes. The journal persists terminal
    /// outcomes, so codes are stable and never reused with different meaning.
    enum class AlchemyError : std::uint16_t
    {
        None = 0,
        InvalidRequest = 1,
        RequestPending = 2,
        DuplicateRequestConflict = 3,
        StaleInventoryRevision = 4,
        IngredientNotFound = 5,
        IngredientNotOwned = 6,
        InvalidIngredient = 7,
        DuplicateSourceInstance = 8,
        ApparatusNotFound = 9,
        InvalidApparatus = 10,
        ContentMismatch = 11,
        MechanicsValidationFailed = 12,
        ServerError = 13,
        UnsupportedProtocol = 14,
        RateLimited = 15,
        QuotaExceeded = 16,
    };

    /// Client → server semantic alchemy request. Contains only player choices:
    /// the expected inventory revision, the selected inventory instances in
    /// slot order, the requested potion name, and the attempt count. No
    /// calculated result, definition, or statistic is accepted from clients.
    struct AlchemyRequest
    {
        std::uint16_t protocolVersion = CurrentAlchemyProtocolVersion;
        std::string requestId;
        std::uint64_t inventoryRevision = 0;
        std::string potionName;
        std::uint32_t count = 1;
        std::vector<std::uint32_t> ingredientInstanceIds;
        std::vector<std::uint32_t> apparatusInstanceIds;
        bool operator==(const AlchemyRequest&) const = default;
    };

    /// One brewing attempt of a committed request.
    struct AlchemyAttemptResult
    {
        bool success = false;
        std::string recordId; // canonical potion record; empty on failure
        bool reused = false;  // true when an equivalent dynamic record was reused
        bool operator==(const AlchemyAttemptResult&) const = default;
    };

    /// Server → client terminal alchemy result. `accepted` means the request
    /// was processed and its gameplay mutations committed atomically (failed
    /// attempts still consume ingredients per native semantics). Rejected
    /// requests carry a machine-readable `error` and mutate nothing.
    struct AlchemyResult
    {
        std::uint16_t protocolVersion = CurrentAlchemyProtocolVersion;
        std::string requestId;
        bool accepted = false;
        AlchemyError error = AlchemyError::None;
        std::uint64_t inventoryRevision = 0;
        std::uint64_t commitSequence = 0;
        std::vector<AlchemyAttemptResult> attempts;
        bool operator==(const AlchemyResult&) const = default;
    };

    std::string_view getAlchemyErrorCode(AlchemyError error);
}

#endif
