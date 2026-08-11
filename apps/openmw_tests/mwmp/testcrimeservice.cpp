#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>

#include <sqlite3.h>

#include <apps/openmw-server/CrimeService.hpp>
#include <apps/openmw-server/PlayerDatabase.hpp>
#include <components/openmw-mp/PlayerCrimeState.hpp>
#include <components/openmw-mp/Sha256.hpp>

namespace
{
    struct TemporaryCrimeDatabase
    {
        std::filesystem::path path = std::filesystem::temp_directory_path()
            / ("openmw-crime-service-test-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".db");

        ~TemporaryCrimeDatabase()
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
    };

    Identity createIdentity(mwmp::PlayerDatabase& database, std::string suffix = {})
    {
        const std::int64_t account = database.createAccount("crime-account" + suffix);
        const std::int64_t character
            = database.createCharacter(account, "Crime Tester" + suffix).characterId;
        return { account, character };
    }

    mwmp::CrimeMutationRequest makeRequest(std::string requestId, mwmp::CrimeMutationKind kind,
        std::int64_t value, std::optional<std::uint64_t> expectedRevision = std::nullopt)
    {
        mwmp::CrimeMutationRequest request;
        request.requestId = std::move(requestId);
        request.kind = kind;
        request.value = value;
        request.expectedRevision = expectedRevision;
        request.source = "test:crime-service";
        return request;
    }

    mwmp::CrimeService::Context makeContext(const Identity& identity)
    {
        mwmp::CrimeService::Context context;
        context.accountId = identity.account;
        context.characterId = identity.character;
        return context;
    }

    void seedCrimeState(mwmp::PlayerDatabase& database, const Identity& identity,
        const mwmp::PlayerCrimeState& state, std::string requestId = "seed-crime-state")
    {
        mwmp::CrimeMutationResult result;
        result.requestId = requestId;
        result.accepted = true;
        result.state = state;

        mwmp::CrimeMutationCommit commit;
        commit.accountId = identity.account;
        commit.characterId = identity.character;
        commit.requestId = std::move(requestId);
        commit.requestHash = mwmp::crypto::sha256hex("seed:" + std::to_string(state.revision));
        commit.resultPayload = mwmp::encodeCrimeMutationResult(result);
        commit.source = "test:seed";
        commit.expectedRevision = state.revision - 1;
        commit.resultingState = state;
        ASSERT_EQ(database.commitPlayerCrimeMutation(commit).status, mwmp::CrimeCommitStatus::Committed);
    }
}

TEST(CrimeService, NewCharacterHasVanillaCompatibleDefaults)
{
    TemporaryCrimeDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const Identity identity = createIdentity(database);

    const mwmp::PlayerCrimeState state = database.loadPlayerCrimeState(identity.character);
    EXPECT_EQ(state.bounty, 0);
    EXPECT_EQ(state.currentCrimeId, -1);
    EXPECT_EQ(state.paidCrimeId, -1);
    EXPECT_EQ(state.revision, 0u);
}

TEST(CrimeService, SetAndModifyCommitAndAdvanceExactlyOnce)
{
    TemporaryCrimeDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const Identity identity = createIdentity(database);
    mwmp::CrimeService service(database);

    const auto set = service.execute(
        makeRequest("set-750", mwmp::CrimeMutationKind::SetBounty, 750), makeContext(identity));
    ASSERT_TRUE(set.result.accepted);
    EXPECT_TRUE(set.committed);
    EXPECT_EQ(set.result.state.bounty, 750);
    EXPECT_EQ(set.result.state.revision, 1u);

    const auto modify = service.execute(
        makeRequest("mod-250", mwmp::CrimeMutationKind::ModifyBounty, 250, 1), makeContext(identity));
    ASSERT_TRUE(modify.result.accepted);
    EXPECT_EQ(modify.result.state.bounty, 1000);
    EXPECT_EQ(modify.result.state.revision, 2u);
    EXPECT_EQ(database.loadPlayerCrimeState(identity.character), modify.result.state);
}

TEST(CrimeService, ZeroBountyRecordsCurrentCrimeGeneration)
{
    TemporaryCrimeDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const Identity identity = createIdentity(database);
    mwmp::PlayerCrimeState seeded;
    seeded.bounty = 750;
    seeded.currentCrimeId = 8;
    seeded.paidCrimeId = 5;
    seeded.revision = 1;
    seedCrimeState(database, identity, seeded);

    mwmp::CrimeService service(database);
    const auto cleared = service.execute(
        makeRequest("clear", mwmp::CrimeMutationKind::SetBounty, 0), makeContext(identity));
    ASSERT_TRUE(cleared.result.accepted);
    EXPECT_EQ(cleared.result.state.bounty, 0);
    EXPECT_EQ(cleared.result.state.currentCrimeId, 8);
    EXPECT_EQ(cleared.result.state.paidCrimeId, 8);
    EXPECT_EQ(cleared.result.state.revision, 2u);
}

TEST(CrimeService, InvalidNegativeAndOverflowResultsDoNotAdvanceRevision)
{
    TemporaryCrimeDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const Identity identity = createIdentity(database);
    mwmp::CrimeService service(database);

    auto negative = service.execute(
        makeRequest("negative", mwmp::CrimeMutationKind::ModifyBounty, -1), makeContext(identity));
    EXPECT_FALSE(negative.result.accepted);
    EXPECT_EQ(negative.result.error, mwmp::CrimeError::InvalidBounty);
    EXPECT_EQ(database.loadPlayerCrimeState(identity.character).revision, 0u);

    auto overflow = service.execute(makeRequest("overflow", mwmp::CrimeMutationKind::SetBounty,
        static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max()) + 1), makeContext(identity));
    EXPECT_FALSE(overflow.result.accepted);
    EXPECT_EQ(overflow.result.error, mwmp::CrimeError::InvalidBounty);
    EXPECT_EQ(database.loadPlayerCrimeState(identity.character).revision, 0u);
}

TEST(CrimeService, ReplaysSameRequestAndRejectsConflictingReuse)
{
    TemporaryCrimeDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const Identity identity = createIdentity(database);
    mwmp::CrimeService service(database);
    const auto request = makeRequest("idempotent", mwmp::CrimeMutationKind::ModifyBounty, 100);

    const auto first = service.execute(request, makeContext(identity));
    ASSERT_TRUE(first.result.accepted);
    EXPECT_TRUE(first.committed);

    const auto replay = service.execute(request, makeContext(identity));
    EXPECT_TRUE(replay.replayed);
    EXPECT_FALSE(replay.committed);
    EXPECT_EQ(replay.result, first.result);
    EXPECT_EQ(database.loadPlayerCrimeState(identity.character).revision, 1u);

    const auto conflict = service.execute(
        makeRequest("idempotent", mwmp::CrimeMutationKind::ModifyBounty, 200), makeContext(identity));
    EXPECT_FALSE(conflict.result.accepted);
    EXPECT_EQ(conflict.result.error, mwmp::CrimeError::DuplicateConflict);
    EXPECT_EQ(database.loadPlayerCrimeState(identity.character).bounty, 100);
    EXPECT_EQ(database.loadPlayerCrimeState(identity.character).revision, 1u);
}

TEST(CrimeService, RejectedRequestIsAlsoDurablyIdempotent)
{
    TemporaryCrimeDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const Identity identity = createIdentity(database);
    mwmp::CrimeService service(database);
    const auto request = makeRequest("bad-mod", mwmp::CrimeMutationKind::ModifyBounty, -5);

    const auto first = service.execute(request, makeContext(identity));
    ASSERT_EQ(first.result.error, mwmp::CrimeError::InvalidBounty);
    const auto replay = service.execute(request, makeContext(identity));
    EXPECT_TRUE(replay.replayed);
    EXPECT_EQ(replay.result, first.result);
    EXPECT_EQ(database.loadPlayerCrimeState(identity.character).revision, 0u);
}

TEST(CrimeService, ExpectedRevisionRejectsStaleMutation)
{
    TemporaryCrimeDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const Identity identity = createIdentity(database);
    mwmp::CrimeService service(database);
    ASSERT_TRUE(service.execute(
        makeRequest("first", mwmp::CrimeMutationKind::SetBounty, 50), makeContext(identity)).result.accepted);

    const auto stale = service.execute(
        makeRequest("stale", mwmp::CrimeMutationKind::ModifyBounty, 10, 0), makeContext(identity));
    EXPECT_FALSE(stale.result.accepted);
    EXPECT_EQ(stale.result.error, mwmp::CrimeError::StaleRevision);
    EXPECT_EQ(database.loadPlayerCrimeState(identity.character).bounty, 50);
    EXPECT_EQ(database.loadPlayerCrimeState(identity.character).revision, 1u);
}

TEST(CrimeService, AuthenticatedIdentityCannotSelectAnotherAccountCharacter)
{
    TemporaryCrimeDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const Identity first = createIdentity(database, "-a");
    const Identity second = createIdentity(database, "-b");
    mwmp::CrimeService service(database);

    auto context = makeContext(first);
    context.characterId = second.character;
    EXPECT_THROW(service.execute(
        makeRequest("wrong-owner", mwmp::CrimeMutationKind::SetBounty, 500), context), std::runtime_error);
    EXPECT_EQ(database.loadPlayerCrimeState(first.character).revision, 0u);
    EXPECT_EQ(database.loadPlayerCrimeState(second.character).revision, 0u);
}

TEST(CrimeService, InjectedFailureRollsBackStateAndTerminalRequest)
{
    TemporaryCrimeDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const Identity identity = createIdentity(database);
    mwmp::CrimeService service(database);
    auto context = makeContext(identity);
    context.failurePoint = mwmp::CrimeCommitFailurePoint::AfterStateWrite;
    const auto request = makeRequest("rollback", mwmp::CrimeMutationKind::SetBounty, 900);

    EXPECT_THROW(service.execute(request, context), std::runtime_error);
    const mwmp::PlayerCrimeState state = database.loadPlayerCrimeState(identity.character);
    EXPECT_EQ(state.bounty, 0);
    EXPECT_EQ(state.currentCrimeId, -1);
    EXPECT_EQ(state.paidCrimeId, -1);
    EXPECT_EQ(state.revision, 0u);
    EXPECT_FALSE(database.loadSemanticRequest(
        "crime", identity.account, identity.character, request.requestId).has_value());
}

TEST(CrimeService, RestartRestoresAllFieldsAndRevision)
{
    TemporaryCrimeDatabase temporary;
    Identity identity;
    {
        mwmp::PlayerDatabase database(temporary.path.string());
        identity = createIdentity(database);
        mwmp::PlayerCrimeState state;
        state.bounty = 750;
        state.currentCrimeId = 17;
        state.paidCrimeId = 11;
        state.revision = 1;
        seedCrimeState(database, identity, state);
    }

    mwmp::PlayerDatabase reopened(temporary.path.string());
    const mwmp::PlayerCrimeState restored = reopened.loadPlayerCrimeState(identity.character);
    EXPECT_EQ(restored.bounty, 750);
    EXPECT_EQ(restored.currentCrimeId, 17);
    EXPECT_EQ(restored.paidCrimeId, 11);
    EXPECT_EQ(restored.revision, 1u);
}

TEST(CrimeService, CharacterDeletionCascadesStateAndRequestJournal)
{
    TemporaryCrimeDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const Identity identity = createIdentity(database);
    mwmp::CrimeService service(database);
    ASSERT_TRUE(service.execute(
        makeRequest("delete-me", mwmp::CrimeMutationKind::SetBounty, 50), makeContext(identity)).result.accepted);
    ASSERT_TRUE(database.deleteCharacter(identity.account, "Crime Tester"));

    sqlite3* raw = nullptr;
    ASSERT_EQ(sqlite3_open(temporary.path.string().c_str(), &raw), SQLITE_OK);
    for (const char* table : { "character_crime_state", "semantic_requests" })
    {
        const std::string sql = std::string("SELECT COUNT(*) FROM ") + table;
        sqlite3_stmt* statement = nullptr;
        ASSERT_EQ(sqlite3_prepare_v2(raw, sql.c_str(), -1, &statement, nullptr), SQLITE_OK);
        ASSERT_EQ(sqlite3_step(statement), SQLITE_ROW);
        EXPECT_EQ(sqlite3_column_int64(statement, 0), 0);
        sqlite3_finalize(statement);
    }
    sqlite3_close(raw);
}

TEST(CrimeService, MigratesLegacyDatabaseWithoutCrimeTables)
{
    TemporaryCrimeDatabase temporary;
    sqlite3* raw = nullptr;
    ASSERT_EQ(sqlite3_open(temporary.path.string().c_str(), &raw), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(raw, R"SQL(
        CREATE TABLE accounts (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT UNIQUE NOT NULL,
            created_at INTEGER NOT NULL DEFAULT 0
        );
        CREATE TABLE characters (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
            name TEXT NOT NULL,
            cell TEXT NOT NULL DEFAULT '',
            pos_x REAL NOT NULL DEFAULT 0, pos_y REAL NOT NULL DEFAULT 0, pos_z REAL NOT NULL DEFAULT 0,
            rot_x REAL NOT NULL DEFAULT 0, rot_y REAL NOT NULL DEFAULT 0, rot_z REAL NOT NULL DEFAULT 0,
            is_new INTEGER NOT NULL DEFAULT 1, last_seen INTEGER NOT NULL DEFAULT 0
        );
    )SQL", nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3_close(raw);

    mwmp::PlayerDatabase database(temporary.path.string());
    const Identity identity = createIdentity(database);
    mwmp::CrimeService service(database);
    const auto outcome = service.execute(
        makeRequest("migrated", mwmp::CrimeMutationKind::SetBounty, 75), makeContext(identity));
    ASSERT_TRUE(outcome.result.accepted);
    EXPECT_EQ(database.loadPlayerCrimeState(identity.character).bounty, 75);
}
