#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>

#include <apps/openmw-server/PlayerDatabase.hpp>
#include <components/openmw-mp/Sha256.hpp>

namespace
{
    struct TemporaryDatabase
    {
        std::filesystem::path path = std::filesystem::temp_directory_path()
            / ("openmw-world-item-take-test-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".db");
        ~TemporaryDatabase()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
            std::filesystem::remove(path.string() + "-wal", error);
            std::filesystem::remove(path.string() + "-shm", error);
        }
    };

    mwmp::WorldItemTakeCommit makeCommit(mwmp::PlayerDatabase& database,
        std::int64_t account, std::int64_t character, std::string requestId = "take-1")
    {
        mwmp::WorldItemTakeCommit commit;
        commit.accountId = account;
        commit.characterId = character;
        commit.requestId = std::move(requestId);
        commit.requestHash = mwmp::crypto::sha256hex(commit.requestId);
        commit.object.kind = mwmp::PlacedObjectKind::ContentReference;
        commit.object.cellId = "Balmora";
        commit.object.refId = "iron dagger";
        commit.object.refIndex = 77;
        commit.object.refContentFile = 0;
        commit.result.requestId = commit.requestId;
        commit.result.accepted = true;
        commit.result.object = commit.object;
        commit.result.itemRefId = commit.object.refId;
        commit.result.itemCount = 1;
        commit.result.crimeValue = 10;
        commit.result.theft = true;
        commit.expectedInventoryRevision = database.loadInventoryRevision(character);
        commit.resultingInventoryRevision = commit.expectedInventoryRevision + 1;
        commit.result.inventoryRevision = commit.resultingInventoryRevision;
        mwmp::Item item;
        item.instanceId = 900;
        item.refId = commit.object.refId;
        item.count = 1;
        commit.inventory.push_back(item);
        return commit;
    }
}

TEST(WorldItemTakePersistence, CommitsTombstoneInventoryAndReplayAtomically)
{
    TemporaryDatabase temporary;
    std::int64_t account = 0;
    std::int64_t character = 0;
    mwmp::WorldItemTakeCommit original;
    {
        mwmp::PlayerDatabase database(temporary.path.string());
        account = database.createAccount("take-account");
        character = database.createCharacter(account, "Take Tester").characterId;
        original = makeCommit(database, account, character);
        const auto committed = database.commitWorldItemTake(original);
        EXPECT_EQ(committed.status, mwmp::WorldItemTakeCommitStatus::Committed);
        EXPECT_EQ(database.loadInventoryRevision(character), 1u);
        ASSERT_EQ(database.loadCharacterInventory(character).size(), 1u);
        ASSERT_EQ(database.loadTakenWorldItemReferences().size(), 1u);

        const auto replay = database.commitWorldItemTake(original);
        EXPECT_EQ(replay.status, mwmp::WorldItemTakeCommitStatus::DuplicateRequest);
        EXPECT_TRUE(replay.result.replayed);

        auto conflict = original;
        conflict.requestHash = mwmp::crypto::sha256hex("different");
        EXPECT_EQ(database.commitWorldItemTake(conflict).status,
            mwmp::WorldItemTakeCommitStatus::DuplicateRequestConflict);

        auto second = makeCommit(database, account, character, "take-2");
        second.object = original.object;
        second.result.object = second.object;
        EXPECT_EQ(database.commitWorldItemTake(second).status,
            mwmp::WorldItemTakeCommitStatus::ObjectAlreadyTaken);
    }

    mwmp::PlayerDatabase reopened(temporary.path.string());
    EXPECT_EQ(reopened.loadInventoryRevision(character), 1u);
    ASSERT_EQ(reopened.loadCharacterInventory(character).size(), 1u);
    ASSERT_EQ(reopened.loadTakenWorldItemReferences().size(), 1u);
    EXPECT_EQ(reopened.loadTakenWorldItemReferences().front(), original.object);
}
