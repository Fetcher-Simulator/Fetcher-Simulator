#include <gtest/gtest.h>

#include <components/openmw-mp/Base/BasePlayer.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerFaction.hpp>
#include <components/openmw-mp/PlayerFactionState.hpp>
#include <components/openmw-mp/SemanticService.hpp>

namespace
{
    mwmp::PlayerFactionEntry entry(
        std::string id, std::int32_t rank, std::int32_t reputation = 0, bool expelled = false)
    {
        return { std::move(id), rank, reputation, expelled };
    }

    mwmp::PlayerFactionState state(std::uint64_t revision, std::vector<mwmp::PlayerFactionEntry> factions)
    {
        mwmp::PlayerFactionState result;
        result.revision = revision;
        result.factions = std::move(factions);
        return result;
    }

    mwmp::FactionMutationRequest request(std::vector<mwmp::FactionMutation> mutations)
    {
        mwmp::FactionMutationRequest result;
        result.requestId = "faction-request-1";
        result.mutations = std::move(mutations);
        result.expectedRevision = 7;
        result.source = "test:faction";
        return result;
    }
}

TEST(PlayerFactionState, CanonicalizesIdsAndOrderingWithoutDiscardingTuples)
{
    const mwmp::PlayerFactionState canonical
        = mwmp::canonicalizePlayerFactionState(state(4, { entry("Z Guild", 2, 8, true), entry("a guild", 0) }));
    ASSERT_EQ(canonical.factions.size(), 2u);
    EXPECT_EQ(canonical.factions[0], entry("a guild", 0));
    EXPECT_EQ(canonical.factions[1], entry("z guild", 2, 8, true));
    EXPECT_EQ(mwmp::validatePlayerFactionState(canonical), mwmp::FactionError::None);
}

TEST(PlayerFactionState, RejectsDuplicatesDefaultsInvalidUtf8AndInvalidRanks)
{
    EXPECT_EQ(mwmp::validatePlayerFactionState(state(1, { entry("guild", -1) })), mwmp::FactionError::InvalidRequest);
    EXPECT_EQ(mwmp::validatePlayerFactionState(state(1, { entry("guild", 0), entry("guild", 1) })),
        mwmp::FactionError::InvalidRequest);
    EXPECT_EQ(mwmp::validatePlayerFactionState(state(1, { entry("Guild", 0) })), mwmp::FactionError::InvalidFaction);
    EXPECT_EQ(mwmp::validatePlayerFactionState(state(1, { entry(std::string("bad\xc0\xaf", 5), 0) })),
        mwmp::FactionError::InvalidFaction);
    EXPECT_EQ(mwmp::validatePlayerFactionState(state(1, { entry("guild", mwmp::MaximumProtocolFactionRank + 1) })),
        mwmp::FactionError::InvalidRank);
}

TEST(PlayerFactionState, DerivesTypedTransitionsIncludingLeaveExpulsionSideEffect)
{
    const mwmp::PlayerFactionState authoritative
        = state(7, { entry("fighters guild", 2, 4, true), entry("mages guild", -1, 9, true) });
    const mwmp::PlayerFactionState desired
        = state(7, { entry("fighters guild", -1, 5, true), entry("mages guild", 3, 9, false) });

    const std::vector<mwmp::FactionMutation> mutations = mwmp::deriveFactionMutations(authoritative, desired);
    EXPECT_EQ(mutations,
        (std::vector<mwmp::FactionMutation>{
            { mwmp::FactionMutationKind::LeaveFaction, "fighters guild", 0 },
            { mwmp::FactionMutationKind::SetFactionReputation, "fighters guild", 5 },
            { mwmp::FactionMutationKind::ExpelFromFaction, "fighters guild", 0 },
            { mwmp::FactionMutationKind::JoinFaction, "mages guild", 0 },
            { mwmp::FactionMutationKind::SetFactionRank, "mages guild", 3 },
            { mwmp::FactionMutationKind::ClearFactionExpulsion, "mages guild", 0 },
        }));
}

TEST(PlayerFactionState, RequestAndResultCanonicalCodecsRoundTrip)
{
    const mwmp::FactionMutationRequest outgoing = request({
        { mwmp::FactionMutationKind::JoinFaction, "mages guild", 0 },
        { mwmp::FactionMutationKind::SetFactionRank, "mages guild", 2 },
        { mwmp::FactionMutationKind::ModifyFactionReputation, "mages guild", 5 },
    });
    const std::string encodedRequest = mwmp::encodeFactionMutationRequest(outgoing);
    EXPECT_EQ(mwmp::decodeFactionMutationRequest(encodedRequest), outgoing);
    EXPECT_THROW(
        mwmp::decodeFactionMutationRequest(encodedRequest.substr(0, encodedRequest.size() - 1)), std::runtime_error);
    EXPECT_THROW(mwmp::decodeFactionMutationRequest(encodedRequest + "x"), std::runtime_error);

    mwmp::FactionMutationResult result;
    result.requestId = outgoing.requestId;
    result.accepted = true;
    result.state = state(8, { entry("mages guild", 2, 5) });
    const std::string encodedResult = mwmp::encodeFactionMutationResult(result);
    EXPECT_EQ(mwmp::decodeFactionMutationResult(encodedResult), result);
    EXPECT_THROW(
        mwmp::decodeFactionMutationResult(encodedResult.substr(0, encodedResult.size() - 1)), std::runtime_error);
    EXPECT_THROW(mwmp::decodeFactionMutationResult(encodedResult + "x"), std::runtime_error);
}

TEST(PlayerFactionState, MutationValidationRejectsArbitrarySnapshotsAndInvalidValues)
{
    auto invalid = request({});
    EXPECT_EQ(mwmp::validateFactionMutationRequest(invalid), mwmp::FactionError::InvalidRequest);

    invalid = request({ { mwmp::FactionMutationKind::SetFactionRank, "guild", -1 } });
    EXPECT_EQ(mwmp::validateFactionMutationRequest(invalid), mwmp::FactionError::InvalidRank);

    invalid = request({ { mwmp::FactionMutationKind::JoinFaction, "guild", 1 } });
    EXPECT_EQ(mwmp::validateFactionMutationRequest(invalid), mwmp::FactionError::InvalidRequest);

    invalid = request({ { mwmp::FactionMutationKind::JoinFaction, "Guild", 0 } });
    EXPECT_EQ(mwmp::validateFactionMutationRequest(invalid), mwmp::FactionError::InvalidRequest);
}

TEST(PlayerFactionPacket, RoundTripsTypedProposalAndAuthoritativeResult)
{
    mwmp::BasePlayer outgoing;
    outgoing.guid = 77;
    outgoing.factionState = state(8, { entry("mages guild", 2, 5, true) });

    mwmp::PacketPlayerFaction proposalEncoder;
    proposalEncoder.mode = mwmp::PacketPlayerFaction::Mode::Proposal;
    proposalEncoder.request = request({
        { mwmp::FactionMutationKind::JoinFaction, "mages guild", 0 },
        { mwmp::FactionMutationKind::SetFactionRank, "mages guild", 2 },
    });
    proposalEncoder.setPlayer(&outgoing);

    mwmp::BasePlayer proposalPlayer;
    mwmp::PacketPlayerFaction proposalDecoder;
    proposalDecoder.setPlayer(&proposalPlayer);
    ASSERT_TRUE(proposalDecoder.decode(proposalEncoder.encode()));
    EXPECT_EQ(proposalDecoder.mode, mwmp::PacketPlayerFaction::Mode::Proposal);
    EXPECT_EQ(proposalDecoder.request, proposalEncoder.request);
    EXPECT_EQ(proposalPlayer.guid, outgoing.guid);

    mwmp::PacketPlayerFaction resultEncoder;
    resultEncoder.mode = mwmp::PacketPlayerFaction::Mode::Result;
    resultEncoder.resultRequestId = proposalEncoder.request.requestId;
    resultEncoder.accepted = true;
    resultEncoder.setPlayer(&outgoing);

    mwmp::BasePlayer resultPlayer;
    mwmp::PacketPlayerFaction resultDecoder;
    resultDecoder.setPlayer(&resultPlayer);
    ASSERT_TRUE(resultDecoder.decode(resultEncoder.encode()));
    EXPECT_EQ(resultDecoder.mode, mwmp::PacketPlayerFaction::Mode::Result);
    EXPECT_TRUE(resultDecoder.accepted);
    EXPECT_EQ(resultDecoder.resultRequestId, proposalEncoder.request.requestId);
    EXPECT_EQ(resultPlayer.factionState, outgoing.factionState);
}

TEST(PlayerFactionPacket, RejectsTruncationAndTrailingBytes)
{
    mwmp::BasePlayer outgoing;
    outgoing.guid = 77;
    outgoing.factionState = state(8, { entry("mages guild", 2) });
    mwmp::PacketPlayerFaction encoder;
    encoder.setPlayer(&outgoing);
    const auto bytes = encoder.encode();

    mwmp::BasePlayer incoming;
    mwmp::PacketPlayerFaction decoder;
    decoder.setPlayer(&incoming);
    auto truncated = bytes;
    truncated.pop_back();
    EXPECT_FALSE(decoder.decode(truncated));
    auto trailing = bytes;
    trailing.push_back(0);
    EXPECT_FALSE(decoder.decode(trailing));
}

TEST(PlayerFactionRevisionGate, RestagesAnIdenticalResultToCorrectOptimisticState)
{
    mwmp::RevisionedStateGate<mwmp::PlayerFactionState> gate;
    const auto authoritative = state(8, { entry("mages guild", 2) });
    ASSERT_EQ(gate.receive(authoritative), mwmp::RevisionDecision::AcceptedNewer);
    ASSERT_TRUE(gate.takePending());
    EXPECT_EQ(gate.receive(authoritative), mwmp::RevisionDecision::IdenticalReplay);
    EXPECT_FALSE(gate.hasPending());
    EXPECT_TRUE(gate.restageLatest());
    EXPECT_EQ(gate.takePending(), authoritative);
}
