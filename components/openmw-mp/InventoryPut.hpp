#ifndef OPENMW_MP_INVENTORYPUT_HPP
#define OPENMW_MP_INVENTORYPUT_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "InventoryTake.hpp"

namespace mwmp
{
    inline constexpr std::uint16_t InventoryPutProtocolVersion = 1;
    inline constexpr std::size_t MaximumInventoryPutStringLength = 255;
    inline constexpr std::size_t MaximumInventoryPutRequestIdLength = 128;
    inline constexpr std::int32_t MaximumInventoryPutCount = 1000000;

    enum class InventoryPutError : std::uint16_t
    {
        None = 0,
        InvalidRequest,
        UnsupportedVersion,
        WrongCell,
        PlayerSnapshotUnavailable,
        DestinationUnavailable,
        StaleDestination,
        ItemUnavailable,
        InvalidCount,
        OutOfRange,
        StaleInventoryRevision,
        DuplicateConflict,
        PersistenceFailure,
    };

    struct InventoryPutRequest
    {
        std::uint16_t protocolVersion = InventoryPutProtocolVersion;
        std::string requestId;
        InventorySourceIdentity destination;
        std::string itemRefId;
        std::uint32_t itemInstanceId = 0;
        std::int32_t itemCharge = -1;
        std::int32_t requestedCount = 0;
        std::uint64_t expectedInventoryRevision = 0;

        bool operator==(const InventoryPutRequest&) const = default;
    };

    struct InventoryPutResult
    {
        std::uint16_t protocolVersion = InventoryPutProtocolVersion;
        std::string requestId;
        bool accepted = false;
        bool replayed = false;
        InventoryPutError error = InventoryPutError::None;
        InventorySourceIdentity destination;
        std::string itemRefId;
        std::uint32_t itemInstanceId = 0;
        std::int32_t itemCharge = -1;
        std::int32_t itemCount = 0;
        std::uint64_t inventoryRevision = 0;

        bool operator==(const InventoryPutResult&) const = default;
    };

    InventoryPutError validateInventoryPutRequest(const InventoryPutRequest& request);
    std::string canonicalInventoryPutRequest(const InventoryPutRequest& request);
    std::string_view getInventoryPutErrorCode(InventoryPutError error);
}

#endif
