#ifndef OPENMW_MP_INVENTORYTAKE_HPP
#define OPENMW_MP_INVENTORYTAKE_HPP

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>

#include <components/openmw-mp/Base/ActorSyncProtocol.hpp>

namespace mwmp
{
    inline constexpr std::uint16_t InventoryTakeProtocolVersion = 2;
    inline constexpr std::size_t MaximumInventoryTakeStringLength = 255;
    inline constexpr std::size_t MaximumInventoryTakeRequestIdLength = 128;
    inline constexpr std::int32_t MaximumInventoryTakeCount = 1000000;

    enum class InventoryTakeKind : std::uint8_t
    {
        Container = 1,
        Corpse = 2,
        Pickpocket = 3,
        ActorInventory = 4,
        PickpocketFinish = 5,
    };

    struct InventorySourceIdentity
    {
        std::string cellId;
        std::string refId;
        std::uint32_t refNum = 0;
        std::uint32_t mpNum = 0;
        ActorInstanceId actorInstanceId = 0;
        std::uint32_t migrationGeneration = 0;

        bool operator==(const InventorySourceIdentity&) const = default;
    };

    enum class InventoryTakeError : std::uint16_t
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
        OutOfRange,
        StaleInventoryRevision,
        DuplicateConflict,
        PersistenceFailure,
    };

    struct InventoryTakeRequest
    {
        std::uint16_t protocolVersion = InventoryTakeProtocolVersion;
        std::string requestId;
        InventoryTakeKind kind = InventoryTakeKind::Container;
        InventorySourceIdentity source;
        std::string itemRefId;
        std::int32_t itemCharge = -1;
        float itemEnchantmentCharge = -1.f;
        std::string itemSoul;
        std::int32_t requestedCount = 0;
        std::uint64_t expectedInventoryRevision = 0;

        bool operator==(const InventoryTakeRequest&) const = default;
    };

    struct InventoryTakeResult
    {
        std::uint16_t protocolVersion = InventoryTakeProtocolVersion;
        std::string requestId;
        bool accepted = false;
        bool replayed = false;
        InventoryTakeError error = InventoryTakeError::None;
        InventoryTakeKind kind = InventoryTakeKind::Container;
        InventorySourceIdentity source;
        std::string itemRefId;
        std::int32_t itemCharge = -1;
        std::int32_t itemCount = 0;
        std::uint64_t inventoryRevision = 0;
        bool detected = false;
        std::int32_t detectionRoll = -1;
        bool theft = false;
        std::int64_t crimeValue = 0;

        bool operator==(const InventoryTakeResult&) const = default;
    };

    struct PickpocketDetectionInput
    {
        float thiefSneak = 0.f;
        float thiefAgility = 0.f;
        float thiefLuck = 0.f;
        float thiefFatigueTerm = 0.f;
        float victimSneak = 0.f;
        float victimAgility = 0.f;
        float victimLuck = 0.f;
        float victimFatigueTerm = 0.f;
        float valueTerm = 0.f;
        std::int32_t minimumChanceDivisor = 1;
        std::int32_t maximumChance = 100;
        std::int32_t roll0To99 = 0;
    };

    struct PickpocketDetectionResult
    {
        bool valid = false;
        bool detected = false;
        std::int32_t threshold = 0;
    };

    InventoryTakeError validateInventoryTakeRequest(const InventoryTakeRequest& request);
    std::string canonicalInventoryTakeRequest(const InventoryTakeRequest& request);
    std::string_view getInventoryTakeErrorCode(InventoryTakeError error);
    PickpocketDetectionResult evaluatePickpocketDetection(const PickpocketDetectionInput& input);
}

#endif
