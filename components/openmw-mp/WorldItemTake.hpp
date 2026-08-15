#ifndef OPENMW_MP_WORLDITEMTAKE_HPP
#define OPENMW_MP_WORLDITEMTAKE_HPP

#include <cstdint>
#include <string>
#include <string_view>

namespace mwmp
{
    inline constexpr std::uint16_t WorldItemTakeProtocolVersion = 1;
    inline constexpr std::size_t MaximumWorldItemTakeRequestIdLength = 128;
    inline constexpr std::size_t MaximumWorldItemTakeCellIdLength = 255;
    inline constexpr std::size_t MaximumWorldItemTakeRefIdLength = 255;
    inline constexpr std::int32_t MaximumWorldItemTakeCount = 1000000;

    enum class PlacedObjectKind : std::uint8_t
    {
        ContentReference = 1,
        ServerPlaced = 2,
    };

    struct PlacedObjectIdentity
    {
        PlacedObjectKind kind = PlacedObjectKind::ContentReference;
        std::string cellId;
        std::string refId;
        std::uint32_t refIndex = 0;
        std::int32_t refContentFile = -1;
        std::uint32_t mpNum = 0;

        bool operator==(const PlacedObjectIdentity&) const = default;
    };

    enum class WorldItemTakeError : std::uint16_t
    {
        None = 0,
        InvalidRequest,
        UnsupportedVersion,
        WrongCell,
        PlayerSnapshotUnavailable,
        UnknownObject,
        ObjectUnavailable,
        InvalidCount,
        OutOfRange,
        StaleInventoryRevision,
        DuplicateConflict,
        PersistenceFailure,
    };

    struct WorldItemTakeRequest
    {
        std::uint16_t protocolVersion = WorldItemTakeProtocolVersion;
        std::string requestId;
        PlacedObjectIdentity object;
        std::int32_t requestedCount = 0;
        std::uint64_t expectedInventoryRevision = 0;

        bool operator==(const WorldItemTakeRequest&) const = default;
    };

    struct WorldItemTakeResult
    {
        std::uint16_t protocolVersion = WorldItemTakeProtocolVersion;
        std::string requestId;
        bool accepted = false;
        bool replayed = false;
        WorldItemTakeError error = WorldItemTakeError::None;
        PlacedObjectIdentity object;
        std::string itemRefId;
        std::int32_t itemCount = 0;
        std::int64_t crimeValue = 0;
        bool theft = false;
        std::uint64_t inventoryRevision = 0;

        bool operator==(const WorldItemTakeResult&) const = default;
    };

    bool isCanonicalPlacedObjectIdentity(const PlacedObjectIdentity& identity);
    WorldItemTakeError validateWorldItemTakeRequest(const WorldItemTakeRequest& request);
    std::string canonicalWorldItemTakeRequest(const WorldItemTakeRequest& request);
    std::string_view getWorldItemTakeErrorCode(WorldItemTakeError error);
}

#endif
