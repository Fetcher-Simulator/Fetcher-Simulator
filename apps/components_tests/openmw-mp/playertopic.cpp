#include <gtest/gtest.h>

#include <components/openmw-mp/Base/BasePlayer.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerTopic.hpp>
#include <components/openmw-mp/PlayerCrimeState.hpp>
#include <components/openmw-mp/PlayerTopicState.hpp>
#include <components/openmw-mp/SemanticService.hpp>

namespace
{
    mwmp::PlayerTopicState makeState(std::uint64_t revision, std::vector<std::string> topics)
    {
        mwmp::PlayerTopicState state;
        state.revision = revision;
        state.knownTopicIds = std::move(topics);
        return state;
    }
}

TEST(PlayerTopicState, CanonicalizesCaseOrderAndDuplicates)
{
    const std::vector<std::string> canonical
        = mwmp::canonicalizeTopicIds({ "Z Topic", "a topic", "A TOPIC", "middle" });
    EXPECT_EQ(canonical, (std::vector<std::string>{ "a topic", "middle", "z topic" }));
}

TEST(PlayerTopicState, RejectsInvalidUtf8LimitsAndNonCanonicalSets)
{
    EXPECT_EQ(mwmp::validatePlayerTopicState(makeState(1, { "" })), mwmp::TopicStateError::InvalidTopicId);
    EXPECT_EQ(mwmp::validatePlayerTopicState(makeState(1, { std::string("bad\xc0\xaf", 5) })),
        mwmp::TopicStateError::InvalidTopicId);
    EXPECT_EQ(mwmp::validatePlayerTopicState(makeState(1, { "Topic" })),
        mwmp::TopicStateError::NonCanonicalTopics);
    EXPECT_EQ(mwmp::validatePlayerTopicState(makeState(1, { "a", "a" })),
        mwmp::TopicStateError::NonCanonicalTopics);

    auto tooMany = makeState(1, {});
    tooMany.knownTopicIds.resize(mwmp::MaximumKnownTopics + 1, "topic");
    EXPECT_EQ(mwmp::validatePlayerTopicState(tooMany), mwmp::TopicStateError::TooManyTopics);

    auto overflow = makeState(mwmp::MaximumPersistedRevision + 1, {});
    EXPECT_EQ(mwmp::validatePlayerTopicState(overflow), mwmp::TopicStateError::RevisionOverflow);
}

TEST(PlayerTopicProtocol, RoundTripsAddAndAuthoritativeSet)
{
    for (const mwmp::PacketPlayerTopic::Action action : {
             mwmp::PacketPlayerTopic::Action::Add, mwmp::PacketPlayerTopic::Action::Set })
    {
        mwmp::BasePlayer outgoing;
        outgoing.guid = 77;
        outgoing.topicState = makeState(42, { "latest rumors", "secret topic" });

        mwmp::PacketPlayerTopic encoder;
        encoder.action = action;
        encoder.setPlayer(&outgoing);
        const auto bytes = encoder.encode();

        mwmp::BasePlayer incoming;
        mwmp::PacketPlayerTopic decoder;
        decoder.setPlayer(&incoming);
        ASSERT_TRUE(decoder.decode(bytes));
        EXPECT_EQ(decoder.action, action);
        EXPECT_EQ(incoming.guid, outgoing.guid);
        EXPECT_EQ(incoming.topicState, outgoing.topicState);
    }
}

TEST(PlayerTopicProtocol, RejectsTruncationTrailingBytesAndInvalidState)
{
    mwmp::BasePlayer outgoing;
    outgoing.guid = 77;
    outgoing.topicState = makeState(42, { "latest rumors" });
    mwmp::PacketPlayerTopic encoder;
    encoder.setPlayer(&outgoing);
    auto bytes = encoder.encode();

    mwmp::BasePlayer incoming;
    mwmp::PacketPlayerTopic decoder;
    decoder.setPlayer(&incoming);

    auto truncated = bytes;
    truncated.pop_back();
    EXPECT_FALSE(decoder.decode(truncated));

    auto trailing = bytes;
    trailing.push_back(0);
    EXPECT_FALSE(decoder.decode(trailing));

    outgoing.topicState.knownTopicIds = { "Noncanonical" };
    const auto invalid = encoder.encode();
    EXPECT_FALSE(decoder.decode(invalid));
}

TEST(PlayerTopicRevisionGate, OrdersNewerStaleDuplicateAndConflictingStates)
{
    mwmp::RevisionedStateGate<mwmp::PlayerTopicState> gate;
    const auto revision41 = makeState(41, { "one" });
    const auto revision42 = makeState(42, { "one", "two" });

    EXPECT_EQ(gate.receive(revision42), mwmp::RevisionDecision::AcceptedNewer);
    ASSERT_EQ(gate.takePending(), revision42);
    EXPECT_EQ(gate.receive(revision41), mwmp::RevisionDecision::Stale);
    EXPECT_EQ(gate.receive(revision42), mwmp::RevisionDecision::IdenticalReplay);

    auto conflict = revision42;
    conflict.knownTopicIds.push_back("three");
    EXPECT_EQ(gate.receive(conflict), mwmp::RevisionDecision::Conflict);
    ASSERT_TRUE(gate.latest());
    EXPECT_EQ(*gate.latest(), revision42);
}

TEST(PlayerTopicRevisionGate, ResetAllowsARevisionFromANewSession)
{
    mwmp::RevisionedStateGate<mwmp::PlayerTopicState> gate;
    EXPECT_EQ(gate.receive(makeState(50, { "old session" })), mwmp::RevisionDecision::AcceptedNewer);
    gate.reset();
    EXPECT_FALSE(gate.hasState());
    EXPECT_EQ(gate.receive(makeState(7, { "new session" })), mwmp::RevisionDecision::AcceptedNewer);
}
