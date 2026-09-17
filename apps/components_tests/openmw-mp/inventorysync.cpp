#include <gtest/gtest.h>

#include <components/openmw-mp/InventorySync.hpp>
#include <apps/openmw-server/InventoryReconciliation.hpp>
#include <limits>

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

namespace
{
    class InventoryReconciliationTest : public testing::Test
    {
    protected:
        std::vector<mwmp::Item> previous;
        std::vector<mwmp::Item> incoming;
        std::vector<mwmp::PendingInventoryTransfer> transfers;
        uint32_t nextId = 100;

        mwmp::Item item(std::string refId, uint32_t id)
        {
            mwmp::Item result;
            result.refId = std::move(refId);
            result.instanceId = id;
            result.count = 1;
            return result;
        }

        mwmp::InventoryIdentityReconciliation reconcile()
        {
            return mwmp::reconcileInventoryIdentities(previous, incoming, transfers,
                [&]() -> std::optional<uint32_t> { return nextId++; });
        }
    };

    TEST_F(InventoryReconciliationTest, ExpectedGoldRecoveryIsNotActionable)
    {
        previous = { item("gold_001", 5849) };
        incoming = { item("gold_001", 0) };
        const auto result = reconcile();
        ASSERT_EQ(incoming.size(), 1u);
        EXPECT_EQ(incoming[0].instanceId, 5849u);
        EXPECT_EQ(incoming[0].count, 1);
        EXPECT_FALSE(result.actionable());
        EXPECT_FALSE(result.echoRequired);
        EXPECT_TRUE(result.firstCorrectedRefId.empty());
        EXPECT_EQ(nextId, 100u);
    }

    TEST_F(InventoryReconciliationTest, MissingStableIdentityRemainsActionableAfterGold)
    {
        previous = { item("gold_001", 5849), item("_es_link_megaton", 42) };
        incoming = { item("gold_001", 0), item("_es_link_megaton", 0) };
        const auto result = reconcile();
        EXPECT_EQ(incoming[1].instanceId, 42u);
        EXPECT_TRUE(result.actionable());
        EXPECT_EQ(result.recoveredPreviousIds, 1u);
        EXPECT_EQ(result.firstCorrectedRefId, "_es_link_megaton");
        EXPECT_EQ(result.firstRequestedId, 0u);
        EXPECT_EQ(result.firstAssignedId, 42u);
    }

    TEST_F(InventoryReconciliationTest, DuplicateSuppliedIdIsCorrectedWithoutLosingStacks)
    {
        previous = { item("blade", 42), item("blade", 43) };
        incoming = { item("blade", 42), item("blade", 42) };
        const auto result = reconcile();
        ASSERT_EQ(incoming.size(), 2u);
        EXPECT_EQ(incoming[0].instanceId, 42u);
        EXPECT_EQ(incoming[1].instanceId, 43u);
        EXPECT_EQ(incoming[0].count + incoming[1].count, 2);
        EXPECT_TRUE(result.actionable());
        EXPECT_TRUE(result.echoRequired);
        EXPECT_EQ(result.invalidSuppliedIds, 1u);
    }

    TEST_F(InventoryReconciliationTest, InvalidNonzeroGoldIdentityRemainsActionable)
    {
        previous = { item("gold_001", 5849) };
        incoming = { item("gold_001", 999) };
        const auto result = reconcile();
        EXPECT_EQ(incoming[0].instanceId, 5849u);
        EXPECT_TRUE(result.actionable());
        EXPECT_TRUE(result.echoRequired);
        EXPECT_EQ(result.invalidSuppliedIds, 1u);
        EXPECT_EQ(result.recoveredPreviousIds, 0u);
        EXPECT_EQ(result.firstRequestedId, 999u);
    }

    TEST_F(InventoryReconciliationTest, TransferRecoveryRemainsActionableAndConsumesCredit)
    {
        incoming = { item("blade", 0) };
        transfers = { { 71, "blade", 1, 1000 } };
        const auto result = reconcile();
        EXPECT_EQ(incoming[0].instanceId, 71u);
        EXPECT_TRUE(transfers.empty());
        EXPECT_TRUE(result.actionable());
        EXPECT_TRUE(result.echoRequired);
        EXPECT_EQ(result.recoveredTransferIds, 1u);
        EXPECT_EQ(result.allocatedIds, 0u);
    }

    TEST_F(InventoryReconciliationTest, NewStackAllocationRemainsActionable)
    {
        incoming = { item("_es_link_mastersword", 0) };
        const auto result = reconcile();
        EXPECT_EQ(incoming[0].instanceId, 100u);
        EXPECT_TRUE(result.actionable());
        EXPECT_TRUE(result.echoRequired);
        EXPECT_EQ(result.allocatedIds, 1u);
    }

    TEST_F(InventoryReconciliationTest, ExistingStableIdentitiesAreQuiet)
    {
        previous = { item("blade", 42) };
        incoming = previous;
        EXPECT_FALSE(reconcile().actionable());
        EXPECT_EQ(incoming[0].instanceId, 42u);
    }
}

TEST(InventorySync, ChargeOnlyClassificationChecksEveryStackAndIgnoresExpectedGoldIdentity)
{
    mwmp::Item gold;
    gold.refId = "gold_001";
    gold.instanceId = 1;
    gold.count = 100;
    mwmp::Item blade;
    blade.refId = "blade";
    blade.instanceId = 2;
    blade.count = 1;
    blade.enchantmentCharge = 10.f;
    const std::vector<mwmp::Item> previous{ gold, blade };
    auto incoming = previous;
    incoming[0].instanceId = 0;
    EXPECT_FALSE(mwmp::isOnlyInventoryEnchantmentChargeChange(incoming, previous));
    incoming[1].enchantmentCharge += 0.02f;
    EXPECT_TRUE(mwmp::isOnlyInventoryEnchantmentChargeChange(incoming, previous));
    incoming[0].count++;
    EXPECT_FALSE(mwmp::isOnlyInventoryEnchantmentChargeChange(incoming, previous));
    incoming[0].count--;
    incoming[1].instanceId = 0;
    EXPECT_FALSE(mwmp::isOnlyInventoryEnchantmentChargeChange(incoming, previous));
    incoming[1].instanceId = 2;
    incoming[1].enchantmentCharge = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(mwmp::isOnlyInventoryEnchantmentChargeChange(incoming, previous));
    incoming.pop_back();
    EXPECT_FALSE(mwmp::isOnlyInventoryEnchantmentChargeChange(incoming, previous));
}
