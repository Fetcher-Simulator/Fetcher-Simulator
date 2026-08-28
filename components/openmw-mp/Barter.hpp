#ifndef OPENMW_MP_BARTER_HPP
#define OPENMW_MP_BARTER_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "InventoryTake.hpp"
#include "WorldItemTake.hpp"

namespace ESM
{
    struct InventoryList;
    struct ItemLevList;
}

namespace mwmp
{
    inline constexpr std::uint16_t BarterProtocolVersion = 1;
    inline constexpr std::size_t MaximumBarterLines = 64;
    inline constexpr std::size_t MaximumBarterStringLength = MaximumInventoryTakeStringLength;
    inline constexpr std::size_t MaximumBarterRequestIdLength = MaximumInventoryTakeRequestIdLength;
    inline constexpr std::int32_t MaximumBarterCount = MaximumInventoryTakeCount;

    enum class BarterLineKind : std::uint8_t
    {
        BuyFinite = 1,
        BuyRestocking = 2,
        Sell = 3,
        BuyWorldItem = 4,
    };

    struct BarterLine
    {
        BarterLineKind kind = BarterLineKind::BuyFinite;
        // Buy lines name the exact merchant/container source. Sell lines leave
        // this identity empty and instead bind to the player's stack instance.
        InventorySourceIdentity source;
        // Populated only by BuyWorldItem. Loose world stock belongs to the
        // placed-object/tombstone domain, never to world_container_items.
        PlacedObjectIdentity worldObject;
        std::string itemRefId;
        std::uint32_t itemInstanceId = 0;
        std::int32_t itemCharge = -1;
        float itemEnchantmentCharge = -1.f;
        std::string itemSoul;
        std::int32_t count = 0;

        bool operator==(const BarterLine&) const = default;
    };

    enum class BarterError : std::uint16_t
    {
        None = 0,
        InvalidRequest,
        UnsupportedVersion,
        WrongCell,
        PlayerSnapshotUnavailable,
        SourceUnavailable,
        StaleSource,
        ItemUnavailable,
        InvalidCount,
        InvalidBalance,
        InsufficientGold,
        MerchantGoldInsufficient,
        OutOfRange,
        StaleInventoryRevision,
        DuplicateConflict,
        PersistenceFailure,
        WorldItemUnavailable,
    };

    struct BarterRequest
    {
        std::uint16_t protocolVersion = BarterProtocolVersion;
        std::string requestId;
        InventorySourceIdentity merchant;
        std::vector<BarterLine> lines;
        // InventoryExtender's final signed balance. Negative means the player
        // pays the merchant; positive means the merchant pays the player.
        std::int32_t balance = 0;
        // Client-visible merchant barter gold before the offer. It is retained
        // in the canonical request for diagnostics and replay identity only;
        // settlement uses the server's durable merchant-gold state.
        std::int32_t merchantGold = 0;
        std::uint64_t expectedInventoryRevision = 0;

        bool operator==(const BarterRequest&) const = default;
    };

    struct BarterResult
    {
        std::uint16_t protocolVersion = BarterProtocolVersion;
        std::string requestId;
        bool accepted = false;
        bool replayed = false;
        BarterError error = BarterError::None;
        std::uint64_t inventoryRevision = 0;
        std::int32_t balance = 0;
        std::int32_t merchantGold = 0;
        std::uint16_t buyLines = 0;
        std::uint16_t sellLines = 0;
        std::vector<InventorySourceIdentity> missingSources;

        bool operator==(const BarterResult&) const = default;
    };

    struct BarterMerchantGoldState
    {
        std::int32_t gold = 0;
        double lastRestockTime = 0.0;
    };

    struct BarterMerchantGoldResolution
    {
        std::int32_t authoritativeGold = 0;
        std::int32_t expectedGold = 0;
        double expectedRestockTime = 0.0;
        double resultingRestockTime = 0.0;
        bool hadStoredState = false;
        bool resetApplied = false;
    };

    BarterMerchantGoldResolution resolveBarterMerchantGold(std::int32_t baseGold,
        std::optional<BarterMerchantGoldState> stored, double currentGameHours, double resetDelayHours);
    BarterError validateBarterRequest(const BarterRequest& request);
    bool isCanonicalBarterSourceIdentity(const InventorySourceIdentity& identity);
    std::string canonicalBarterRequest(const BarterRequest& request);
    std::string_view getBarterErrorCode(BarterError error);
    bool isEligibleBarterRestockDescendant(const ESM::ItemLevList& root,
        std::string_view concreteItemRefId, int playerLevel,
        const std::function<const ESM::ItemLevList*(std::string_view)>& findList);
    bool isBarterRestockingTemplate(const ESM::InventoryList& inventory,
        std::string_view concreteItemRefId, int count, int playerLevel,
        const std::function<const ESM::ItemLevList*(std::string_view)>& findList);
}

#endif
