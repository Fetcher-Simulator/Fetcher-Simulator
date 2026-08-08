#ifndef OPENMW_COMPONENTS_OPENMW_MP_INVENTORYSYNC_HPP
#define OPENMW_COMPONENTS_OPENMW_MP_INVENTORYSYNC_HPP

#include <cmath>
#include <vector>

#include <components/openmw-mp/Base/BaseStructs.hpp>

namespace mwmp
{
    inline bool requiresStableInventoryInstanceId(const Item& item)
    {
        // ContainerStore canonicalizes every gold pile into one gold_001 stack
        // backed by a fresh ManualRef, so a server-issued RefNum cannot survive
        // an authoritative inventory rebuild for this intentionally fungible item.
        return item.refId != "gold_001";
    }

    inline bool isOnlySmallEnchantmentChargeChange(
        const Item& live, const Item& previous, float immediateDelta)
    {
        if (live.instanceId != previous.instanceId
            || live.refId != previous.refId
            || live.count != previous.count
            || live.charge != previous.charge
            || live.soul != previous.soul)
            return false;

        const float delta = std::abs(live.enchantmentCharge - previous.enchantmentCharge);
        return delta >= 0.001f && delta < immediateDelta;
    }

    inline bool inventoryAckMatchesSentSnapshot(
        const std::vector<Item>& authoritative, const std::vector<Item>& sent)
    {
        if (authoritative.size() != sent.size())
            return false;

        for (std::size_t i = 0; i < authoritative.size(); ++i)
        {
            const Item& ack = authoritative[i];
            const Item& local = sent[i];
            if (ack.refId != local.refId
                || ack.count != local.count
                || ack.charge != local.charge
                || std::abs(ack.enchantmentCharge - local.enchantmentCharge) >= 0.001f
                || ack.soul != local.soul)
                return false;

            if (requiresStableInventoryInstanceId(local) && ack.instanceId != local.instanceId)
                return false;
        }
        return true;
    }

    class InventoryRevisionGate
    {
    public:
        bool canSend() const { return !mInFlight; }
        void markSent() { mInFlight = true; }

        bool observeAuthoritative()
        {
            if (!mInFlight)
                return false;
            mInFlight = false;
            return true;
        }

        void reset() { mInFlight = false; }

    private:
        bool mInFlight = false;
    };
}

#endif
