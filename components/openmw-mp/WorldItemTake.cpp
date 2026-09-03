#include "WorldItemTake.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <type_traits>

namespace
{
    template <class T>
    void appendInteger(std::string& output, T value)
    {
        using U = std::make_unsigned_t<T>;
        U encoded = static_cast<U>(value);
        for (std::size_t index = 0; index < sizeof(T); ++index)
            output.push_back(static_cast<char>((encoded >> (index * 8)) & 0xffu));
    }

    void appendString(std::string& output, std::string_view value)
    {
        appendInteger<std::uint32_t>(output, static_cast<std::uint32_t>(value.size()));
        output.append(value);
    }
}

bool mwmp::isCanonicalPlacedObjectIdentity(const PlacedObjectIdentity& identity)
{
    if (identity.cellId.empty() || identity.cellId.size() > MaximumWorldItemTakeCellIdLength
        || identity.refId.empty() || identity.refId.size() > MaximumWorldItemTakeRefIdLength)
        return false;

    if (identity.kind == PlacedObjectKind::ContentReference)
        return identity.refIndex != 0 && identity.refContentFile >= 0 && identity.mpNum == 0;
    if (identity.kind == PlacedObjectKind::ServerPlaced)
        return identity.refIndex == 0 && identity.refContentFile == -1 && identity.mpNum != 0;
    return false;
}

bool mwmp::containsRetiredServerPlacedMpNum(
    const std::vector<PlacedObjectIdentity>& identities, std::uint32_t mpNum)
{
    if (mpNum == 0)
        return false;
    return std::any_of(identities.begin(), identities.end(), [&](const PlacedObjectIdentity& identity) {
        return identity.kind == PlacedObjectKind::ServerPlaced && identity.mpNum == mpNum;
    });
}

mwmp::WorldItemTakeError mwmp::validateWorldItemTakeRequest(const WorldItemTakeRequest& request)
{
    if (request.protocolVersion != WorldItemTakeProtocolVersion)
        return WorldItemTakeError::UnsupportedVersion;
    if (request.requestId.empty() || request.requestId.size() > MaximumWorldItemTakeRequestIdLength
        || !isCanonicalPlacedObjectIdentity(request.object))
        return WorldItemTakeError::InvalidRequest;
    if (request.requestedCount <= 0 || request.requestedCount > MaximumWorldItemTakeCount)
        return WorldItemTakeError::InvalidCount;
    if (!isInventoryTransferSoundDirection(request.soundDirection))
        return WorldItemTakeError::InvalidRequest;
    return WorldItemTakeError::None;
}

std::string mwmp::canonicalWorldItemTakeRequest(const WorldItemTakeRequest& request)
{
    std::string output;
    output.reserve(64 + request.requestId.size() + request.object.cellId.size() + request.object.refId.size());
    appendInteger(output, request.protocolVersion);
    appendString(output, request.requestId);
    appendInteger(output, static_cast<std::uint8_t>(request.object.kind));
    appendString(output, request.object.cellId);
    appendString(output, request.object.refId);
    appendInteger(output, request.object.refIndex);
    appendInteger(output, request.object.refContentFile);
    appendInteger(output, request.object.mpNum);
    appendInteger(output, request.requestedCount);
    appendInteger(output, request.expectedInventoryRevision);
    appendInteger(output, static_cast<std::uint8_t>(request.soundDirection));
    return output;
}

std::string_view mwmp::getWorldItemTakeErrorCode(WorldItemTakeError error)
{
    switch (error)
    {
        case WorldItemTakeError::None: return "none";
        case WorldItemTakeError::InvalidRequest: return "invalid_request";
        case WorldItemTakeError::UnsupportedVersion: return "unsupported_version";
        case WorldItemTakeError::WrongCell: return "wrong_cell";
        case WorldItemTakeError::PlayerSnapshotUnavailable: return "player_snapshot_unavailable";
        case WorldItemTakeError::UnknownObject: return "unknown_object";
        case WorldItemTakeError::ObjectUnavailable: return "object_unavailable";
        case WorldItemTakeError::InvalidCount: return "invalid_count";
        case WorldItemTakeError::OutOfRange: return "out_of_range";
        case WorldItemTakeError::StaleInventoryRevision: return "stale_inventory_revision";
        case WorldItemTakeError::DuplicateConflict: return "duplicate_conflict";
        case WorldItemTakeError::PersistenceFailure: return "persistence_failure";
    }
    return "unknown";
}
