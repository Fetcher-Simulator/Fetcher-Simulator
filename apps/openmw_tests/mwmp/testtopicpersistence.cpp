#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>

#include <sqlite3.h>

#include <apps/openmw-server/PlayerDatabase.hpp>

namespace
{
    struct TemporaryTopicDatabase
    {
        std::filesystem::path path = std::filesystem::temp_directory_path()
            / ("openmw-topic-state-test-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".db");

        ~TemporaryTopicDatabase()
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
        Identity identity;
        identity.name = "Topic Tester" + suffix;
        identity.account = database.createAccount("topic-account" + suffix);
        identity.character = database.createCharacter(identity.account, identity.name).characterId;
        return identity;
    }
}

TEST(PlayerTopicPersistence, NewCharacterStartsWithCanonicalEmptyState)
{
    TemporaryTopicDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const Identity identity = createIdentity(database);

    const mwmp::PlayerTopicState state = database.loadPlayerTopicState(identity.character);
    EXPECT_EQ(state.revision, 0u);
    EXPECT_TRUE(state.knownTopicIds.empty());
    EXPECT_EQ(mwmp::validatePlayerTopicState(state), mwmp::TopicStateError::None);
}

TEST(PlayerTopicPersistence, AddCanonicalizesPersistsAndAdvancesOnce)
{
    TemporaryTopicDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const Identity identity = createIdentity(database);

    const mwmp::TopicMutationResult result
        = database.addKnownTopics(identity.character, 0, { "Z Topic", "a topic", "A TOPIC" });
    EXPECT_EQ(result.status, mwmp::TopicMutationStatus::Committed);
    EXPECT_EQ(result.state.revision, 1u);
    EXPECT_EQ(result.state.knownTopicIds, (std::vector<std::string>{ "a topic", "z topic" }));
    EXPECT_EQ(database.loadPlayerTopicState(identity.character), result.state);

    const mwmp::TopicMutationResult duplicate
        = database.addKnownTopics(identity.character, 1, { "A Topic", "z topic" });
    EXPECT_EQ(duplicate.status, mwmp::TopicMutationStatus::Idempotent);
    EXPECT_EQ(duplicate.state, result.state);
}

TEST(PlayerTopicPersistence, StaleRevisionReturnsAuthoritativeStateWithoutMutation)
{
    TemporaryTopicDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const Identity identity = createIdentity(database);
    const mwmp::PlayerTopicState committed
        = database.addKnownTopics(identity.character, 0, { "one" }).state;

    const mwmp::TopicMutationResult stale
        = database.addKnownTopics(identity.character, 0, { "two" });
    EXPECT_EQ(stale.status, mwmp::TopicMutationStatus::StaleRevision);
    EXPECT_EQ(stale.state, committed);
    EXPECT_EQ(database.loadPlayerTopicState(identity.character), committed);
}

TEST(PlayerTopicPersistence, RestartRestoresTopicsAndRevision)
{
    TemporaryTopicDatabase temporary;
    Identity identity;
    {
        mwmp::PlayerDatabase database(temporary.path.string());
        identity = createIdentity(database);
        ASSERT_EQ(database.addKnownTopics(identity.character, 0, { "one", "two" }).status,
            mwmp::TopicMutationStatus::Committed);
    }

    mwmp::PlayerDatabase reopened(temporary.path.string());
    const mwmp::PlayerTopicState restored = reopened.loadPlayerTopicState(identity.character);
    EXPECT_EQ(restored.revision, 1u);
    EXPECT_EQ(restored.knownTopicIds, (std::vector<std::string>{ "one", "two" }));
}

TEST(PlayerTopicPersistence, CharactersAreIsolated)
{
    TemporaryTopicDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const Identity first = createIdentity(database, "-one");
    const Identity second = createIdentity(database, "-two");

    ASSERT_EQ(database.addKnownTopics(first.character, 0, { "first topic" }).status,
        mwmp::TopicMutationStatus::Committed);
    ASSERT_EQ(database.addKnownTopics(second.character, 0, { "second topic" }).status,
        mwmp::TopicMutationStatus::Committed);
    EXPECT_EQ(database.loadPlayerTopicState(first.character).knownTopicIds,
        (std::vector<std::string>{ "first topic" }));
    EXPECT_EQ(database.loadPlayerTopicState(second.character).knownTopicIds,
        (std::vector<std::string>{ "second topic" }));
}

TEST(PlayerTopicPersistence, CharacterDeletionCascadesTopicRows)
{
    TemporaryTopicDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const Identity identity = createIdentity(database);
    ASSERT_EQ(database.addKnownTopics(identity.character, 0, { "delete me" }).status,
        mwmp::TopicMutationStatus::Committed);
    ASSERT_TRUE(database.deleteCharacter(identity.account, identity.name));

    sqlite3* raw = nullptr;
    ASSERT_EQ(sqlite3_open(temporary.path.string().c_str(), &raw), SQLITE_OK);
    for (const char* table : { "character_topic_state", "character_known_topics" })
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
