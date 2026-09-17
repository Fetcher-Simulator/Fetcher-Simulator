#ifndef OPENMW_SERVER_INVENTORYRECONCILIATION_HPP
#define OPENMW_SERVER_INVENTORYRECONCILIATION_HPP

#include <algorithm>
#include <optional>
#include <unordered_set>

#include <components/openmw-mp/InventorySync.hpp>
#include <components/openmw-mp/InventoryTake.hpp>

namespace mwmp
{
    struct PendingInventoryTransfer
    {
        uint32_t instanceId = 0;
        std::string refId;
        int count = 0;
        uint64_t expiresAtMs = 0;
    };

    struct InventoryIdentityReconciliation
    {
        bool echoRequired = false;
        std::size_t invalidSuppliedIds = 0;
        std::size_t recoveredPreviousIds = 0;
        std::size_t recoveredTransferIds = 0;
        std::size_t allocatedIds = 0;
        std::size_t allocationFailures = 0;
        std::string firstCorrectedRefId;
        uint32_t firstRequestedId = 0;
        uint32_t firstAssignedId = 0;

        bool actionable() const
        {
            return invalidSuppliedIds != 0 || recoveredPreviousIds != 0
                || recoveredTransferIds != 0 || allocatedIds != 0 || allocationFailures != 0;
        }
    };

    // Transfers must already have been pruned for expiry by the caller. Keep the
    // identity algorithm independent of transport/logging so diagnostics exercise
    // the same matching and allocation paths in tests as in the server.
    template <class AllocateId>
    InventoryIdentityReconciliation reconcileInventoryIdentities(const std::vector<Item>& previous,
        std::vector<Item>& items, std::vector<PendingInventoryTransfer>& transfers, AllocateId&& allocateId)
    {
        std::unordered_set<uint32_t> used;
        InventoryIdentityReconciliation result;

        for (Item& item : items)
        {
            if (item.refId.empty() || item.count <= 0)
                continue;

            const uint32_t requestedId = item.instanceId;
            bool itemIdentityChanged = false;
            auto previousById = std::find_if(previous.begin(), previous.end(), [&](const Item& old) {
                return item.instanceId != 0 && old.instanceId == item.instanceId && old.refId == item.refId;
            });
            auto transferById = std::find_if(transfers.begin(), transfers.end(),
                [&](const PendingInventoryTransfer& transfer) {
                    return item.instanceId != 0 && transfer.instanceId == item.instanceId
                        && transfer.refId == item.refId;
                });
            const bool suppliedIdIsValid = item.instanceId != 0 && used.count(item.instanceId) == 0
                && (previousById != previous.end() || transferById != transfers.end());
            if (!suppliedIdIsValid && item.instanceId != 0)
            {
                item.instanceId = 0;
                result.echoRequired = true;
                itemIdentityChanged = true;
                ++result.invalidSuppliedIds;
            }

            if (item.instanceId == 0)
            {
                const auto previousMatch = std::find_if(previous.begin(), previous.end(), [&](const Item& old) {
                    return old.instanceId != 0 && used.count(old.instanceId) == 0 && sameAuthoritativeItemIdentity(old, item);
                });
                if (previousMatch != previous.end())
                {
                    item.instanceId = previousMatch->instanceId;
                    // Missing fungible gold identity is expected after a live store rebuild.
                    // Invalid nonzero IDs above remain actionable, including on gold.
                    if (requiresStableInventoryInstanceId(item))
                    {
                        itemIdentityChanged = true;
                        ++result.recoveredPreviousIds;
                    }
                }
            }

            if (item.instanceId == 0)
            {
                const auto transfer = std::find_if(transfers.begin(), transfers.end(),
                    [&](const PendingInventoryTransfer& pending) {
                        return pending.instanceId != 0 && used.count(pending.instanceId) == 0
                            && pending.refId == item.refId;
                    });
                if (transfer != transfers.end())
                {
                    item.instanceId = transfer->instanceId;
                    transferById = transfer;
                    result.echoRequired = true;
                    itemIdentityChanged = true;
                    ++result.recoveredTransferIds;
                }
            }

            if (item.instanceId == 0)
            {
                const std::optional<uint32_t> allocated = allocateId();
                if (!allocated)
                {
                    ++result.allocationFailures;
                    continue;
                }
                item.instanceId = *allocated;
                result.echoRequired = true;
                itemIdentityChanged = true;
                ++result.allocatedIds;
            }

            if (itemIdentityChanged && result.firstCorrectedRefId.empty())
            {
                result.firstCorrectedRefId = item.refId;
                result.firstRequestedId = requestedId;
                result.firstAssignedId = item.instanceId;
            }

            used.insert(item.instanceId);
            if (transferById != transfers.end())
                transfers.erase(transferById);
        }
        return result;
    }
}

#endif
