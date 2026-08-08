#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include <components/openmw-mp/Packets/Records/PacketEnchantingRequest.hpp>
#include <components/openmw-mp/Packets/Records/PacketEnchantingResult.hpp>
#include <components/openmw-mp/Records/EnchantingProtocol.hpp>

namespace
{
    mwmp::records::EnchantingEffectChoice choice(const char* effectId, int magMin, int magMax, int duration, int area)
    {
        mwmp::records::EnchantingEffectChoice result;
        result.effectId = effectId;
        result.magnitudeMin = magMin;
        result.magnitudeMax = magMax;
        result.duration = duration;
        result.area = area;
        return result;
    }
}

TEST(EnchantingProtocol, RequestRoundTripsSemanticInputs)
{
    mwmp::records::EnchantingRequest request;
    request.protocolVersion = mwmp::records::CurrentEnchantingProtocolVersion;
    request.requestId = "client-enchanting-1";
    request.inventoryRevision = 42;
    request.targetInstanceId = 11;
    request.soulGemInstanceId = 22;
    request.castStyle = 2;
    request.itemName = "Sword of Testing";
    request.selfEnchanting = false;
    request.enchanterNetId = 0x1234567890ABCDEFull;
    request.effects = { choice("FireDamage", 1, 10, 5, 0), choice("FrostDamage", 0, 0, 1, 2) };
    request.effects[0].skillId = "longblade";
    request.effects[1].attributeId = "strength";

    mwmp::PacketEnchantingRequest packet;
    packet.request = request;
    const std::vector<uint8_t> bytes = packet.encode(7);

    mwmp::PacketEnchantingRequest decoded;
    ASSERT_TRUE(decoded.decode(bytes));
    EXPECT_EQ(decoded.request, request);
    EXPECT_EQ(decoded.getSequence(), 7u);
}

TEST(EnchantingProtocol, ResultRoundTripsTerminalState)
{
    mwmp::records::EnchantingResult result;
    result.protocolVersion = mwmp::records::CurrentEnchantingProtocolVersion;
    result.requestId = "client-enchanting-2";
    result.accepted = true;
    result.error = mwmp::records::EnchantingError::None;
    result.success = true;
    result.inventoryRevision = 43;
    result.commitSequence = 9;
    result.enchantmentRecordId = "$custom_enchantment_1";
    result.itemRecordId = "$custom_weapon_1";
    result.enchantmentReused = true;
    result.itemReused = false;

    mwmp::PacketEnchantingResult packet;
    packet.result = result;
    const std::vector<uint8_t> bytes = packet.encode();

    mwmp::PacketEnchantingResult decoded;
    ASSERT_TRUE(decoded.decode(bytes));
    EXPECT_EQ(decoded.result, result);
}

TEST(EnchantingProtocol, MalformedRequestIsRejected)
{
    mwmp::PacketEnchantingRequest decoded;
    // Truncated payloads must not decode.
    EXPECT_FALSE(decoded.decode(std::vector<uint8_t>{}));
    EXPECT_FALSE(decoded.decode(std::vector<uint8_t>{ 1, 2, 3 }));

    // Valid header but truncated body.
    mwmp::PacketEnchantingRequest packet;
    packet.request.requestId = "id";
    const std::vector<uint8_t> bytes = packet.encode();
    EXPECT_FALSE(decoded.decode(std::vector<uint8_t>(bytes.begin(), bytes.begin() + bytes.size() / 2)));

    // Trailing garbage after the payload is rejected.
    std::vector<uint8_t> trailing = bytes;
    trailing.push_back(0xAA);
    EXPECT_FALSE(decoded.decode(trailing));
}

TEST(EnchantingProtocol, OversizedRequestIsRejected)
{
    mwmp::records::EnchantingRequest request;
    request.requestId = "oversized";
    // More effects than the native UI supports.
    for (std::uint32_t i = 0; i < mwmp::records::MaxEnchantingEffects + 1; ++i)
        request.effects.push_back(choice("FireDamage", 1, 1, 1, 0));
    mwmp::PacketEnchantingRequest packet;
    packet.request = request;
    const std::vector<uint8_t> bytes = packet.encode();

    mwmp::PacketEnchantingRequest decoded;
    EXPECT_FALSE(decoded.decode(bytes));

    // A huge declared effect count must not trigger an unbounded allocation.
    mwmp::records::EnchantingRequest huge;
    huge.requestId = "huge";
    mwmp::PacketEnchantingRequest hugePacket;
    hugePacket.request = huge;
    std::vector<uint8_t> hugeBytes = hugePacket.encode();
    // Rewrite the effect count field to UINT32_MAX. Payload layout: version(2)
    // + requestId string(2+len) + revision(8) + target(4) + gem(4)
    // + castStyle(4) + name string(2+len) + selfEnchanting(1) + enchanter(8)
    // + effect count(4), after the 10-byte wire header.
    const std::size_t countOffset = mwmp::PacketHeader::WIRE_SIZE + 2 + (2 + huge.requestId.size()) + 8 + 4 + 4 + 4
        + (2 + huge.itemName.size()) + 1 + 8;
    ASSERT_LE(countOffset + 4, hugeBytes.size());
    for (std::size_t i = 0; i < 4; ++i)
        hugeBytes[countOffset + i] = 0xFF;
    EXPECT_FALSE(decoded.decode(hugeBytes));
}

TEST(EnchantingProtocol, OversizedItemNameIsRejected)
{
    mwmp::records::EnchantingRequest request;
    request.requestId = "long-name";
    request.itemName.assign(mwmp::records::MaxEnchantingItemNameLength + 1, 'x');
    mwmp::PacketEnchantingRequest packet;
    packet.request = request;
    const std::vector<uint8_t> bytes = packet.encode();

    mwmp::PacketEnchantingRequest decoded;
    EXPECT_FALSE(decoded.decode(bytes));
}

TEST(EnchantingProtocol, InvalidEnumsAndRangesAreRejected)
{
    // Invalid cast style.
    {
        mwmp::records::EnchantingRequest request;
        request.requestId = "bad-style";
        request.castStyle = 9;
        mwmp::PacketEnchantingRequest packet;
        packet.request = request;
        mwmp::PacketEnchantingRequest decoded;
        EXPECT_FALSE(decoded.decode(packet.encode()));
    }
    // Invalid effect range.
    {
        mwmp::records::EnchantingRequest request;
        request.requestId = "bad-range";
        request.effects.push_back(choice("FireDamage", 1, 1, 1, 0));
        request.effects[0].range = 7;
        mwmp::PacketEnchantingRequest packet;
        packet.request = request;
        mwmp::PacketEnchantingRequest decoded;
        EXPECT_FALSE(decoded.decode(packet.encode()));
    }
    // Negative magnitude.
    {
        mwmp::records::EnchantingRequest request;
        request.requestId = "bad-magnitude";
        request.effects.push_back(choice("FireDamage", -1, 1, 1, 0));
        mwmp::PacketEnchantingRequest packet;
        packet.request = request;
        mwmp::PacketEnchantingRequest decoded;
        EXPECT_FALSE(decoded.decode(packet.encode()));
    }
    // Magnitude above the protocol bound.
    {
        mwmp::records::EnchantingRequest request;
        request.requestId = "huge-magnitude";
        request.effects.push_back(
            choice("FireDamage", 1, mwmp::records::MaxEnchantingMagnitude + 1, 1, 0));
        mwmp::PacketEnchantingRequest packet;
        packet.request = request;
        mwmp::PacketEnchantingRequest decoded;
        EXPECT_FALSE(decoded.decode(packet.encode()));
    }
    // Oversized effect id.
    {
        mwmp::records::EnchantingRequest request;
        request.requestId = "huge-effect-id";
        request.effects.push_back(choice("FireDamage", 1, 1, 1, 0));
        request.effects[0].effectId.assign(mwmp::records::MaxEnchantingEffectIdLength + 1, 'x');
        mwmp::PacketEnchantingRequest packet;
        packet.request = request;
        mwmp::PacketEnchantingRequest decoded;
        EXPECT_FALSE(decoded.decode(packet.encode()));
    }
}

TEST(EnchantingProtocol, ResultRejectsOversizedIds)
{
    mwmp::records::EnchantingResult result;
    result.requestId = "big";
    result.enchantmentRecordId.assign(mwmp::records::MaxEnchantingEffectIdLength + 1, 'x');
    mwmp::PacketEnchantingResult packet;
    packet.result = result;
    const std::vector<uint8_t> bytes = packet.encode();

    mwmp::PacketEnchantingResult decoded;
    EXPECT_FALSE(decoded.decode(bytes));
}

TEST(EnchantingProtocol, ErrorCodesAreStableAndDescriptive)
{
    EXPECT_EQ(mwmp::records::getEnchantingErrorCode(mwmp::records::EnchantingError::StaleInventoryRevision),
        "enchanting_stale_inventory_revision");
    EXPECT_EQ(mwmp::records::getEnchantingErrorCode(mwmp::records::EnchantingError::TargetItemNotFound),
        "enchanting_target_item_not_found");
    EXPECT_EQ(mwmp::records::getEnchantingErrorCode(mwmp::records::EnchantingError::EmptySoul),
        "enchanting_empty_soul");
    EXPECT_EQ(mwmp::records::getEnchantingErrorCode(mwmp::records::EnchantingError::EffectNotAllowed),
        "enchanting_effect_not_allowed");
    EXPECT_EQ(mwmp::records::getEnchantingErrorCode(mwmp::records::EnchantingError::InsufficientGold),
        "enchanting_insufficient_gold");
    EXPECT_EQ(mwmp::records::getEnchantingErrorCode(mwmp::records::EnchantingError::None), "none");
    EXPECT_EQ(mwmp::records::getEnchantingErrorCode(static_cast<mwmp::records::EnchantingError>(999)),
        "enchanting_unknown_error");
}
