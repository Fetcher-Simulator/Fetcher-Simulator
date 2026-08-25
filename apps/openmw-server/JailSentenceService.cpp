#include "JailSentenceService.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>

namespace
{
    std::string lowerAscii(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    bool sameEvidenceIdentity(const mwmp::ContainerItem& left, const mwmp::ContainerItem& right)
    {
        if (left.instanceId != 0 || right.instanceId != 0)
            return left.instanceId != 0 && left.instanceId == right.instanceId;
        return lowerAscii(left.refId) == lowerAscii(right.refId) && left.charge == right.charge
            && std::abs(left.enchantmentCharge - right.enchantmentCharge) < 0.001f
            && left.soul == right.soul;
    }

    void appendEvidenceItem(std::vector<mwmp::ContainerItem>& items, const mwmp::Item& item)
    {
        mwmp::ContainerItem evidenceItem;
        evidenceItem.refId = item.refId;
        evidenceItem.count = item.count;
        evidenceItem.charge = item.charge;
        evidenceItem.instanceId = item.instanceId;
        evidenceItem.enchantmentCharge = item.enchantmentCharge;
        evidenceItem.soul = item.soul;

        const auto existing = std::find_if(items.begin(), items.end(),
            [&](const mwmp::ContainerItem& value) { return sameEvidenceIdentity(value, evidenceItem); });
        if (existing == items.end())
            items.push_back(std::move(evidenceItem));
        else
            existing->count += evidenceItem.count;
    }
}

mwmp::JailSentencePlan mwmp::JailSentenceService::planConfiscation(const std::vector<Item>& inventory,
    const std::vector<EquipmentItem>& equipment, const std::vector<StolenItemRecord>& stolenItems,
    const ContainerRecord& evidence, const AllocateInstanceId& allocateInstanceId)
{
    JailSentencePlan plan;
    plan.inventory = inventory;
    plan.equipment = equipment;
    plan.evidence = evidence;

    if (evidence.cellId.empty() || evidence.refId.empty() || evidence.refNum == 0
        || evidence.mpNum != 0 || !evidence.hasAuthority)
    {
        plan.error = JailSentencePlanError::EvidenceUnavailable;
        return plan;
    }

    using OwnerKey = std::pair<std::string, bool>;
    std::map<std::string, std::map<OwnerKey, std::int64_t>> remaining;
    for (const StolenItemRecord& record : stolenItems)
    {
        if (record.refId.empty() || record.count <= 0)
        {
            plan.error = JailSentencePlanError::InvalidInput;
            return plan;
        }
        auto& count = remaining[lowerAscii(record.refId)][{ record.ownerId, record.isFaction }];
        if (count > std::numeric_limits<std::int64_t>::max() - record.count)
        {
            plan.error = JailSentencePlanError::InvalidInput;
            return plan;
        }
        count += record.count;
    }

    for (auto inventoryIt = plan.inventory.begin(); inventoryIt != plan.inventory.end();)
    {
        auto stolenIt = remaining.find(lowerAscii(inventoryIt->refId));
        if (inventoryIt->refId.empty() || inventoryIt->count <= 0 || stolenIt == remaining.end())
        {
            ++inventoryIt;
            continue;
        }

        std::int64_t stolenCount = 0;
        for (const auto& [owner, count] : stolenIt->second)
        {
            (void)owner;
            if (stolenCount > std::numeric_limits<std::int64_t>::max() - count)
            {
                plan.error = JailSentencePlanError::InvalidInput;
                return plan;
            }
            stolenCount += count;
        }
        const int moveCount = static_cast<int>(std::min<std::int64_t>(inventoryIt->count, stolenCount));
        if (moveCount <= 0)
        {
            ++inventoryIt;
            continue;
        }

        Item moved = *inventoryIt;
        moved.count = moveCount;
        if (moveCount < inventoryIt->count)
        {
            moved.instanceId = allocateInstanceId ? allocateInstanceId() : 0;
            if (moved.instanceId == 0)
            {
                plan.error = JailSentencePlanError::InstanceIdUnavailable;
                return plan;
            }
            inventoryIt->count -= moveCount;
            ++inventoryIt;
        }
        else
            inventoryIt = plan.inventory.erase(inventoryIt);

        std::int64_t toConsume = moveCount;
        for (auto ownerIt = stolenIt->second.begin(); ownerIt != stolenIt->second.end() && toConsume > 0;)
        {
            const std::int64_t consumed = std::min(toConsume, ownerIt->second);
            plan.stolenItemMutations.push_back(
                { moved.refId, ownerIt->first.first, ownerIt->first.second, -consumed });
            ownerIt->second -= consumed;
            toConsume -= consumed;
            if (ownerIt->second == 0)
                ownerIt = stolenIt->second.erase(ownerIt);
            else
                ++ownerIt;
        }
        if (stolenIt->second.empty())
            remaining.erase(stolenIt);

        appendEvidenceItem(plan.evidence.items, moved);
        plan.confiscatedItems.push_back(std::move(moved));
    }

    plan.equipment.erase(std::remove_if(plan.equipment.begin(), plan.equipment.end(), [&](const EquipmentItem& equipped) {
        if (equipped.slot < 0 || equipped.item.refId.empty())
            return true;
        return std::none_of(plan.inventory.begin(), plan.inventory.end(), [&](const Item& item) {
            if (equipped.item.instanceId != 0)
                return item.instanceId == equipped.item.instanceId && item.refId == equipped.item.refId;
            return lowerAscii(item.refId) == lowerAscii(equipped.item.refId)
                && item.charge == equipped.item.charge;
        });
    }), plan.equipment.end());
    plan.equipmentChanged = plan.equipment != equipment;

    return plan;
}
