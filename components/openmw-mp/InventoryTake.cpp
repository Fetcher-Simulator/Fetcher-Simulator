#include "InventoryTake.hpp"

#include <algorithm>
#include <cmath>
#include <bit>
#include <limits>

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

    bool validString(std::string_view value, std::size_t maximum, bool allowEmpty = false)
    {
        if ((!allowEmpty && value.empty()) || value.size() > maximum)
            return false;
        for (unsigned char c : value)
        {
            if (c == 0 || c < 0x20 || c == 0x7f)
                return false;
        }
        return true;
    }
}

mwmp::InventoryTakeError mwmp::validateInventoryTakeRequest(const InventoryTakeRequest& request)
{
    if (request.protocolVersion != InventoryTakeProtocolVersion)
        return InventoryTakeError::UnsupportedVersion;
    if (!validString(request.requestId, MaximumInventoryTakeRequestIdLength)
        || !validString(request.source.cellId, MaximumInventoryTakeStringLength)
        || !validString(request.source.refId, MaximumInventoryTakeStringLength))
        return InventoryTakeError::InvalidRequest;
    if (request.kind != InventoryTakeKind::Container && request.kind != InventoryTakeKind::Corpse
        && request.kind != InventoryTakeKind::Pickpocket
        && request.kind != InventoryTakeKind::ActorInventory
        && request.kind != InventoryTakeKind::PickpocketFinish
        && request.kind != InventoryTakeKind::Barter)
        return InventoryTakeError::InvalidRequest;

    const auto validIdentityShape = [](const InventorySourceIdentity& identity, bool actor) {
        const bool hasRefNum = identity.refNum != 0;
        const bool hasMpNum = identity.mpNum != 0;
        if (hasRefNum == hasMpNum || actor != (identity.actorInstanceId != 0)
            || (actor && identity.migrationGeneration == 0)
            || (!actor && identity.migrationGeneration != 0))
            return false;
        if (!actor)
            return true;
        const ActorInstanceKey key = unpackActorInstanceId(identity.actorInstanceId);
        return (hasRefNum && key.kind == ActorKeyKind::VanillaRefNum && key.id == identity.refNum)
            || (hasMpNum && key.kind == ActorKeyKind::SpawnedMpNum && key.id == identity.mpNum);
    };
    const auto emptyIdentity = [](const InventorySourceIdentity& identity) {
        return identity.cellId.empty() && identity.refId.empty() && identity.refNum == 0
            && identity.mpNum == 0 && identity.actorInstanceId == 0
            && identity.migrationGeneration == 0;
    };

    const bool sourceIsActor = request.source.actorInstanceId != 0;
    const bool actorSourceRequired = request.kind == InventoryTakeKind::Corpse
        || request.kind == InventoryTakeKind::Pickpocket
        || request.kind == InventoryTakeKind::ActorInventory
        || request.kind == InventoryTakeKind::PickpocketFinish;
    if ((request.kind == InventoryTakeKind::Container && sourceIsActor)
        || (actorSourceRequired && !sourceIsActor)
        || !validIdentityShape(request.source, sourceIsActor))
        return InventoryTakeError::InvalidRequest;

    if (request.kind == InventoryTakeKind::Barter)
    {
        if (!validString(request.merchant.cellId, MaximumInventoryTakeStringLength)
            || !validString(request.merchant.refId, MaximumInventoryTakeStringLength)
            || !validIdentityShape(request.merchant, true))
            return InventoryTakeError::InvalidRequest;
        if (request.barterPrice <= 0)
            return InventoryTakeError::InvalidPrice;
    }
    else if (!emptyIdentity(request.merchant) || request.barterPrice != 0)
        return InventoryTakeError::InvalidRequest;

    const bool finish = request.kind == InventoryTakeKind::PickpocketFinish;
    if (finish && (!request.itemRefId.empty() || request.itemCharge != -1
            || request.itemEnchantmentCharge != -1.f || !request.itemSoul.empty()
            || request.requestedCount != 0))
        return InventoryTakeError::InvalidRequest;
    if (!finish && (!validString(request.itemRefId, MaximumInventoryTakeStringLength)
            || !validString(request.itemSoul, MaximumInventoryTakeStringLength, true)
            || !std::isfinite(request.itemEnchantmentCharge)
            || request.requestedCount <= 0 || request.requestedCount > MaximumInventoryTakeCount))
        return InventoryTakeError::InvalidCount;
    return InventoryTakeError::None;
}

std::string mwmp::canonicalInventoryTakeRequest(const InventoryTakeRequest& request)
{
    std::string bytes("OMIT", 4);
    bytes.push_back(static_cast<char>(request.protocolVersion));
    bytes.push_back(static_cast<char>(request.protocolVersion >> 8));
    appendString(bytes, request.requestId);
    bytes.push_back(static_cast<char>(request.kind));
    appendString(bytes, request.source.cellId);
    appendString(bytes, request.source.refId);
    appendU32(bytes, request.source.refNum);
    appendU32(bytes, request.source.mpNum);
    appendU64(bytes, request.source.actorInstanceId);
    appendU32(bytes, request.source.migrationGeneration);
    appendString(bytes, request.merchant.cellId);
    appendString(bytes, request.merchant.refId);
    appendU32(bytes, request.merchant.refNum);
    appendU32(bytes, request.merchant.mpNum);
    appendU64(bytes, request.merchant.actorInstanceId);
    appendU32(bytes, request.merchant.migrationGeneration);
    appendString(bytes, request.itemRefId);
    appendU32(bytes, std::bit_cast<std::uint32_t>(request.itemCharge));
    appendU32(bytes, std::bit_cast<std::uint32_t>(request.itemEnchantmentCharge));
    appendString(bytes, request.itemSoul);
    appendU32(bytes, std::bit_cast<std::uint32_t>(request.requestedCount));
    appendU32(bytes, std::bit_cast<std::uint32_t>(request.barterPrice));
    appendU64(bytes, request.expectedInventoryRevision);
    return bytes;
}

std::string_view mwmp::getInventoryTakeErrorCode(InventoryTakeError error)
{
    switch (error)
    {
        case InventoryTakeError::None: return "none";
        case InventoryTakeError::InvalidRequest: return "invalid_request";
        case InventoryTakeError::UnsupportedVersion: return "unsupported_version";
        case InventoryTakeError::WrongCell: return "wrong_cell";
        case InventoryTakeError::PlayerSnapshotUnavailable: return "player_snapshot_unavailable";
        case InventoryTakeError::SourceUnavailable: return "source_unavailable";
        case InventoryTakeError::StaleSource: return "stale_source";
        case InventoryTakeError::ItemUnavailable: return "item_unavailable";
        case InventoryTakeError::InvalidCount: return "invalid_count";
        case InventoryTakeError::InvalidPrice: return "invalid_price";
        case InventoryTakeError::InsufficientGold: return "insufficient_gold";
        case InventoryTakeError::OutOfRange: return "out_of_range";
        case InventoryTakeError::StaleInventoryRevision: return "stale_inventory_revision";
        case InventoryTakeError::DuplicateConflict: return "duplicate_conflict";
        case InventoryTakeError::PersistenceFailure: return "persistence_failure";
    }
    return "unknown_error";
}

mwmp::PickpocketDetectionResult mwmp::evaluatePickpocketDetection(
    const PickpocketDetectionInput& input)
{
    const bool finite = std::isfinite(input.thiefSneak) && std::isfinite(input.thiefAgility)
        && std::isfinite(input.thiefLuck) && std::isfinite(input.thiefFatigueTerm)
        && std::isfinite(input.victimSneak) && std::isfinite(input.victimAgility)
        && std::isfinite(input.victimLuck) && std::isfinite(input.victimFatigueTerm)
        && std::isfinite(input.valueTerm);
    if (!finite || input.minimumChanceDivisor <= 0 || input.maximumChance < 0
        || input.roll0To99 < 0 || input.roll0To99 > 99)
        return {};

    const float thief = (.2f * input.thiefAgility + .1f * input.thiefLuck + input.thiefSneak)
        * input.thiefFatigueTerm;
    const float victim = (input.valueTerm + .2f * input.victimAgility
        + .1f * input.victimLuck + input.victimSneak) * input.victimFatigueTerm;
    float threshold = 2.f * thief - victim;
    const float minimum = input.thiefSneak / input.minimumChanceDivisor;
    threshold = threshold < minimum ? minimum
        : std::min(static_cast<float>(input.maximumChance), threshold);

    PickpocketDetectionResult result;
    result.valid = true;
    result.threshold = static_cast<std::int32_t>(threshold);
    result.detected = input.roll0To99 > result.threshold;
    return result;
}
