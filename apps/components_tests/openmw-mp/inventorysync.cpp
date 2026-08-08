#include <gtest/gtest.h>

#include <components/openmw-mp/InventorySync.hpp>

TEST(InventoryRevisionGate, BlocksAdditionalMutationUntilAuthoritativeReply)
{
    mwmp::InventoryRevisionGate gate;
    EXPECT_TRUE(gate.canSend());
    gate.markSent();
    EXPECT_FALSE(gate.canSend());
    EXPECT_TRUE(gate.observeAuthoritative());
    EXPECT_TRUE(gate.canSend());
    EXPECT_FALSE(gate.observeAuthoritative());
}

TEST(InventoryRevisionGate, ResetReleasesInFlightMutation)
{
    mwmp::InventoryRevisionGate gate;
    gate.markSent();
    gate.reset();
    EXPECT_TRUE(gate.canSend());
}

TEST(InventorySync, UnchangedAuthoritativeAckMatchesSentSnapshot)
{
    mwmp::Item item;
    item.instanceId = 42;
    item.refId = "iron dagger";
    item.count = 1;
    item.charge = 50;
    item.enchantmentCharge = 17.5f;

    EXPECT_TRUE(mwmp::inventoryAckMatchesSentSnapshot({ item }, { item }));

    mwmp::Item corrected = item;
    corrected.instanceId = 43;
    EXPECT_FALSE(mwmp::inventoryAckMatchesSentSnapshot({ corrected }, { item }));

    corrected = item;
    corrected.count = 2;
    EXPECT_FALSE(mwmp::inventoryAckMatchesSentSnapshot({ corrected }, { item }));
}

TEST(InventorySync, FungibleGoldInstanceIdDoesNotForceLiveStoreReconciliation)
{
    mwmp::Item sent;
    sent.refId = "gold_001";
    sent.count = 100;
    sent.instanceId = 0;

    mwmp::Item authoritative = sent;
    authoritative.instanceId = 4048;

    EXPECT_TRUE(mwmp::inventoryAckMatchesSentSnapshot({ authoritative }, { sent }));
}

TEST(InventorySync, SmallRechargeOnlyChangeCanBeDeferred)
{
    mwmp::Item previous;
    previous.instanceId = 7;
    previous.refId = "recharging saber";
    previous.count = 1;
    previous.charge = 100;
    previous.enchantmentCharge = 499.50f;

    mwmp::Item live = previous;
    live.enchantmentCharge = 499.52f;
    EXPECT_TRUE(mwmp::isOnlySmallEnchantmentChargeChange(live, previous, 0.05f));

    live.enchantmentCharge = 499.60f;
    EXPECT_FALSE(mwmp::isOnlySmallEnchantmentChargeChange(live, previous, 0.05f));

    live = previous;
    live.charge = 99;
    live.enchantmentCharge = 499.52f;
    EXPECT_FALSE(mwmp::isOnlySmallEnchantmentChargeChange(live, previous, 0.05f));
}
