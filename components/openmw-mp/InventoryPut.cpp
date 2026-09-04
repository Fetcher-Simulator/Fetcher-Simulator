#include "InventoryPut.hpp"

#include <bit>

namespace
{
    void appendU32(std::string& bytes, std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
            bytes.push_back(static_cast<char>(value >> shift));
    }

    void appendU64(std::string& bytes, std::uint64_t value)
    {
        for (int shift = 0; shift < 64; shift += 8)
            bytes.push_back(static_cast<char>(value >> shift));
    }

    void appendString(std::string& bytes, std::string_view value)
    {
        appendU32(bytes, static_cast<std::uint32_t>(value.size()));
        bytes.append(value);
    }

    bool validString(std::string_view value, std::size_t maximum)
    {
        if (value.empty() || value.size() > maximum)
            return false;
        for (unsigned char c : value)
        {
            if (c == 0 || c < 0x20 || c == 0x7f)
                return false;
        }
        return true;
    }
}

mwmp::InventoryPutError mwmp::validateInventoryPutRequest(const InventoryPutRequest& request)
{
    if (request.protocolVersion != InventoryPutProtocolVersion)
        return InventoryPutError::UnsupportedVersion;
    if (!validString(request.requestId, MaximumInventoryPutRequestIdLength)
        || !validString(request.destination.cellId, MaximumInventoryPutStringLength)
        || !validString(request.destination.refId, MaximumInventoryPutStringLength)
        || !validString(request.itemRefId, MaximumInventoryPutStringLength))
        return InventoryPutError::InvalidRequest;
    const bool hasRefNum = request.destination.refNum != 0;
    const bool hasMpNum = request.destination.mpNum != 0;
    const bool actorDestination = request.destination.actorInstanceId != 0;
    if (hasRefNum == hasMpNum || request.itemInstanceId == 0
        || (actorDestination && request.destination.migrationGeneration == 0)
        || (!actorDestination && request.destination.migrationGeneration != 0))
        return InventoryPutError::InvalidRequest;
    if (actorDestination)
    {
        const ActorInstanceKey key = unpackActorInstanceId(request.destination.actorInstanceId);
        if ((hasRefNum && (key.kind != ActorKeyKind::VanillaRefNum || key.id != request.destination.refNum))
            || (hasMpNum && (key.kind != ActorKeyKind::SpawnedMpNum || key.id != request.destination.mpNum)))
            return InventoryPutError::InvalidRequest;
    }
    if (request.requestedCount <= 0 || request.requestedCount > MaximumInventoryPutCount)
        return InventoryPutError::InvalidCount;
    return InventoryPutError::None;
}

std::string mwmp::canonicalInventoryPutRequest(const InventoryPutRequest& request)
{
    std::string bytes("OMIP", 4);
    bytes.push_back(static_cast<char>(request.protocolVersion));
    bytes.push_back(static_cast<char>(request.protocolVersion >> 8));
    appendString(bytes, request.requestId);
    appendString(bytes, request.destination.cellId);
    appendString(bytes, request.destination.refId);
    appendU32(bytes, request.destination.refNum);
    appendU32(bytes, request.destination.mpNum);
    appendU64(bytes, request.destination.actorInstanceId);
    appendU32(bytes, request.destination.migrationGeneration);
    appendU32(bytes, request.destination.authorityGeneration);
    appendString(bytes, request.itemRefId);
    appendU32(bytes, request.itemInstanceId);
    appendU32(bytes, std::bit_cast<std::uint32_t>(request.itemCharge));
    appendU32(bytes, std::bit_cast<std::uint32_t>(request.requestedCount));
    appendU64(bytes, request.expectedInventoryRevision);
    return bytes;
}

std::string_view mwmp::getInventoryPutErrorCode(InventoryPutError error)
{
    switch (error)
    {
        case InventoryPutError::None: return "none";
        case InventoryPutError::InvalidRequest: return "invalid_request";
        case InventoryPutError::UnsupportedVersion: return "unsupported_version";
        case InventoryPutError::WrongCell: return "wrong_cell";
        case InventoryPutError::PlayerSnapshotUnavailable: return "player_snapshot_unavailable";
        case InventoryPutError::DestinationUnavailable: return "destination_unavailable";
        case InventoryPutError::StaleDestination: return "stale_destination";
        case InventoryPutError::ItemUnavailable: return "item_unavailable";
        case InventoryPutError::InvalidCount: return "invalid_count";
        case InventoryPutError::OutOfRange: return "out_of_range";
        case InventoryPutError::StaleInventoryRevision: return "stale_inventory_revision";
        case InventoryPutError::DuplicateConflict: return "duplicate_conflict";
        case InventoryPutError::PersistenceFailure: return "persistence_failure";
    }
    return "unknown_error";
}
