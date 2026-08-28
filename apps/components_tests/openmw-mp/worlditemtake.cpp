#include <gtest/gtest.h>

#include <components/openmw-mp/Packets/Object/PacketWorldItemTake.hpp>
#include <components/openmw-mp/WorldItemTake.hpp>

namespace
{
    mwmp::WorldItemTakeRequest request()
    {
        mwmp::WorldItemTakeRequest value;
        value.requestId = "take-42-1";
        value.object.kind = mwmp::PlacedObjectKind::ContentReference;
        value.object.cellId = "Balmora, Guild of Mages";
        value.object.refId = "misc_com_bottle_01";
        value.object.refIndex = 123;
        value.object.refContentFile = 1;
        value.requestedCount = 1;
        value.expectedInventoryRevision = 9;
        return value;
    }
}

TEST(WorldItemTakeProtocol, CanonicalIdentityRejectsMixedAndIncompleteKeys)
{
    auto value = request();
    EXPECT_TRUE(mwmp::isCanonicalPlacedObjectIdentity(value.object));
    value.object.mpNum = 5;
    EXPECT_FALSE(mwmp::isCanonicalPlacedObjectIdentity(value.object));
    value = request();
    value.object.kind = mwmp::PlacedObjectKind::ServerPlaced;
    value.object.refIndex = 0;
    value.object.refContentFile = -1;
    value.object.mpNum = 5;
    EXPECT_TRUE(mwmp::isCanonicalPlacedObjectIdentity(value.object));
}

TEST(WorldItemTakeProtocol, ConsumedServerPlacedMpNumsRemainRetired)
{
    mwmp::PlacedObjectIdentity content = request().object;
    mwmp::PlacedObjectIdentity retired;
    retired.kind = mwmp::PlacedObjectKind::ServerPlaced;
    retired.cellId = "Balmora";
    retired.refId = "misc_com_bottle_01";
    retired.refContentFile = -1;
    retired.mpNum = 6441;

    const std::vector<mwmp::PlacedObjectIdentity> identities{ content, retired };
    EXPECT_TRUE(mwmp::containsRetiredServerPlacedMpNum(identities, 6441));
    EXPECT_FALSE(mwmp::containsRetiredServerPlacedMpNum(identities, 6442));
    EXPECT_FALSE(mwmp::containsRetiredServerPlacedMpNum(identities, 0));
}

TEST(WorldItemTakeProtocol, RequestValidationAndCanonicalEncodingAreDeterministic)
{
    const auto value = request();
    EXPECT_EQ(mwmp::validateWorldItemTakeRequest(value), mwmp::WorldItemTakeError::None);
    EXPECT_EQ(mwmp::canonicalWorldItemTakeRequest(value), mwmp::canonicalWorldItemTakeRequest(value));
    auto invalid = value;
    invalid.requestedCount = 0;
    EXPECT_EQ(mwmp::validateWorldItemTakeRequest(invalid), mwmp::WorldItemTakeError::InvalidCount);
}

TEST(WorldItemTakeProtocol, RequestAndResultPacketsRoundTrip)
{
    mwmp::PacketWorldItemTakeRequest outgoingRequest;
    outgoingRequest.request = request();
    const auto requestBytes = outgoingRequest.encode();
    mwmp::PacketWorldItemTakeRequest incomingRequest;
    ASSERT_TRUE(incomingRequest.decode(requestBytes.data(), requestBytes.size()));
    EXPECT_EQ(incomingRequest.request, outgoingRequest.request);

    mwmp::PacketWorldItemTakeResult outgoingResult;
    outgoingResult.result.requestId = "take-42-1";
    outgoingResult.result.accepted = true;
    outgoingResult.result.object = request().object;
    outgoingResult.result.itemRefId = "misc_com_bottle_01";
    outgoingResult.result.itemCount = 1;
    outgoingResult.result.crimeValue = 2;
    outgoingResult.result.theft = true;
    outgoingResult.result.inventoryRevision = 10;
    const auto resultBytes = outgoingResult.encode();
    mwmp::PacketWorldItemTakeResult incomingResult;
    ASSERT_TRUE(incomingResult.decode(resultBytes.data(), resultBytes.size()));
    EXPECT_EQ(incomingResult.result, outgoingResult.result);
}
