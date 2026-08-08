#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include <components/openmw-mp/Packets/Records/PacketAlchemyRequest.hpp>
#include <components/openmw-mp/Packets/Records/PacketAlchemyResult.hpp>
#include <components/openmw-mp/Records/AlchemyProtocol.hpp>

TEST(AlchemyProtocol, RequestRoundTripsSemanticInputs)
{
    mwmp::records::AlchemyRequest request;
    request.protocolVersion = mwmp::records::CurrentAlchemyProtocolVersion;
    request.requestId = "client-alchemy-1";
    request.inventoryRevision = 42;
    request.potionName = "Potion of Testing";
    request.count = 3;
    request.ingredientInstanceIds = { 11, 22, 33, 44 };
    request.apparatusInstanceIds = { 55, 66 };

    mwmp::PacketAlchemyRequest packet;
    packet.request = request;
    const std::vector<uint8_t> bytes = packet.encode(7);

    mwmp::PacketAlchemyRequest decoded;
    ASSERT_TRUE(decoded.decode(bytes));
    EXPECT_EQ(decoded.request, request);
    EXPECT_EQ(decoded.getSequence(), 7u);
}

TEST(AlchemyProtocol, ResultRoundTripsTerminalState)
{
    mwmp::records::AlchemyResult result;
    result.protocolVersion = mwmp::records::CurrentAlchemyProtocolVersion;
    result.requestId = "client-alchemy-2";
    result.accepted = true;
    result.error = mwmp::records::AlchemyError::None;
    result.inventoryRevision = 43;
    result.commitSequence = 9;
    result.attempts = {
        { true, "$custom_potion_1", false },
        { false, {}, false },
        { true, "$custom_potion_1", true },
    };

    mwmp::PacketAlchemyResult packet;
    packet.result = result;
    const std::vector<uint8_t> bytes = packet.encode();

    mwmp::PacketAlchemyResult decoded;
    ASSERT_TRUE(decoded.decode(bytes));
    EXPECT_EQ(decoded.result, result);
}

TEST(AlchemyProtocol, MalformedRequestIsRejected)
{
    mwmp::PacketAlchemyRequest decoded;
    // Truncated payloads must not decode.
    EXPECT_FALSE(decoded.decode(std::vector<uint8_t>{}));
    EXPECT_FALSE(decoded.decode(std::vector<uint8_t>{ 1, 2, 3 }));

    // Valid header but truncated body.
    mwmp::PacketAlchemyRequest packet;
    packet.request.requestId = "id";
    const std::vector<uint8_t> bytes = packet.encode();
    EXPECT_FALSE(decoded.decode(std::vector<uint8_t>(bytes.begin(), bytes.begin() + bytes.size() / 2)));

    // Trailing garbage after the payload is rejected.
    std::vector<uint8_t> trailing = bytes;
    trailing.push_back(0xAA);
    EXPECT_FALSE(decoded.decode(trailing));
}

TEST(AlchemyProtocol, OversizedRequestIsRejected)
{
    mwmp::records::AlchemyRequest request;
    request.requestId = "oversized";
    // More ingredient slots than native alchemy supports.
    for (std::uint32_t i = 0; i < mwmp::records::MaxAlchemyIngredients + 1; ++i)
        request.ingredientInstanceIds.push_back(i + 1);
    mwmp::PacketAlchemyRequest packet;
    packet.request = request;
    const std::vector<uint8_t> bytes = packet.encode();

    mwmp::PacketAlchemyRequest decoded;
    EXPECT_FALSE(decoded.decode(bytes));

    // A huge declared count must not trigger an unbounded allocation.
    mwmp::records::AlchemyRequest huge;
    huge.requestId = "huge";
    mwmp::PacketAlchemyRequest hugePacket;
    hugePacket.request = huge;
    std::vector<uint8_t> hugeBytes = hugePacket.encode();
    // Rewrite the ingredient count field to UINT32_MAX. Payload layout:
    // version(2) + requestId string(2+len) + revision(8) + name string(2+len)
    // + count(4) + ingredient count(4), after the 10-byte wire header.
    const std::size_t countOffset = mwmp::PacketHeader::WIRE_SIZE + 2 + (2 + huge.requestId.size()) + 8
        + (2 + huge.potionName.size()) + 4;
    ASSERT_LE(countOffset + 4, hugeBytes.size());
    for (std::size_t i = 0; i < 4; ++i)
        hugeBytes[countOffset + i] = 0xFF;
    EXPECT_FALSE(decoded.decode(hugeBytes));
}

TEST(AlchemyProtocol, OversizedPotionNameIsRejected)
{
    mwmp::records::AlchemyRequest request;
    request.requestId = "long-name";
    request.potionName.assign(mwmp::records::MaxAlchemyPotionNameLength + 1, 'x');
    mwmp::PacketAlchemyRequest packet;
    packet.request = request;
    const std::vector<uint8_t> bytes = packet.encode();

    mwmp::PacketAlchemyRequest decoded;
    EXPECT_FALSE(decoded.decode(bytes));
}

TEST(AlchemyProtocol, ResultRejectsTooManyAttempts)
{
    mwmp::records::AlchemyResult result;
    result.requestId = "too-many";
    for (std::uint32_t i = 0; i < mwmp::records::MaxAlchemyAttempts + 1; ++i)
        result.attempts.push_back({ true, "$custom_potion_x", false });
    mwmp::PacketAlchemyResult packet;
    packet.result = result;
    const std::vector<uint8_t> bytes = packet.encode();

    mwmp::PacketAlchemyResult decoded;
    EXPECT_FALSE(decoded.decode(bytes));
}

TEST(AlchemyProtocol, ErrorCodesAreStableAndDescriptive)
{
    EXPECT_EQ(mwmp::records::getAlchemyErrorCode(mwmp::records::AlchemyError::StaleInventoryRevision),
        "alchemy_stale_inventory_revision");
    EXPECT_EQ(mwmp::records::getAlchemyErrorCode(mwmp::records::AlchemyError::IngredientNotFound),
        "alchemy_ingredient_not_found");
    EXPECT_EQ(mwmp::records::getAlchemyErrorCode(mwmp::records::AlchemyError::DuplicateRequestConflict),
        "alchemy_duplicate_request_conflict");
    EXPECT_EQ(mwmp::records::getAlchemyErrorCode(mwmp::records::AlchemyError::None), "none");
    EXPECT_EQ(mwmp::records::getAlchemyErrorCode(static_cast<mwmp::records::AlchemyError>(999)),
        "alchemy_unknown_error");
}
