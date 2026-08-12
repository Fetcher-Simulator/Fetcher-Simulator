#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

#include <sqlite3.h>

#include <apps/openmw-server/FactionService.hpp>
#include <apps/openmw-server/PlayerDatabase.hpp>

namespace
{
    struct TemporaryFactionDatabase
    {
        std::filesystem::path path = std::filesystem::temp_directory_path()
            / ("openmw-faction-service-test-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".db");

        ~TemporaryFactionDatabase()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
            std::filesystem::remove(path.string() + "-wal", error);
            std::filesystem::remove(path.string() + "-shm", error);
        }
    };

    struct Identity
    {
        std::int64_t account = 0;
        std::int64_t character = 0;
        std::string name;
    };

    Identity createIdentity(mwmp::PlayerDatabase& database, std::string suffix = {})
    {
        Identity result;
        result.name = "Faction Tester" + suffix;
        result.account = database.createAccount("faction-account" + suffix);
        result.character = database.createCharacter(result.account, result.name).characterId;
        return result;
    }

    struct Rules
    {
        std::map<std::string, mwmp::FactionService::FactionDefinition> definitions{
            { "fighters guild", { { true, true, true, true } } },
            { "mages guild", { { true, true, true, true, true } } },
        };

        mwmp::FactionService::Context context(
            const Identity& identity, mwmp::FactionCommitFailurePoint failure = mwmp::FactionCommitFailurePoint::None)
        {
            mwmp::FactionService::Context result;
            result.accountId = identity.account;
            result.characterId = identity.character;
            result.failurePoint = failure;
            result.findFaction = [this](std::string_view id) -> std::optional<mwmp::FactionService::FactionDefinition> {
                const auto found = definitions.find(std::string(id));
                if (found == definitions.end())
                    return std::nullopt;
                return found->second;
            };
            return result;
        }
    };

    mwmp::FactionMutation mutation(mwmp::FactionMutationKind kind, std::string faction, std::int64_t value = 0)
    {
        return { kind, std::move(faction), value };
    }

    mwmp::FactionMutationRequest request(
        std::string id, std::uint64_t revision, std::vector<mwmp::FactionMutation> mutations)
    {
        mwmp::FactionMutationRequest result;
        result.requestId = std::move(id);
        result.expectedRevision = revision;
        result.mutations = std::move(mutations);
        result.source = "test:faction-service";
        return result;
    }

    const mwmp::PlayerFactionEntry& onlyFaction(const mwmp::FactionMutationResult& result)
    {
        EXPECT_EQ(result.state.factions.size(), 1u);
        return result.state.factions.front();
    }
}

TEST(FactionService, NewCharacterHasEmptyRevisionZeroState)
{
    TemporaryFactionDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const Identity identity = createIdentity(database);
    const mwmp::PlayerFactionState state = database.loadPlayerFactionState(identity.character);
    EXPECT_EQ(state.revision, 0u);
    EXPECT_TRUE(state.factions.empty());
}

TEST(FactionService, JoinPreservesRankZeroMembership)
{
    TemporaryFactionDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const Identity identity = createIdentity(database);
    Rules rules;
    mwmp::FactionService service(database);

    const auto outcome
        = service.execute(request("join", 0, { mutation(mwmp::FactionMutationKind::JoinFaction, "fighters guild") }),
            rules.context(identity));
    ASSERT_TRUE(outcome.result.accepted);
    EXPECT_TRUE(outcome.committed);
    EXPECT_EQ(outcome.result.state.revision, 1u);
    EXPECT_EQ(onlyFaction(outcome.result).rank, 0);
}

TEST(FactionService, BatchPromotesReputationAndExpulsionAtomically)
{
    TemporaryFactionDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const Identity identity = createIdentity(database);
    Rules rules;
    mwmp::FactionService service(database);

    const auto outcome
        = service.execute(request("batch", 0,
                              { mutation(mwmp::FactionMutationKind::JoinFaction, "fighters guild"),
                                  mutation(mwmp::FactionMutationKind::SetFactionRank, "fighters guild", 2),
                                  mutation(mwmp::FactionMutationKind::SetFactionReputation, "fighters guild", 17),
                                  mutation(mwmp::FactionMutationKind::ExpelFromFaction, "fighters guild") }),
            rules.context(identity));
    ASSERT_TRUE(outcome.result.accepted);
    const auto& faction = onlyFaction(outcome.result);
    EXPECT_EQ(faction.rank, 2);
    EXPECT_EQ(faction.reputation, 17);
    EXPECT_TRUE(faction.expelled);
    EXPECT_EQ(database.loadPlayerFactionState(identity.character), outcome.result.state);
}

TEST(FactionService, DemoteLeaveAndClearExpulsionMatchNpcStatsSemantics)
{
    TemporaryFactionDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const Identity identity = createIdentity(database);
    Rules rules;
    mwmp::FactionService service(database);
    ASSERT_TRUE(service
            .execute(request("seed", 0,
                         { mutation(mwmp::FactionMutationKind::JoinFaction, "fighters guild"),
                             mutation(mwmp::FactionMutationKind::SetFactionRank, "fighters guild", 1),
                             mutation(mwmp::FactionMutationKind::ExpelFromFaction, "fighters guild") }),
                rules.context(identity))
            .result.accepted);

    const auto demoted
        = service.execute(request("demote", 1,
                              { mutation(mwmp::FactionMutationKind::ModifyFactionRank, "fighters guild", -1),
                                  mutation(mwmp::FactionMutationKind::ClearFactionExpulsion, "fighters guild") }),
            rules.context(identity));
    ASSERT_TRUE(demoted.result.accepted);
    EXPECT_EQ(onlyFaction(demoted.result).rank, 0);
    EXPECT_FALSE(onlyFaction(demoted.result).expelled);

    const auto left
        = service.execute(request("leave", 2, { mutation(mwmp::FactionMutationKind::LeaveFaction, "fighters guild") }),
            rules.context(identity));
    ASSERT_TRUE(left.result.accepted);
    EXPECT_TRUE(left.result.state.factions.empty());
}

TEST(FactionService, SupportsMultipleIndependentFactions)
{
    TemporaryFactionDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const Identity identity = createIdentity(database);
    Rules rules;
    mwmp::FactionService service(database);
    const auto outcome
        = service.execute(request("multi", 0,
                              { mutation(mwmp::FactionMutationKind::JoinFaction, "fighters guild"),
                                  mutation(mwmp::FactionMutationKind::JoinFaction, "mages guild"),
                                  mutation(mwmp::FactionMutationKind::SetFactionRank, "mages guild", 3) }),
            rules.context(identity));
    ASSERT_TRUE(outcome.result.accepted);
    ASSERT_EQ(outcome.result.state.factions.size(), 2u);
    EXPECT_EQ(outcome.result.state.factions[0].factionId, "fighters guild");
    EXPECT_EQ(outcome.result.state.factions[1].rank, 3);
}

TEST(FactionService, RestartRestoresExactTupleAndRevision)
{
    TemporaryFactionDatabase temporary;
    Identity identity;
    Rules rules;
    {
        mwmp::PlayerDatabase database(temporary.path.string());
        identity = createIdentity(database);
        mwmp::FactionService service(database);
        ASSERT_TRUE(service
                .execute(request("persist", 0,
                             { mutation(mwmp::FactionMutationKind::JoinFaction, "mages guild"),
                                 mutation(mwmp::FactionMutationKind::SetFactionRank, "mages guild", 2),
                                 mutation(mwmp::FactionMutationKind::SetFactionReputation, "mages guild", -9),
                                 mutation(mwmp::FactionMutationKind::ExpelFromFaction, "mages guild") }),
                    rules.context(identity))
                .result.accepted);
    }

    mwmp::PlayerDatabase reopened(temporary.path.string());
    const mwmp::PlayerFactionState restored = reopened.loadPlayerFactionState(identity.character);
    EXPECT_EQ(restored.revision, 1u);
    ASSERT_EQ(restored.factions.size(), 1u);
    EXPECT_EQ(restored.factions[0], (mwmp::PlayerFactionEntry{ "mages guild", 2, -9, true }));
}

TEST(FactionService, StaleRevisionRejectsWithoutMutationAndReplays)
{
    TemporaryFactionDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const Identity identity = createIdentity(database);
    Rules rules;
    mwmp::FactionService service(database);
    const auto committed
        = service.execute(request("join", 0, { mutation(mwmp::FactionMutationKind::JoinFaction, "fighters guild") }),
            rules.context(identity));
    ASSERT_TRUE(committed.result.accepted);

    const auto staleRequest
        = request("stale", 0, { mutation(mwmp::FactionMutationKind::SetFactionRank, "fighters guild", 1) });
    const auto stale = service.execute(staleRequest, rules.context(identity));
    EXPECT_FALSE(stale.result.accepted);
    EXPECT_EQ(stale.result.error, mwmp::FactionError::StaleRevision);
    EXPECT_EQ(stale.result.state, committed.result.state);
    const auto replay = service.execute(staleRequest, rules.context(identity));
    EXPECT_TRUE(replay.replayed);
    EXPECT_EQ(replay.result, stale.result);
}

TEST(FactionService, DuplicateRequestReplaysAndConflictingHashRejects)
{
    TemporaryFactionDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const Identity identity = createIdentity(database);
    Rules rules;
    mwmp::FactionService service(database);
    const auto originalRequest
        = request("duplicate", 0, { mutation(mwmp::FactionMutationKind::JoinFaction, "fighters guild") });
    const auto original = service.execute(originalRequest, rules.context(identity));
    ASSERT_TRUE(original.result.accepted);

    const auto replay = service.execute(originalRequest, rules.context(identity));
    EXPECT_TRUE(replay.replayed);
    EXPECT_FALSE(replay.committed);
    EXPECT_EQ(replay.result, original.result);

    const auto conflict
        = service.execute(request("duplicate", 1, { mutation(mwmp::FactionMutationKind::JoinFaction, "mages guild") }),
            rules.context(identity));
    EXPECT_FALSE(conflict.result.accepted);
    EXPECT_EQ(conflict.result.error, mwmp::FactionError::DuplicateConflict);
    EXPECT_EQ(database.loadPlayerFactionState(identity.character), original.result.state);
}

TEST(FactionService, InvalidFactionRankAndTransitionRejectWithoutStateChange)
{
    TemporaryFactionDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const Identity identity = createIdentity(database);
    Rules rules;
    mwmp::FactionService service(database);

    const auto missing
        = service.execute(request("missing", 0, { mutation(mwmp::FactionMutationKind::JoinFaction, "missing guild") }),
            rules.context(identity));
    EXPECT_EQ(missing.result.error, mwmp::FactionError::InvalidFaction);

    const auto rank
        = service.execute(request("rank", 0,
                              { mutation(mwmp::FactionMutationKind::JoinFaction, "fighters guild"),
                                  mutation(mwmp::FactionMutationKind::SetFactionRank, "fighters guild", 9) }),
            rules.context(identity));
    EXPECT_EQ(rank.result.error, mwmp::FactionError::InvalidRank);

    const auto transition = service.execute(
        request("transition", 0, { mutation(mwmp::FactionMutationKind::SetFactionRank, "fighters guild", 1) }),
        rules.context(identity));
    EXPECT_EQ(transition.result.error, mwmp::FactionError::InvalidTransition);
    EXPECT_EQ(database.loadPlayerFactionState(identity.character).revision, 0u);
}

TEST(FactionService, CharactersRemainIsolatedAndDeletionCascades)
{
    TemporaryFactionDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const Identity first = createIdentity(database, "-one");
    const Identity second = createIdentity(database, "-two");
    Rules rules;
    mwmp::FactionService service(database);
    ASSERT_TRUE(service
            .execute(request("first", 0, { mutation(mwmp::FactionMutationKind::JoinFaction, "fighters guild") }),
                rules.context(first))
            .result.accepted);
    ASSERT_TRUE(service
            .execute(request("second", 0, { mutation(mwmp::FactionMutationKind::JoinFaction, "mages guild") }),
                rules.context(second))
            .result.accepted);
    EXPECT_EQ(database.loadPlayerFactionState(first.character).factions[0].factionId, "fighters guild");
    EXPECT_EQ(database.loadPlayerFactionState(second.character).factions[0].factionId, "mages guild");

    ASSERT_TRUE(database.deleteCharacter(first.account, first.name));
    sqlite3* raw = nullptr;
    ASSERT_EQ(sqlite3_open(temporary.path.string().c_str(), &raw), SQLITE_OK);
    for (const char* table : { "character_faction_state", "character_factions" })
    {
        const std::string sql
            = std::string("SELECT COUNT(*) FROM ") + table + " WHERE character_id=" + std::to_string(first.character);
        sqlite3_stmt* statement = nullptr;
        ASSERT_EQ(sqlite3_prepare_v2(raw, sql.c_str(), -1, &statement, nullptr), SQLITE_OK);
        ASSERT_EQ(sqlite3_step(statement), SQLITE_ROW);
        EXPECT_EQ(sqlite3_column_int64(statement, 0), 0);
        sqlite3_finalize(statement);
    }
    sqlite3_close(raw);
}

TEST(FactionService, InjectedFailureRollsBackTupleRevisionAndRequestJournal)
{
    TemporaryFactionDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const Identity identity = createIdentity(database);
    Rules rules;
    mwmp::FactionService service(database);
    const auto proposal
        = request("rollback", 0, { mutation(mwmp::FactionMutationKind::JoinFaction, "fighters guild") });

    EXPECT_THROW(service.execute(proposal, rules.context(identity, mwmp::FactionCommitFailurePoint::AfterStateWrite)),
        std::runtime_error);
    EXPECT_EQ(database.loadPlayerFactionState(identity.character), mwmp::PlayerFactionState{});
    EXPECT_FALSE(database.loadSemanticRequest("faction", identity.account, identity.character, "rollback"));
}
