#include <components/openmw-mp/Packets/Worldstate/PacketRuntimeContentBootstrapComplete.hpp>
#include <components/openmw-mp/RuntimeContentBootstrapGate.hpp>

#include <gtest/gtest.h>

namespace
{
    struct CharacterData
    {
        int characterId = 0;
        bool operator==(const CharacterData&) const = default;
    };

    using Gate = mwmp::RuntimeContentBootstrapGate<CharacterData>;
}

TEST(RuntimeContentBootstrap, CompletionPacketRoundTripsOnWorldstateType)
{
    mwmp::PacketRuntimeContentBootstrapComplete outgoing;
    const std::vector<std::uint8_t> encoded = outgoing.encode();

    mwmp::PacketHeader header;
    ASSERT_TRUE(mwmp::BasePacket::peekHeader(encoded.data(), encoded.size(), header));
    EXPECT_EQ(static_cast<mwmp::PacketType>(header.type), mwmp::PacketType::RuntimeContentBootstrapComplete);
    EXPECT_GT(header.type, static_cast<std::uint16_t>(mwmp::PacketType::ActorAttackV2));

    mwmp::PacketRuntimeContentBootstrapComplete incoming;
    EXPECT_TRUE(incoming.decode(encoded));

    std::vector<std::uint8_t> malformed = encoded;
    malformed.push_back(0);
    EXPECT_FALSE(incoming.decode(malformed));
}

TEST(RuntimeContentBootstrap, NaturalOrderFinalizesCharacterData)
{
    Gate gate;
    EXPECT_TRUE(gate.finish(true));
    gate.retainCharacterData({ 1 });

    const auto ready = gate.takeReadyCharacterData();
    ASSERT_TRUE(ready.has_value());
    EXPECT_EQ(ready->characterId, 1);
}

TEST(RuntimeContentBootstrap, OvertakenCharacterDataWaitsForCompletion)
{
    Gate gate;
    gate.retainCharacterData({ 2 });

    EXPECT_FALSE(gate.takeReadyCharacterData().has_value());
    EXPECT_TRUE(gate.hasPendingCharacterData());
    EXPECT_TRUE(gate.finish(true));

    const auto ready = gate.takeReadyCharacterData();
    ASSERT_TRUE(ready.has_value());
    EXPECT_EQ(ready->characterId, 2);
}

TEST(RuntimeContentBootstrap, CompletionBeforeLaterCharacterDataFinalizesImmediately)
{
    Gate gate;
    EXPECT_TRUE(gate.finish(true));
    EXPECT_FALSE(gate.takeReadyCharacterData().has_value());

    gate.retainCharacterData({ 3 });
    EXPECT_EQ(gate.takeReadyCharacterData(), CharacterData{ 3 });
}

TEST(RuntimeContentBootstrap, UnresolvedRequiredDefinitionFailsWithoutRelease)
{
    Gate gate;
    gate.retainCharacterData({ 4 });

    EXPECT_FALSE(gate.finish(false, "unresolved script override"));
    EXPECT_EQ(gate.state(), Gate::State::Failed);
    EXPECT_EQ(gate.error(), "unresolved script override");
    EXPECT_FALSE(gate.takeReadyCharacterData().has_value());
    EXPECT_TRUE(gate.hasPendingCharacterData());
}

TEST(RuntimeContentBootstrap, ResetPreventsPriorSessionCompletionFromReleasingReconnect)
{
    Gate gate;
    ASSERT_TRUE(gate.finish(true));
    gate.retainCharacterData({ 5 });
    ASSERT_TRUE(gate.takeReadyCharacterData().has_value());

    gate.reset();
    gate.retainCharacterData({ 6 });
    EXPECT_FALSE(gate.isContentReady());
    EXPECT_FALSE(gate.takeReadyCharacterData().has_value());

    ASSERT_TRUE(gate.finish(true));
    EXPECT_EQ(gate.takeReadyCharacterData(), CharacterData{ 6 });
}
