#include <gtest/gtest.h>

#include <components/openmw-mp/Base/BasePlayer.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerBounty.hpp>
#include <components/openmw-mp/PlayerCrimeState.hpp>
#include <components/openmw-mp/SemanticService.hpp>

namespace
{
    mwmp::PlayerCrimeState makeState(std::uint64_t revision, std::int32_t bounty,
        std::int32_t currentCrimeId = 4, std::int32_t paidCrimeId = 2)
    {
        mwmp::PlayerCrimeState state;
        state.revision = revision;
        state.bounty = bounty;
        state.currentCrimeId = currentCrimeId;
        state.paidCrimeId = paidCrimeId;
        return state;
    }
}

TEST(PlayerCrimeProtocol, PacketRoundTripsSemanticState)
{
    mwmp::BasePlayer outgoing;
    outgoing.guid = 77;
    outgoing.crimeState = makeState(42, 750, 8, 5);

    mwmp::PacketPlayerBounty encoder;
    encoder.setPlayer(&outgoing);
    const auto bytes = encoder.encode();

    mwmp::BasePlayer incoming;
    mwmp::PacketPlayerBounty decoder;
    decoder.setPlayer(&incoming);
    ASSERT_TRUE(decoder.decode(bytes));
    EXPECT_EQ(incoming.guid, 77u);
    EXPECT_EQ(incoming.crimeState, outgoing.crimeState);
}

TEST(PlayerCrimeProtocol, RejectsTruncationTrailingBytesAndHeaderLengthMismatch)
{
    mwmp::BasePlayer outgoing;
    outgoing.guid = 77;
    outgoing.crimeState = makeState(42, 750);
    mwmp::PacketPlayerBounty encoder;
    encoder.setPlayer(&outgoing);
    auto bytes = encoder.encode();

    mwmp::BasePlayer incoming;
    mwmp::PacketPlayerBounty decoder;
    decoder.setPlayer(&incoming);

    auto truncated = bytes;
    truncated.pop_back();
    EXPECT_FALSE(decoder.decode(truncated));

    auto trailing = bytes;
    trailing.push_back(0);
    EXPECT_FALSE(decoder.decode(trailing));

    auto badLength = bytes;
    ++badLength[2]; // payloadSize is little-endian on the supported wire platforms.
    EXPECT_FALSE(decoder.decode(badLength));
}

TEST(PlayerCrimeProtocol, RejectsUnsupportedSchemaAndInvalidRanges)
{
    EXPECT_EQ(mwmp::validatePlayerCrimeState(makeState(1, -1)), mwmp::CrimeError::InvalidBounty);
    EXPECT_EQ(mwmp::validatePlayerCrimeState(makeState(1, 0, -1, 0)), mwmp::CrimeError::InvalidCrimeId);

    auto unsupported = makeState(1, 0);
    unsupported.schemaVersion = mwmp::PlayerCrimeStateSchemaVersion + 1;
    EXPECT_EQ(mwmp::validatePlayerCrimeState(unsupported), mwmp::CrimeError::UnsupportedVersion);

    auto overflow = makeState(mwmp::MaximumPersistedRevision + 1, 0);
    EXPECT_EQ(mwmp::validatePlayerCrimeState(overflow), mwmp::CrimeError::RevisionOverflow);
}

TEST(PlayerCrimeProtocol, CanonicalRequestAndResultEncodingIsStable)
{
    mwmp::CrimeMutationRequest request;
    request.requestId = "crime-request-1";
    request.kind = mwmp::CrimeMutationKind::ModifyBounty;
    request.value = 250;
    request.expectedRevision = 9;
    request.source = "server_lua:test";

    const std::string encoded = mwmp::encodeCrimeMutationRequest(request);
    EXPECT_EQ(encoded, mwmp::encodeCrimeMutationRequest(request));

    mwmp::CrimeMutationResult result;
    result.requestId = request.requestId;
    result.accepted = true;
    result.state = makeState(10, 1000, 8, 5);
    const std::string resultBytes = mwmp::encodeCrimeMutationResult(result);
    EXPECT_EQ(mwmp::decodeCrimeMutationResult(resultBytes), result);

    EXPECT_THROW(mwmp::decodeCrimeMutationResult(resultBytes.substr(0, resultBytes.size() - 1)), std::runtime_error);
    EXPECT_THROW(mwmp::decodeCrimeMutationResult(resultBytes + "x"), std::runtime_error);
}

TEST(PlayerCrimeRevisionGate, OrdersNewerStaleDuplicateAndConflictingStates)
{
    mwmp::RevisionedStateGate<mwmp::PlayerCrimeState> gate;
    const auto revision41 = makeState(41, 500);
    const auto revision42 = makeState(42, 750);

    EXPECT_EQ(gate.receive(revision42), mwmp::RevisionDecision::AcceptedNewer);
    EXPECT_TRUE(gate.hasState());
    EXPECT_TRUE(gate.hasPending());
    ASSERT_EQ(gate.takePending(), revision42);
    EXPECT_FALSE(gate.hasPending());

    EXPECT_EQ(gate.receive(revision41), mwmp::RevisionDecision::Stale);
    EXPECT_EQ(gate.receive(revision42), mwmp::RevisionDecision::IdenticalReplay);
    EXPECT_FALSE(gate.hasPending());

    auto conflict = revision42;
    conflict.bounty = 999;
    EXPECT_EQ(gate.receive(conflict), mwmp::RevisionDecision::Conflict);
    ASSERT_TRUE(gate.latest());
    EXPECT_EQ(*gate.latest(), revision42);
}

TEST(PlayerCrimeRevisionGate, ResetPreventsPriorSessionStateFromWinning)
{
    mwmp::RevisionedStateGate<mwmp::PlayerCrimeState> gate;
    EXPECT_EQ(gate.receive(makeState(50, 500)), mwmp::RevisionDecision::AcceptedNewer);
    gate.reset();
    EXPECT_FALSE(gate.hasState());
    EXPECT_EQ(gate.receive(makeState(7, 70)), mwmp::RevisionDecision::AcceptedNewer);
    ASSERT_TRUE(gate.latest());
    EXPECT_EQ(gate.latest()->revision, 7u);
}
