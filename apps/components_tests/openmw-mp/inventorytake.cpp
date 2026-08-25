#include <gtest/gtest.h>

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
    outgoing.container.items.push_back(
        { "daedric dagger", 1, 314, 123456, 87.25f, "golden saint" });
    outgoing.mAction = static_cast<std::uint8_t>(mwmp::ContainerAction::BootstrapRequest);
    const auto encoded = outgoing.encode();
    mwmp::PacketContainer incoming;
    ASSERT_TRUE(incoming.decode(encoded));
    EXPECT_EQ(incoming.container.cellId, outgoing.container.cellId);
    EXPECT_EQ(incoming.container.items, outgoing.container.items);
    EXPECT_EQ(incoming.mAction, outgoing.mAction);

    auto trailing = encoded;
    trailing.push_back(0);
    EXPECT_FALSE(incoming.decode(trailing));

    outgoing.mAction = 99;
    EXPECT_FALSE(incoming.decode(outgoing.encode()));
}
