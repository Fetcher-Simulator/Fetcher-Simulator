#include <gtest/gtest.h>

#include <components/openmw-mp/InventoryPut.hpp>
#include <components/openmw-mp/Packets/Object/PacketInventoryPut.hpp>

namespace
{
    mwmp::InventoryPutRequest request()
    {
        mwmp::InventoryPutRequest value;
        value.requestId = "inventory-put-1";
        value.destination.cellId = "Balmora";
        value.destination.refId = "crate_01";
        value.destination.refNum = 42;
        value.itemRefId = "iron_cuirass";
        value.itemInstanceId = 9001;
        value.itemCharge = 320;
        value.requestedCount = 1;
        value.expectedInventoryRevision = 9;
        return value;
    }
}

TEST(InventoryPutProtocol, RequestRoundTripsCanonically)
{
    mwmp::PacketInventoryPutRequest outgoing;
    outgoing.request = request();
    const auto encoded = outgoing.encode();
    EXPECT_EQ(encoded, outgoing.encode());

    mwmp::PacketInventoryPutRequest incoming;
    ASSERT_TRUE(incoming.decode(encoded));
    EXPECT_EQ(incoming.request, outgoing.request);
    EXPECT_EQ(mwmp::canonicalInventoryPutRequest(incoming.request),
        mwmp::canonicalInventoryPutRequest(outgoing.request));

    auto trailing = encoded;
    trailing.push_back(0);
    EXPECT_FALSE(incoming.decode(trailing));
}

TEST(InventoryPutProtocol, ResultRoundTripsAuthoritativeIdentity)
{
    mwmp::PacketInventoryPutResult outgoing;
    outgoing.result.requestId = request().requestId;
    outgoing.result.accepted = true;
    outgoing.result.destination = request().destination;
    outgoing.result.itemRefId = request().itemRefId;
    outgoing.result.itemInstanceId = request().itemInstanceId;
    outgoing.result.itemCharge = request().itemCharge;
    outgoing.result.itemCount = request().requestedCount;
    outgoing.result.inventoryRevision = 10;

    mwmp::PacketInventoryPutResult incoming;
    ASSERT_TRUE(incoming.decode(outgoing.encode()));
    EXPECT_EQ(incoming.result, outgoing.result);
}

TEST(InventoryPutProtocol, RejectsMissingStableIdentityAndAmbiguousDestination)
{
    auto value = request();
    value.itemInstanceId = 0;
    EXPECT_EQ(mwmp::validateInventoryPutRequest(value), mwmp::InventoryPutError::InvalidRequest);

    value = request();
    value.destination.mpNum = 7;
    EXPECT_EQ(mwmp::validateInventoryPutRequest(value), mwmp::InventoryPutError::InvalidRequest);

    value = request();
    value.requestedCount = 0;
    EXPECT_EQ(mwmp::validateInventoryPutRequest(value), mwmp::InventoryPutError::InvalidCount);
}

TEST(InventoryPutProtocol, AcceptsCanonicalVanillaAndSpawnedActorDestinations)
{
    auto vanilla = request();
    vanilla.destination.refId = "hlaalu guard_outside";
    vanilla.destination.refNum = 428720;
    vanilla.destination.actorInstanceId
        = mwmp::packActorInstanceKey({ mwmp::ActorKeyKind::VanillaRefNum, vanilla.destination.refNum });
    vanilla.destination.migrationGeneration = 3;
    EXPECT_EQ(mwmp::validateInventoryPutRequest(vanilla), mwmp::InventoryPutError::None);

    mwmp::PacketInventoryPutRequest vanillaPacket;
    vanillaPacket.request = vanilla;
    mwmp::PacketInventoryPutRequest vanillaDecoded;
    ASSERT_TRUE(vanillaDecoded.decode(vanillaPacket.encode()));
    EXPECT_EQ(vanillaDecoded.request, vanilla);

    auto spawned = vanilla;
    spawned.destination.refId = "r_bc_dyn_bard_fargoth";
    spawned.destination.refNum = 0;
    spawned.destination.mpNum = 5901;
    spawned.destination.actorInstanceId
        = mwmp::packActorInstanceKey({ mwmp::ActorKeyKind::SpawnedMpNum, spawned.destination.mpNum });
    EXPECT_EQ(mwmp::validateInventoryPutRequest(spawned), mwmp::InventoryPutError::None);

    auto mismatch = spawned;
    mismatch.destination.actorInstanceId
        = mwmp::packActorInstanceKey({ mwmp::ActorKeyKind::SpawnedMpNum, spawned.destination.mpNum + 1 });
    EXPECT_EQ(mwmp::validateInventoryPutRequest(mismatch), mwmp::InventoryPutError::InvalidRequest);
}
