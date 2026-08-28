#include "Barter.hpp"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <limits>
#include <unordered_set>

#include <components/esm3/loadcont.hpp>
#include <components/esm3/loadlevlist.hpp>

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

    bool emptyIdentity(const mwmp::InventorySourceIdentity& identity)
    {
        return identity.cellId.empty() && identity.refId.empty() && identity.refNum == 0
            && identity.mpNum == 0 && identity.actorInstanceId == 0
            && identity.migrationGeneration == 0;
    }

    bool emptyWorldIdentity(const mwmp::PlacedObjectIdentity& identity)
    {
        return identity.cellId.empty() && identity.refId.empty() && identity.refIndex == 0
            && identity.refContentFile == -1 && identity.mpNum == 0;
    }

    bool validIdentityShape(const mwmp::InventorySourceIdentity& identity, bool actor)
    {
        const bool hasRefNum = identity.refNum != 0;
        const bool hasMpNum = identity.mpNum != 0;
        if (hasRefNum == hasMpNum || actor != (identity.actorInstanceId != 0)
            || (actor && identity.migrationGeneration == 0)
            || (!actor && identity.migrationGeneration != 0))
            return false;
        if (!actor)
            return true;
        const mwmp::ActorInstanceKey key = mwmp::unpackActorInstanceId(identity.actorInstanceId);
        return (hasRefNum && key.kind == mwmp::ActorKeyKind::VanillaRefNum && key.id == identity.refNum)
            || (hasMpNum && key.kind == mwmp::ActorKeyKind::SpawnedMpNum && key.id == identity.mpNum);
    }

    void appendIdentity(std::string& bytes, const mwmp::InventorySourceIdentity& identity)
    {
        appendString(bytes, identity.cellId);
        appendString(bytes, identity.refId);
        appendU32(bytes, identity.refNum);
        appendU32(bytes, identity.mpNum);
        appendU64(bytes, identity.actorInstanceId);
        appendU32(bytes, identity.migrationGeneration);
    }

    void appendWorldIdentity(std::string& bytes, const mwmp::PlacedObjectIdentity& identity)
    {
        bytes.push_back(static_cast<char>(identity.kind));
        appendString(bytes, identity.cellId);
        appendString(bytes, identity.refId);
        appendU32(bytes, identity.refIndex);
        appendU32(bytes, std::bit_cast<std::uint32_t>(identity.refContentFile));
        appendU32(bytes, identity.mpNum);
    }
}

bool mwmp::isCanonicalBarterSourceIdentity(const InventorySourceIdentity& identity)
{
    const bool actor = identity.actorInstanceId != 0;
    return validString(identity.cellId, MaximumBarterStringLength)
        && validString(identity.refId, MaximumBarterStringLength)
        && validIdentityShape(identity, actor);
}

mwmp::BarterError mwmp::validateBarterRequest(const BarterRequest& request)
{
    if (request.protocolVersion != BarterProtocolVersion)
        return BarterError::UnsupportedVersion;
    if (!validString(request.requestId, MaximumBarterRequestIdLength)
        || !validString(request.merchant.cellId, MaximumBarterStringLength)
        || !validString(request.merchant.refId, MaximumBarterStringLength)
        || !validIdentityShape(request.merchant, true))
        return BarterError::InvalidRequest;
    if (request.lines.empty() || request.lines.size() > MaximumBarterLines || request.merchantGold < 0)
        return BarterError::InvalidRequest;

    bool hasBuy = false;
    bool hasSell = false;
    std::vector<mwmp::PlacedObjectIdentity> worldObjects;
    for (const BarterLine& line : request.lines)
    {
        if (line.kind != BarterLineKind::BuyFinite && line.kind != BarterLineKind::BuyRestocking
            && line.kind != BarterLineKind::Sell && line.kind != BarterLineKind::BuyWorldItem)
            return BarterError::InvalidRequest;
        if (!validString(line.itemRefId, MaximumBarterStringLength)
            || !validString(line.itemSoul, MaximumBarterStringLength, true)
            || !std::isfinite(line.itemEnchantmentCharge))
            return BarterError::InvalidRequest;
        if (line.count <= 0 || line.count > MaximumBarterCount)
            return BarterError::InvalidCount;

        if (line.kind == BarterLineKind::Sell)
        {
            hasSell = true;
            if (!emptyIdentity(line.source) || !emptyWorldIdentity(line.worldObject)
                || line.itemInstanceId == 0)
                return BarterError::InvalidRequest;
        }
        else if (line.kind == BarterLineKind::BuyWorldItem)
        {
            hasBuy = true;
            if (!emptyIdentity(line.source) || !isCanonicalPlacedObjectIdentity(line.worldObject)
                || line.worldObject.refId != line.itemRefId || line.itemInstanceId != 0)
                return BarterError::InvalidRequest;
            if (std::find(worldObjects.begin(), worldObjects.end(), line.worldObject) != worldObjects.end())
                return BarterError::InvalidRequest;
            worldObjects.push_back(line.worldObject);
        }
        else
        {
            hasBuy = true;
            if (!isCanonicalBarterSourceIdentity(line.source)
                || !emptyWorldIdentity(line.worldObject))
                return BarterError::InvalidRequest;
        }
    }

    if (!hasBuy && !hasSell)
        return BarterError::InvalidRequest;
    return BarterError::None;
}

std::string mwmp::canonicalBarterRequest(const BarterRequest& request)
{
    std::string bytes("OMBT", 4);
    bytes.push_back(static_cast<char>(request.protocolVersion));
    bytes.push_back(static_cast<char>(request.protocolVersion >> 8));
    appendString(bytes, request.requestId);
    appendIdentity(bytes, request.merchant);
    appendU32(bytes, static_cast<std::uint32_t>(request.lines.size()));
    for (const BarterLine& line : request.lines)
    {
        bytes.push_back(static_cast<char>(line.kind));
        appendIdentity(bytes, line.source);
        appendWorldIdentity(bytes, line.worldObject);
        appendString(bytes, line.itemRefId);
        appendU32(bytes, line.itemInstanceId);
        appendU32(bytes, std::bit_cast<std::uint32_t>(line.itemCharge));
        appendU32(bytes, std::bit_cast<std::uint32_t>(line.itemEnchantmentCharge));
        appendString(bytes, line.itemSoul);
        appendU32(bytes, std::bit_cast<std::uint32_t>(line.count));
    }
    appendU32(bytes, std::bit_cast<std::uint32_t>(request.balance));
    appendU32(bytes, std::bit_cast<std::uint32_t>(request.merchantGold));
    appendU64(bytes, request.expectedInventoryRevision);
    return bytes;
}

std::string_view mwmp::getBarterErrorCode(BarterError error)
{
    switch (error)
    {
        case BarterError::None: return "none";
        case BarterError::InvalidRequest: return "invalid_request";
        case BarterError::UnsupportedVersion: return "unsupported_version";
        case BarterError::WrongCell: return "wrong_cell";
        case BarterError::PlayerSnapshotUnavailable: return "player_snapshot_unavailable";
        case BarterError::SourceUnavailable: return "source_unavailable";
        case BarterError::StaleSource: return "stale_source";
        case BarterError::ItemUnavailable: return "item_unavailable";
        case BarterError::InvalidCount: return "invalid_count";
        case BarterError::InvalidBalance: return "invalid_balance";
        case BarterError::InsufficientGold: return "insufficient_gold";
        case BarterError::MerchantGoldInsufficient: return "merchant_gold_insufficient";
        case BarterError::OutOfRange: return "out_of_range";
        case BarterError::StaleInventoryRevision: return "stale_inventory_revision";
        case BarterError::DuplicateConflict: return "duplicate_conflict";
        case BarterError::PersistenceFailure: return "persistence_failure";
        case BarterError::WorldItemUnavailable: return "world_item_unavailable";
    }
    return "unknown_error";
}

mwmp::BarterMerchantGoldResolution mwmp::resolveBarterMerchantGold(std::int32_t baseGold,
    std::optional<BarterMerchantGoldState> stored, double currentGameHours, double resetDelayHours)
{
    BarterMerchantGoldResolution result;
    result.authoritativeGold = std::max<std::int32_t>(0, baseGold);
    result.expectedGold = result.authoritativeGold;
    result.expectedRestockTime = 0.0;
    result.resultingRestockTime = currentGameHours;
    result.hadStoredState = stored.has_value();

    if (!stored)
    {
        result.resetApplied = true;
        return result;
    }

    result.authoritativeGold = std::max<std::int32_t>(0, stored->gold);
    result.expectedGold = result.authoritativeGold;
    result.expectedRestockTime = stored->lastRestockTime;
    result.resultingRestockTime = stored->lastRestockTime;

    const double delay = std::max(0.0, resetDelayHours);
    // The dedicated server's authoritative world clock is currently session-local.
    // If it moved backwards across a server restart, treat that as a restock boundary
    // instead of pinning a merchant to a future timestamp indefinitely.
    if (currentGameHours < stored->lastRestockTime
        || currentGameHours >= stored->lastRestockTime + delay)
    {
        result.authoritativeGold = std::max<std::int32_t>(0, baseGold);
        result.resultingRestockTime = currentGameHours;
        result.resetApplied = true;
    }
    return result;
}

bool mwmp::isEligibleBarterRestockDescendant(const ESM::ItemLevList& root,
    std::string_view concreteItemRefId, int playerLevel,
    const std::function<const ESM::ItemLevList*(std::string_view)>& findList)
{
    if (concreteItemRefId.empty() || playerLevel < 1 || !findList)
        return false;
    const auto lower = [](std::string_view value) {
        std::string result(value);
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return result;
    };
    const std::string concrete = lower(concreteItemRefId);
    std::unordered_set<std::string> visiting;
    std::function<bool(const ESM::ItemLevList&, unsigned)> visit
        = [&](const ESM::ItemLevList& list, unsigned depth) {
            if (depth > 32)
                return false;
            const std::string listId = lower(list.mId.serializeText());
            if (!visiting.insert(listId).second)
                return false;

            int highestLevel = 0;
            for (const auto& entry : list.mList)
            {
                if (entry.mLevel <= playerLevel && entry.mLevel > highestLevel)
                    highestLevel = entry.mLevel;
            }
            const bool allLevels = (list.mFlags & ESM::ItemLevList::AllLevels) != 0;
            bool found = false;
            for (const auto& entry : list.mList)
            {
                if (entry.mLevel > playerLevel || (!allLevels && entry.mLevel != highestLevel))
                    continue;
                const std::string entryId = entry.mId.serializeText();
                if (lower(entryId) == concrete)
                {
                    found = true;
                    break;
                }
                if (const ESM::ItemLevList* nested = findList(entryId))
                {
                    if (visit(*nested, depth + 1))
                    {
                        found = true;
                        break;
                    }
                }
            }
            visiting.erase(listId);
            return found;
        };
    return visit(root, 0);
}

bool mwmp::isBarterRestockingTemplate(const ESM::InventoryList& inventory,
    std::string_view concreteItemRefId, int count, int playerLevel,
    const std::function<const ESM::ItemLevList*(std::string_view)>& findList)
{
    if (concreteItemRefId.empty() || count <= 0 || playerLevel < 1)
        return false;

    const auto lower = [](std::string_view value) {
        std::string result(value);
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return result;
    };
    const std::string concrete = lower(concreteItemRefId);
    const auto matches = [&](const ESM::ContItem& item) {
        if (lower(item.mItem.serializeText()) == concrete)
            return true;
        if (!findList)
            return false;
        const ESM::ItemLevList* list = findList(item.mItem.serializeText());
        return list != nullptr && isEligibleBarterRestockDescendant(
            *list, concreteItemRefId, playerLevel, findList);
    };

    std::int64_t negativeCapacity = 0;
    bool hasPositiveTemplate = false;
    for (const ESM::ContItem& item : inventory.mList)
    {
        if (item.mCount == 0 || !matches(item))
            continue;
        if (item.mCount < 0)
            negativeCapacity += std::abs(static_cast<std::int64_t>(item.mCount));
        else
            hasPositiveTemplate = true;
    }

    return !hasPositiveTemplate && negativeCapacity >= count;
}
