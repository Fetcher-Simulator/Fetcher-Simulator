#include <gtest/gtest.h>

#include <cmath>

#include <components/openmw-mp/InventoryTake.hpp>
#include <components/openmw-mp/Packets/Object/PacketInventoryTake.hpp>
#include <components/openmw-mp/Packets/Object/PacketContainer.hpp>

namespace
{
    mwmp::InventoryTakeRequest request()
    {
        mwmp::InventoryTakeRequest value;
        value.requestId = "inventory-take-1";
        value.kind = mwmp::InventoryTakeKind::Pickpocket;
        value.source.cellId = "Balmora";
        value.source.refId = "guard";
        value.source.refNum = 7;
        value.source.actorInstanceId
            = mwmp::packActorInstanceKey({ mwmp::ActorKeyKind::VanillaRefNum, 7 });
        value.source.migrationGeneration = 3;
        value.itemRefId = "gold_001";
        value.itemInstanceId = 4242;
        value.itemCharge = -1;
        value.itemEnchantmentCharge = 42.5f;
        value.itemSoul = "golden saint";
        value.requestedCount = 10;
        value.expectedInventoryRevision = 9;
        return value;
    }
}

TEST(InventoryTakeProtocol, RequestRoundTripsCanonically)
{
    mwmp::PacketInventoryTakeRequest outgoing;
    outgoing.request = request();
    outgoing.request.soundDirection = mwmp::InventoryTransferSoundDirection::Down;
    const auto first = outgoing.encode();
    EXPECT_EQ(first, outgoing.encode());

    mwmp::PacketInventoryTakeRequest incoming;
    ASSERT_TRUE(incoming.decode(first));
    EXPECT_EQ(incoming.request, outgoing.request);
    EXPECT_EQ(mwmp::canonicalInventoryTakeRequest(incoming.request),
        mwmp::canonicalInventoryTakeRequest(outgoing.request));

    auto trailing = first;
    trailing.push_back(0);
    EXPECT_FALSE(incoming.decode(trailing));

    auto invalidDirection = request();
    invalidDirection.soundDirection = static_cast<mwmp::InventoryTransferSoundDirection>(0);
    EXPECT_EQ(mwmp::validateInventoryTakeRequest(invalidDirection), mwmp::InventoryTakeError::InvalidRequest);

    auto downSound = request();
    downSound.soundDirection = mwmp::InventoryTransferSoundDirection::Down;
    EXPECT_NE(mwmp::canonicalInventoryTakeRequest(downSound),
        mwmp::canonicalInventoryTakeRequest(request()));

    auto differentInstance = request();
    differentInstance.itemInstanceId++;
    EXPECT_NE(mwmp::canonicalInventoryTakeRequest(differentInstance),
        mwmp::canonicalInventoryTakeRequest(request()));
}

TEST(InventoryTakeProtocol, BatchRequestRoundTripsWithStableRowIdentities)
{
    mwmp::InventoryTakeBatchRequest value;
    value.requestId = "inventory-take-batch-1";
    value.kind = mwmp::InventoryTakeKind::Corpse;
    value.source.cellId = "Balmora";
    value.source.refId = "boba_fett";
    value.source.mpNum = 6687;
    value.source.actorInstanceId
        = mwmp::packActorInstanceKey({ mwmp::ActorKeyKind::SpawnedMpNum, value.source.mpNum });
    value.source.migrationGeneration = 1;
    value.source.authorityGeneration = 12;
    value.expectedInventoryRevision = 44;
    value.soundDirection = mwmp::InventoryTransferSoundDirection::Down;
    value.items = {
        { "mandalore_cuirass", 8001, 412, -1.f, "", 1 },
        { "dwemer jinksword", 8002, 998, 180.392f, "", 1 },
        { "gold_001", 8003, -1, -1.f, "", 100 },
    };

    ASSERT_EQ(mwmp::validateInventoryTakeBatchRequest(value), mwmp::InventoryTakeError::None);
    mwmp::PacketInventoryTakeBatchRequest outgoing;
    outgoing.request = value;
    const auto bytes = outgoing.encode();
    EXPECT_EQ(bytes, outgoing.encode());

    mwmp::PacketInventoryTakeBatchRequest incoming;
    ASSERT_TRUE(incoming.decode(bytes));
    EXPECT_EQ(incoming.request, value);
    EXPECT_EQ(mwmp::canonicalInventoryTakeBatchRequest(incoming.request),
        mwmp::canonicalInventoryTakeBatchRequest(value));

    auto drifted = value;
    drifted.items[1].itemEnchantmentCharge = 180.725f;
    EXPECT_NE(mwmp::canonicalInventoryTakeBatchRequest(drifted),
        mwmp::canonicalInventoryTakeBatchRequest(value));
    auto differentInstance = value;
    differentInstance.items[1].itemInstanceId++;
    EXPECT_NE(mwmp::canonicalInventoryTakeBatchRequest(differentInstance),
        mwmp::canonicalInventoryTakeBatchRequest(value));

    auto empty = value;
    empty.items.clear();
    EXPECT_EQ(mwmp::validateInventoryTakeBatchRequest(empty), mwmp::InventoryTakeError::InvalidCount);
    auto pickpocket = value;
    pickpocket.kind = mwmp::InventoryTakeKind::Pickpocket;
    EXPECT_EQ(mwmp::validateInventoryTakeBatchRequest(pickpocket), mwmp::InventoryTakeError::InvalidRequest);
}

TEST(InventoryTakeProtocol, BatchResultRoundTripsAtomically)
{
    mwmp::InventoryTakeBatchResult value;
    value.requestId = "inventory-take-batch-1";
    value.accepted = true;
    value.kind = mwmp::InventoryTakeKind::Corpse;
    value.source.cellId = "Balmora";
    value.source.refId = "boba_fett";
    value.source.mpNum = 6687;
    value.source.actorInstanceId
        = mwmp::packActorInstanceKey({ mwmp::ActorKeyKind::SpawnedMpNum, value.source.mpNum });
    value.source.migrationGeneration = 1;
    value.lineCount = 12;
    value.itemCount = 111;
    value.inventoryRevision = 45;

    mwmp::PacketInventoryTakeBatchResult outgoing;
    outgoing.result = value;
    mwmp::PacketInventoryTakeBatchResult incoming;
    ASSERT_TRUE(incoming.decode(outgoing.encode()));
    EXPECT_EQ(incoming.result, value);
}

TEST(InventoryTakeProtocol, BarterRequestCarriesStrictMerchantIdentity)
{
    mwmp::InventoryTakeRequest value;
    value.requestId = "barter-take-1";
    value.kind = mwmp::InventoryTakeKind::Barter;
    value.source.cellId = "Taris, Lower City Black Market";
    value.source.refId = "SW_TarisChestLCVend1";
    value.source.refNum = 29598;
    value.merchant.cellId = value.source.cellId;
    value.merchant.refId = "sw_blackmarketmerchant";
    value.merchant.refNum = 29597;
    value.merchant.actorInstanceId
        = mwmp::packActorInstanceKey({ mwmp::ActorKeyKind::VanillaRefNum, value.merchant.refNum });
    value.merchant.migrationGeneration = 1;
    value.itemRefId = "SW_BlasterRepeater";
    value.itemCharge = -1;
    value.itemEnchantmentCharge = -1.f;
    value.requestedCount = 1;
    value.barterPrice = 5625;
    value.expectedInventoryRevision = 42;

    ASSERT_EQ(mwmp::validateInventoryTakeRequest(value), mwmp::InventoryTakeError::None);

    mwmp::PacketInventoryTakeRequest outgoing;
    outgoing.request = value;
    mwmp::PacketInventoryTakeRequest incoming;
    ASSERT_TRUE(incoming.decode(outgoing.encode()));
    EXPECT_EQ(incoming.request, value);

    auto missingMerchant = value;
    missingMerchant.merchant = {};
    EXPECT_EQ(mwmp::validateInventoryTakeRequest(missingMerchant), mwmp::InventoryTakeError::InvalidRequest);

    auto nonActorMerchant = value;
    nonActorMerchant.merchant.actorInstanceId = 0;
    nonActorMerchant.merchant.migrationGeneration = 0;
    EXPECT_EQ(mwmp::validateInventoryTakeRequest(nonActorMerchant), mwmp::InventoryTakeError::InvalidRequest);

    auto missingPrice = value;
    missingPrice.barterPrice = 0;
    EXPECT_EQ(mwmp::validateInventoryTakeRequest(missingPrice), mwmp::InventoryTakeError::InvalidPrice);

    auto ordinaryTake = value;
    ordinaryTake.kind = mwmp::InventoryTakeKind::Container;
    EXPECT_EQ(mwmp::validateInventoryTakeRequest(ordinaryTake), mwmp::InventoryTakeError::InvalidRequest);

    auto differentMerchant = value;
    differentMerchant.merchant.refId = "other_merchant";
    EXPECT_NE(mwmp::canonicalInventoryTakeRequest(differentMerchant),
        mwmp::canonicalInventoryTakeRequest(value));

    auto differentPrice = value;
    differentPrice.barterPrice = value.barterPrice - 1;
    EXPECT_NE(mwmp::canonicalInventoryTakeRequest(differentPrice),
        mwmp::canonicalInventoryTakeRequest(value));
}

TEST(InventoryTakeProtocol, ResultRoundTripsDetectionAndCrimeMetadata)
{
    mwmp::PacketInventoryTakeResult outgoing;
    outgoing.result.requestId = request().requestId;
    outgoing.result.accepted = true;
    outgoing.result.kind = mwmp::InventoryTakeKind::Pickpocket;
    outgoing.result.source = request().source;
    outgoing.result.itemRefId = "gold_001";
    outgoing.result.itemCount = 10;
    outgoing.result.inventoryRevision = 10;
    outgoing.result.detected = true;
    outgoing.result.detectionRoll = 93;
    outgoing.result.crimeValue = 10;

    mwmp::PacketInventoryTakeResult incoming;
    ASSERT_TRUE(incoming.decode(outgoing.encode()));
    EXPECT_EQ(incoming.result, outgoing.result);
}

TEST(InventoryTakeProtocol, RejectsInvalidActorBindingAndCounts)
{
    auto value = request();
    value.source.actorInstanceId = 0;
    EXPECT_EQ(mwmp::validateInventoryTakeRequest(value), mwmp::InventoryTakeError::InvalidRequest);
    value = request();
    value.requestedCount = 0;
    EXPECT_EQ(mwmp::validateInventoryTakeRequest(value), mwmp::InventoryTakeError::InvalidCount);
    value = request();
    value.source.mpNum = 9;
    EXPECT_EQ(mwmp::validateInventoryTakeRequest(value), mwmp::InventoryTakeError::InvalidRequest);
    value = request();
    value.source.actorInstanceId
        = mwmp::packActorInstanceKey({ mwmp::ActorKeyKind::VanillaRefNum, 8 });
    EXPECT_EQ(mwmp::validateInventoryTakeRequest(value), mwmp::InventoryTakeError::InvalidRequest);
}

TEST(InventoryTakeAuthority, PreservesNativeDetectionThresholdAndRollBoundary)
{
    mwmp::PickpocketDetectionInput input;
    input.thiefSneak = 40.f;
    input.thiefAgility = 50.f;
    input.thiefLuck = 50.f;
    input.thiefFatigueTerm = 1.f;
    input.victimSneak = 20.f;
    input.victimAgility = 40.f;
    input.victimLuck = 40.f;
    input.victimFatigueTerm = 1.f;
    input.valueTerm = 0.f;
    input.minimumChanceDivisor = 4;
    input.maximumChance = 75;
    input.roll0To99 = 75;
    const auto success = mwmp::evaluatePickpocketDetection(input);
    ASSERT_TRUE(success.valid);
    EXPECT_EQ(success.threshold, 75);
    EXPECT_FALSE(success.detected);

    input.roll0To99 = 76;
    EXPECT_TRUE(mwmp::evaluatePickpocketDetection(input).detected);
    input.minimumChanceDivisor = 0;
    EXPECT_FALSE(mwmp::evaluatePickpocketDetection(input).valid);
}

TEST(InventoryTakeProtocol, ContainerBootstrapIsStrictAndCanonical)
{
    mwmp::PacketContainer outgoing;
    outgoing.container.cellId = "Balmora";
    outgoing.container.refId = "crate_01";
    outgoing.container.refNum = 42;
    outgoing.authorityGeneration = 17;
    outgoing.bootstrapSequence = 0x123456789abcdef0ULL;
    outgoing.container.items.push_back(
        { "daedric dagger", 1, 314, 123456, 87.25f, "golden saint" });
    outgoing.container.items.push_back(
        { "restocking potion", 5, -1, 0, -1.f, "", true });
    outgoing.mAction = static_cast<std::uint8_t>(mwmp::ContainerAction::BootstrapRequest);
    const auto encoded = outgoing.encode();
    mwmp::PacketContainer incoming;
    ASSERT_TRUE(incoming.decode(encoded));
    EXPECT_EQ(incoming.container.cellId, outgoing.container.cellId);
    EXPECT_EQ(incoming.container.items, outgoing.container.items);
    EXPECT_EQ(incoming.mAction, outgoing.mAction);
    EXPECT_EQ(incoming.authorityGeneration, 17u);
    EXPECT_EQ(incoming.bootstrapSequence, outgoing.bootstrapSequence);

    auto trailing = encoded;
    trailing.push_back(0);
    EXPECT_FALSE(incoming.decode(trailing));

    outgoing.mAction = 99;
    EXPECT_FALSE(incoming.decode(outgoing.encode()));
}

namespace
{
    mwmp::ContainerItem medkit(int count, std::uint32_t instanceId, bool restocking = false,
        int charge = -1, float enchantmentCharge = -1.f, std::string soul = {})
    {
        mwmp::ContainerItem item;
        item.refId = "sw_medkit";
        item.count = count;
        item.charge = charge;
        item.instanceId = instanceId;
        item.enchantmentCharge = enchantmentCharge;
        item.soul = std::move(soul);
        item.restocking = restocking;
        return item;
    }

    mwmp::Item inventoryMedkit(int count, std::uint32_t instanceId, int charge = -1)
    {
        mwmp::Item item;
        item.refId = "sw_medkit";
        item.count = count;
        item.charge = charge;
        item.instanceId = instanceId;
        return item;
    }
}

TEST(InventoryAuthority, EquipmentMetadataReconcileUpdatesExactAuthoritativeRow)
{
    mwmp::ContainerItem sword;
    sword.refId = "dwemer jinksword";
    sword.count = 1;
    sword.charge = 997;
    sword.enchantmentCharge = 120.f;
    sword.instanceId = 7001;
    std::vector<mwmp::ContainerItem> source{ sword };

    mwmp::EquipmentItem before;
    before.slot = 16;
    before.item.refId = sword.refId;
    before.item.count = 1;
    before.item.charge = 997;
    before.item.enchantmentCharge = 120.f;
    mwmp::EquipmentItem after = before;
    after.item.enchantmentCharge = 90.f;

    EXPECT_TRUE(mwmp::reconcileAuthoritativeContainerEquipment(source, { before }, { after }));
    ASSERT_EQ(source.size(), 1u);
    EXPECT_EQ(source.front().instanceId, 7001u);
    EXPECT_EQ(source.front().charge, 997);
    EXPECT_FLOAT_EQ(source.front().enchantmentCharge, 90.f);
}

TEST(InventoryAuthority, EquipmentMetadataReconcileOnlyUpdatesPreviousConditionRow)
{
    mwmp::ContainerItem worn;
    worn.refId = "iron_cuirass";
    worn.count = 1;
    worn.charge = 100;
    worn.instanceId = 7101;
    mwmp::ContainerItem other = worn;
    other.charge = 50;
    other.instanceId = 7102;
    std::vector<mwmp::ContainerItem> source{ worn, other };

    mwmp::EquipmentItem before;
    before.slot = 1;
    before.item.refId = worn.refId;
    before.item.count = 1;
    before.item.charge = 100;
    mwmp::EquipmentItem after = before;
    after.item.charge = 80;

    EXPECT_TRUE(mwmp::reconcileAuthoritativeContainerEquipment(source, { before }, { after }));
    ASSERT_EQ(source.size(), 2u);
    EXPECT_EQ(source[0].instanceId, 7101u);
    EXPECT_EQ(source[0].charge, 80);
    EXPECT_EQ(source[1].instanceId, 7102u);
    EXPECT_EQ(source[1].charge, 50);
}

TEST(InventoryAuthority, EquipmentMetadataReconcileDoesNotRewriteEquipmentSwap)
{
    mwmp::ContainerItem sword;
    sword.refId = "dwemer jinksword";
    sword.count = 1;
    sword.charge = 997;
    sword.enchantmentCharge = 120.f;
    sword.instanceId = 7201;
    const std::vector<mwmp::ContainerItem> original{ sword };
    auto source = original;

    mwmp::EquipmentItem before;
    before.slot = 16;
    before.item.refId = sword.refId;
    before.item.count = 1;
    before.item.charge = 997;
    before.item.enchantmentCharge = 120.f;
    mwmp::EquipmentItem after = before;
    after.item.refId = "iron longsword";
    after.item.charge = 400;
    after.item.enchantmentCharge = -1.f;

    EXPECT_FALSE(mwmp::reconcileAuthoritativeContainerEquipment(source, { before }, { after }));
    EXPECT_EQ(source, original);
}

TEST(InventoryAuthority, WorldPickupMergesIntoExistingCanonicalPlayerStack)
{
    std::vector<mwmp::Item> inventory{ inventoryMedkit(4, 6001) };
    mwmp::Item pickedUp = inventoryMedkit(2, 7001);

    EXPECT_EQ(mwmp::mergeAuthoritativeInventoryItem(inventory, pickedUp),
        mwmp::AuthoritativeStackMutation::Merged);
    ASSERT_EQ(inventory.size(), 1u);
    EXPECT_EQ(inventory.front().count, 6);
    EXPECT_EQ(inventory.front().instanceId, 6001u);
    EXPECT_EQ(pickedUp.instanceId, 6001u);
}

TEST(InventoryAuthority, RefIdsStackCaseInsensitively)
{
    std::vector<mwmp::Item> inventory{ inventoryMedkit(1, 6001) };
    inventory.front().refId = "sw_medkit";
    mwmp::Item restockingPurchase = inventoryMedkit(2, 0);
    restockingPurchase.refId = "SW_Medkit";

    EXPECT_EQ(mwmp::mergeAuthoritativeInventoryItem(inventory, restockingPurchase),
        mwmp::AuthoritativeStackMutation::Merged);
    ASSERT_EQ(inventory.size(), 1u);
    EXPECT_EQ(inventory.front().count, 3);
    EXPECT_EQ(inventory.front().instanceId, 6001u);
    EXPECT_EQ(restockingPurchase.instanceId, 6001u);
}

TEST(InventoryAuthority, NativeNonStackableWorldPickupRetainsSeparateIdentity)
{
    std::vector<mwmp::Item> inventory{ inventoryMedkit(1, 6001) };
    mwmp::Item scriptedPickup = inventoryMedkit(1, 7001);

    EXPECT_EQ(mwmp::mergeAuthoritativeInventoryItem(inventory, scriptedPickup, false),
        mwmp::AuthoritativeStackMutation::Added);
    ASSERT_EQ(inventory.size(), 2u);
    EXPECT_EQ(inventory[0].instanceId, 6001u);
    EXPECT_EQ(inventory[1].instanceId, 7001u);
}

TEST(InventoryAuthority, EquippedDestinationCanBeExcludedFromNativeStackMerge)
{
    std::vector<mwmp::Item> inventory{ inventoryMedkit(10, 6001) };
    mwmp::Item recovered = inventoryMedkit(1, 7001);

    const auto unequippedOnly = [](const mwmp::Item& item) { return item.instanceId != 6001; };
    EXPECT_EQ(mwmp::mergeAuthoritativeInventoryItem(inventory, recovered, true, unequippedOnly),
        mwmp::AuthoritativeStackMutation::Added);
    ASSERT_EQ(inventory.size(), 2u);
    EXPECT_EQ(inventory[0].count, 10);
    EXPECT_EQ(inventory[0].instanceId, 6001u);
    EXPECT_EQ(inventory[1].count, 1);
    EXPECT_EQ(inventory[1].instanceId, 7001u);
}

TEST(InventoryAuthority, UnequippedCompatibleDestinationIsPreferredWhenEquippedStackIsBlocked)
{
    std::vector<mwmp::Item> inventory{
        inventoryMedkit(10, 6001),
        inventoryMedkit(3, 6002),
    };
    mwmp::Item recovered = inventoryMedkit(2, 7001);

    const auto unequippedOnly = [](const mwmp::Item& item) { return item.instanceId != 6001; };
    EXPECT_EQ(mwmp::mergeAuthoritativeInventoryItem(inventory, recovered, true, unequippedOnly),
        mwmp::AuthoritativeStackMutation::Merged);
    ASSERT_EQ(inventory.size(), 2u);
    EXPECT_EQ(inventory[0].count, 10);
    EXPECT_EQ(inventory[0].instanceId, 6001u);
    EXPECT_EQ(inventory[1].count, 5);
    EXPECT_EQ(inventory[1].instanceId, 6002u);
    EXPECT_EQ(recovered.instanceId, 6002u);
}

TEST(InventoryAuthority, RepeatedPartialPutsRetainOneDestinationIdentity)
{
    std::vector<mwmp::ContainerItem> destination{ medkit(3, 6337) };
    mwmp::ContainerItem first = medkit(1, 6338);
    mwmp::ContainerItem second = medkit(2, 6339);

    EXPECT_EQ(mwmp::mergeAuthoritativeContainerItem(destination, first),
        mwmp::AuthoritativeStackMutation::Merged);
    EXPECT_EQ(mwmp::mergeAuthoritativeContainerItem(destination, second),
        mwmp::AuthoritativeStackMutation::Merged);
    ASSERT_EQ(destination.size(), 1u);
    EXPECT_EQ(destination.front().count, 6);
    EXPECT_EQ(destination.front().instanceId, 6337u);
    EXPECT_EQ(first.instanceId, 6337u);
    EXPECT_EQ(second.instanceId, 6337u);
}

TEST(InventoryAuthority, NativeNonStackableItemsRetainSeparateDestinationIdentities)
{
    std::vector<mwmp::ContainerItem> destination{ medkit(1, 6337) };
    mwmp::ContainerItem scriptedItem = medkit(1, 6338);

    EXPECT_EQ(mwmp::mergeAuthoritativeContainerItem(destination, scriptedItem, false),
        mwmp::AuthoritativeStackMutation::Added);
    ASSERT_EQ(destination.size(), 2u);
    EXPECT_EQ(destination[0].instanceId, 6337u);
    EXPECT_EQ(destination[1].instanceId, 6338u);
}

TEST(InventoryAuthority, ActorLocalNegativeRestockIsInfectiousOnSale)
{
    std::vector<mwmp::ContainerItem> actorInventory{ medkit(2, 0, true) };
    mwmp::ContainerItem sold = medkit(1, 6335);

    EXPECT_EQ(mwmp::mergeAuthoritativeContainerItem(actorInventory, sold),
        mwmp::AuthoritativeStackMutation::Merged);
    ASSERT_EQ(actorInventory.size(), 1u);
    EXPECT_EQ(actorInventory.front().count, 3);
    EXPECT_TRUE(actorInventory.front().restocking);
    EXPECT_EQ(actorInventory.front().instanceId, 0u);
    EXPECT_TRUE(sold.restocking);
    EXPECT_EQ(sold.instanceId, 0u);
}

TEST(InventoryAuthority, ChestBackedMerchantConsumesActorFiniteBeforeUnchangedChestRestock)
{
    std::vector<mwmp::ContainerItem> actorInventory;
    std::vector<mwmp::ContainerItem> chestInventory{ medkit(2, 0, true) };
    mwmp::ContainerItem sold = medkit(1, 6335);
    ASSERT_EQ(mwmp::mergeAuthoritativeContainerItem(actorInventory, sold),
        mwmp::AuthoritativeStackMutation::Added);

    auto finite = mwmp::takeAuthoritativeContainerItems(actorInventory,
        "sw_medkit", -1, -1.f, "", 1);
    ASSERT_TRUE(finite);
    EXPECT_TRUE(actorInventory.empty());

    std::vector<mwmp::Item> playerInventory;
    mwmp::Item actorPurchase = inventoryMedkit(1, 7100);
    ASSERT_EQ(mwmp::mergeAuthoritativeInventoryItem(playerInventory, actorPurchase),
        mwmp::AuthoritativeStackMutation::Added);
    auto restocking = mwmp::takeAuthoritativeContainerItems(chestInventory,
        "sw_medkit", -1, -1.f, "", 2);
    ASSERT_TRUE(restocking);
    EXPECT_TRUE(restocking->backingRows.empty());
    mwmp::Item chestPurchase = inventoryMedkit(restocking->taken.count, 0);
    ASSERT_EQ(mwmp::mergeAuthoritativeInventoryItem(playerInventory, chestPurchase),
        mwmp::AuthoritativeStackMutation::Merged);

    ASSERT_EQ(playerInventory.size(), 1u);
    EXPECT_EQ(playerInventory.front().count, 3);
    ASSERT_EQ(chestInventory.size(), 1u);
    EXPECT_EQ(chestInventory.front().count, 2);
    EXPECT_TRUE(chestInventory.front().restocking);
}

TEST(InventoryAuthority, AggregateTakeConsumesCompatibleBackingRowsAtomically)
{
    std::vector<mwmp::ContainerItem> source{
        medkit(3, 6337), medkit(1, 6338), medkit(1, 6339), medkit(1, 6340)
    };
    auto taken = mwmp::takeAuthoritativeContainerItems(source, "SW_Medkit", -1, -1.f, "", 6);

    ASSERT_TRUE(taken);
    EXPECT_TRUE(source.empty());
    EXPECT_EQ(taken->taken.count, 6);
    ASSERT_EQ(taken->backingRows.size(), 4u);
    EXPECT_EQ(taken->backingRows[0].instanceId, 6337u);
    EXPECT_EQ(taken->backingRows[0].count, 3);
    EXPECT_EQ(taken->backingRows[3].instanceId, 6340u);
}

TEST(InventoryAuthority, PartialAggregateTakePreservesSurvivingBackingIdentities)
{
    std::vector<mwmp::ContainerItem> source{
        medkit(3, 6337), medkit(1, 6338), medkit(1, 6339), medkit(1, 6340)
    };
    auto taken = mwmp::takeAuthoritativeContainerItems(source, "sw_medkit", -1, -1.f, "", 4);

    ASSERT_TRUE(taken);
    ASSERT_EQ(taken->backingRows.size(), 2u);
    EXPECT_EQ(taken->backingRows[0].instanceId, 6337u);
    EXPECT_EQ(taken->backingRows[1].instanceId, 6338u);
    ASSERT_EQ(source.size(), 2u);
    EXPECT_EQ(source[0].instanceId, 6339u);
    EXPECT_EQ(source[0].count, 1);
    EXPECT_EQ(source[1].instanceId, 6340u);
    EXPECT_EQ(source[1].count, 1);
}

TEST(InventoryAuthority, AggregateTakeRejectsIncompatibleMetadataWithoutMutation)
{
    std::vector<mwmp::ContainerItem> source{
        medkit(3, 6337, false, -1),
        medkit(2, 6338, false, 10),
        medkit(2, 6339, false, -1, 5.f),
        medkit(2, 6340, false, -1, -1.f, "soul"),
    };
    const auto original = source;

    EXPECT_FALSE(mwmp::takeAuthoritativeContainerItems(source, "sw_medkit", -1, -1.f, "", 4));
    EXPECT_EQ(source, original);
}

TEST(InventoryAuthority, AggregateTakeCanUseNativeStackCompatibilityAcrossRawCharge)
{
    // A miscellaneous item has no item health: raw charge is irrelevant to
    // native stacking. This does not permit damaged armor/weapon aggregation.
    std::vector<mwmp::ContainerItem> source{
        medkit(3, 6337, false, -1), medkit(1, 6338, false, 0)
    };
    const auto exactSource = source;
    EXPECT_FALSE(mwmp::takeAuthoritativeContainerItems(source, "sw_medkit", -1, -1.f, "", 4));
    EXPECT_EQ(source, exactSource);

    auto nativeCompatible = mwmp::takeAuthoritativeContainerItems(source, "sw_medkit", -1, -1.f, "", 4,
        [](const mwmp::ContainerItem& left, const mwmp::ContainerItem& right) {
            return left.refId == right.refId
                && std::abs(left.enchantmentCharge - right.enchantmentCharge) < 0.001f
                && left.soul == right.soul;
        });

    ASSERT_TRUE(nativeCompatible);
    EXPECT_TRUE(source.empty());
    EXPECT_EQ(nativeCompatible->taken.count, 4);
    ASSERT_EQ(nativeCompatible->backingRows.size(), 2u);
    EXPECT_EQ(nativeCompatible->backingRows[0].count, 3);
    EXPECT_EQ(nativeCompatible->backingRows[1].count, 1);
}

TEST(InventoryAuthority, FiniteAndRestockingRowsCannotBeAggregatedByOneTake)
{
    std::vector<mwmp::ContainerItem> source{ medkit(1, 6335), medkit(2, 0, true) };
    const auto original = source;

    EXPECT_FALSE(mwmp::takeAuthoritativeContainerItems(source, "sw_medkit", -1, -1.f, "", 3));
    EXPECT_EQ(source, original);
}

TEST(InventoryAuthority, InstanceIdSelectsExactRowAcrossEnchantRechargeDrift)
{
    mwmp::ContainerItem first;
    first.refId = "dwemer jinksword";
    first.count = 1;
    first.charge = 998;
    first.enchantmentCharge = 180.297f;
    first.instanceId = 9001;
    mwmp::ContainerItem second = first;
    second.enchantmentCharge = 75.f;
    second.instanceId = 9002;
    std::vector<mwmp::ContainerItem> source{ first, second };

    auto taken = mwmp::takeAuthoritativeContainerItems(source, "dwemer jinksword", 998, 180.509f, "", 1,
        [](const auto&, const auto&) { return false; }, 9001);
    ASSERT_TRUE(taken);
    EXPECT_EQ(taken->taken.instanceId, 9001u);
    EXPECT_FLOAT_EQ(taken->taken.enchantmentCharge, 180.297f);
    ASSERT_EQ(source.size(), 1u);
    EXPECT_EQ(source.front().instanceId, 9002u);
}

TEST(InventoryAuthority, StaleNonzeroInstanceIdNeverFallsBackToAnotherRow)
{
    mwmp::ContainerItem sword;
    sword.refId = "dwemer jinksword";
    sword.count = 1;
    sword.charge = 998;
    sword.enchantmentCharge = 180.297f;
    sword.instanceId = 9001;
    std::vector<mwmp::ContainerItem> source{ sword };
    const auto original = source;

    EXPECT_FALSE(mwmp::takeAuthoritativeContainerItems(source, "dwemer jinksword", 998, 180.297f, "", 1,
        [](const auto&, const auto&) { return true; }, 9999));
    EXPECT_EQ(source, original);
}

TEST(InventoryAuthority, ExactTakeDoesNotConsultStackCompatibility)
{
    std::vector<mwmp::ContainerItem> source{ { "iron_cuirass", 1, 130, 10 } };
    auto taken = mwmp::takeAuthoritativeContainerItems(source, "IRON_CUIRASS", 130, -1.f, "", 1,
        [](const auto&, const auto&) -> bool {
            ADD_FAILURE() << "Exact-row removal must not require stack compatibility";
            return false;
        });
    ASSERT_TRUE(taken);
    EXPECT_TRUE(source.empty());
    EXPECT_EQ(taken->taken.instanceId, 10u);
    EXPECT_EQ(taken->taken.charge, 130);
}

TEST(InventoryAuthority, ExactAnchorPrecedesEarlierNativeCompatibleRow)
{
    std::vector<mwmp::ContainerItem> source{ medkit(2, 1, false, 0), medkit(2, 2) };
    auto taken = mwmp::takeAuthoritativeContainerItems(source, "sw_medkit", -1, -1.f, "", 3,
        [](const auto&, const auto&) { return true; });
    ASSERT_TRUE(taken);
    ASSERT_EQ(taken->backingRows.size(), 2u);
    EXPECT_EQ(taken->backingRows[0].instanceId, 2u);
    EXPECT_EQ(taken->backingRows[0].count, 2);
    EXPECT_EQ(taken->backingRows[1].instanceId, 1u);
    ASSERT_EQ(source.size(), 1u);
    EXPECT_EQ(source[0], medkit(1, 1, false, 0));
}

TEST(InventoryAuthority, NonStackableRowsCannotSatisfyAggregateTake)
{
    std::vector<mwmp::ContainerItem> source{ { "sword", 1, 999, 1 }, { "sword", 1, 999, 2 } };
    const auto original = source;
    const auto neverStacks = [](const auto&, const auto&) { return false; };
    EXPECT_FALSE(mwmp::takeAuthoritativeContainerItems(source, "sword", 999, -1.f, "", 2, neverStacks));
    EXPECT_EQ(source, original);
    for (uint32_t id : { 1u, 2u })
    {
        auto taken = mwmp::takeAuthoritativeContainerItems(source, "sword", 999, -1.f, "", 1, neverStacks);
        ASSERT_TRUE(taken);
        EXPECT_EQ(taken->taken.instanceId, id);
    }
    EXPECT_TRUE(source.empty());
}

TEST(InventoryAuthority, ExactRestockingNonStackableTakePreservesTemplate)
{
    std::vector<mwmp::ContainerItem> source{ medkit(2, 0, true, 130) };
    const auto original = source;
    const auto neverStacks = [](const auto&, const auto&) { return false; };
    auto taken = mwmp::takeAuthoritativeContainerItems(source, "sw_medkit", 130, -1.f, "", 2, neverStacks);
    ASSERT_TRUE(taken);
    EXPECT_TRUE(taken->backingRows.empty());
    EXPECT_EQ(source, original);
    EXPECT_FALSE(mwmp::takeAuthoritativeContainerItems(source, "sw_medkit", 130, -1.f, "", 3, neverStacks));
    EXPECT_EQ(source, original);
}

TEST(InventoryAuthority, EmptyExactRowDoesNotMaskAvailableAnchor)
{
    std::vector<mwmp::ContainerItem> source{ medkit(0, 1, true), medkit(1, 2) };
    auto taken = mwmp::takeAuthoritativeContainerItems(source, "sw_medkit", -1, -1.f, "", 1);
    ASSERT_TRUE(taken);
    EXPECT_EQ(taken->taken.instanceId, 2u);
    ASSERT_EQ(source.size(), 1u);
    EXPECT_EQ(source[0], medkit(0, 1, true));
}

TEST(InventoryTakeProtocol, ContainerResetCarriesAuthorityGenerationAndRejectsPayloadItems)
{
    mwmp::PacketContainer outgoing;
    outgoing.container.cellId = "Balmora";
    outgoing.container.refId.clear();
    outgoing.container.refNum = 0;
    outgoing.authorityGeneration = 17;
    outgoing.mAction = static_cast<std::uint8_t>(mwmp::ContainerAction::Reset);

    mwmp::PacketContainer incoming;
    ASSERT_TRUE(incoming.decode(outgoing.encode()));
    EXPECT_EQ(incoming.authorityGeneration, 17u);
    EXPECT_EQ(incoming.mAction, static_cast<std::uint8_t>(mwmp::ContainerAction::Reset));
    EXPECT_TRUE(incoming.container.items.empty());
    EXPECT_TRUE(incoming.container.refId.empty());
    EXPECT_EQ(incoming.container.refNum, 0u);

    outgoing.mAction = static_cast<std::uint8_t>(mwmp::ContainerAction::Set);
    EXPECT_FALSE(incoming.decode(outgoing.encode()));
    outgoing.mAction = static_cast<std::uint8_t>(mwmp::ContainerAction::Reset);

    outgoing.container.refId = "crate_01";
    EXPECT_FALSE(incoming.decode(outgoing.encode()));
    outgoing.container.refId.clear();

    outgoing.container.items.push_back({ "gold_001", 1 });
    EXPECT_FALSE(incoming.decode(outgoing.encode()));
}

TEST(InventoryTakeProtocol, CanonicalRequestBindsAuthorityGeneration)
{
    auto first = request();
    first.source.authorityGeneration = 7;
    auto second = first;
    second.source.authorityGeneration = 8;
    EXPECT_NE(mwmp::canonicalInventoryTakeRequest(first),
        mwmp::canonicalInventoryTakeRequest(second));

    mwmp::PacketInventoryTakeRequest outgoing;
    outgoing.request = first;
    mwmp::PacketInventoryTakeRequest incoming;
    ASSERT_TRUE(incoming.decode(outgoing.encode()));
    EXPECT_EQ(incoming.request.source.authorityGeneration, 7u);
}
