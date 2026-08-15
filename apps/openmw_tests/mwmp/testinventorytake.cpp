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
            / ("openmw-inventory-take-test-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".db");
        ~TemporaryDatabase()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
            std::filesystem::remove(path.string() + "-wal", error);
            std::filesystem::remove(path.string() + "-shm", error);
        }
    };

    mwmp::InventoryTakeCommit makeCommit(std::int64_t account, std::int64_t character)
    {
        mwmp::ContainerRecord source;
        source.cellId = "Balmora";
        source.refId = "crate_01";
        source.refNum = 42;
        source.hasAuthority = true;
        source.items = { { "gold_001", 10, -1 }, { "iron dagger", 1, 100 } };

        mwmp::InventoryTakeCommit commit;
        commit.accountId = account;
        commit.characterId = character;
        commit.requestId = "inventory-take-1";
        commit.requestHash = mwmp::crypto::sha256hex(commit.requestId);
        commit.expectedInventoryRevision = 0;
        commit.resultingInventoryRevision = 1;
        commit.expectedSource = source;
        commit.resultingSource = source;
        commit.resultingSource->items.front().count = 5;
        commit.result.requestId = commit.requestId;
        commit.result.accepted = true;
        commit.result.kind = mwmp::InventoryTakeKind::Container;
        commit.result.source.cellId = source.cellId;
        commit.result.source.refId = source.refId;
        commit.result.source.refNum = source.refNum;
        commit.result.itemRefId = "gold_001";
        commit.result.itemCharge = -1;
        commit.result.itemCount = 5;
        commit.result.inventoryRevision = 1;
        commit.result.theft = true;
        commit.result.crimeValue = 5;
        mwmp::Item item;
        item.instanceId = 901;
        item.refId = "gold_001";
        item.count = 5;
        commit.inventory.push_back(item);
        return commit;
    }
}

TEST(InventoryTakePersistence, SourceDestinationAndReplayAreAtomicAcrossRestart)
{
    TemporaryDatabase temporary;
    std::int64_t account = 0;
    std::int64_t character = 0;
    mwmp::InventoryTakeCommit original;
    {
        mwmp::PlayerDatabase database(temporary.path.string());
        account = database.createAccount("inventory-take-account");
        character = database.createCharacter(account, "Inventory Take Tester").characterId;
        original = makeCommit(account, character);
        database.upsertContainerRecord(*original.expectedSource);

        const auto committed = database.commitInventoryTake(original);
        EXPECT_EQ(committed.status, mwmp::InventoryTakeCommitStatus::Committed);
        EXPECT_EQ(database.loadInventoryRevision(character), 1u);
        ASSERT_EQ(database.loadCharacterInventory(character).size(), 1u);
        const auto containers = database.loadContainerRecords();
        ASSERT_EQ(containers.size(), 1u);
        EXPECT_EQ(containers.front().items, original.resultingSource->items);

        const auto replay = database.commitInventoryTake(original);
        EXPECT_EQ(replay.status, mwmp::InventoryTakeCommitStatus::DuplicateRequest);
        EXPECT_TRUE(replay.result.replayed);
        const auto stored = database.loadInventoryTake(account, character, original.requestId);
        ASSERT_TRUE(stored);
        EXPECT_EQ(stored->requestHash, original.requestHash);
        EXPECT_TRUE(stored->result.replayed);

        auto conflict = original;
        conflict.requestHash = mwmp::crypto::sha256hex("conflict");
        EXPECT_EQ(database.commitInventoryTake(conflict).status,
            mwmp::InventoryTakeCommitStatus::DuplicateRequestConflict);
    }

    mwmp::PlayerDatabase reopened(temporary.path.string());
    EXPECT_EQ(reopened.loadInventoryRevision(character), 1u);
    ASSERT_EQ(reopened.loadCharacterInventory(character).size(), 1u);
    const auto stored = reopened.loadInventoryTake(account, character, original.requestId);
    ASSERT_TRUE(stored);
    EXPECT_EQ(stored->result.detectionRoll, -1);
    EXPECT_EQ(stored->result.inventoryRevision, 1u);
}

TEST(InventoryTakePersistence, DetectedPickpocketPersistsRollWithoutMovingItems)
{
    TemporaryDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const std::int64_t account = database.createAccount("pickpocket-account");
    const std::int64_t character = database.createCharacter(account, "Pickpocket Tester").characterId;
    auto commit = makeCommit(account, character);
    database.upsertContainerRecord(*commit.expectedSource);
    commit.requestId = "pickpocket-detected";
    commit.requestHash = mwmp::crypto::sha256hex(commit.requestId);
    commit.result.requestId = commit.requestId;
    commit.result.kind = mwmp::InventoryTakeKind::Pickpocket;
    commit.result.detected = true;
    commit.result.detectionRoll = 97;
    commit.resultingInventoryRevision = 0;
    commit.result.inventoryRevision = 0;
    commit.resultingSource.reset();
    commit.inventory.clear();

    EXPECT_EQ(database.commitInventoryTake(commit).status, mwmp::InventoryTakeCommitStatus::Committed);
    EXPECT_EQ(database.loadInventoryRevision(character), 0u);
    EXPECT_TRUE(database.loadCharacterInventory(character).empty());
    ASSERT_EQ(database.loadContainerRecords().front().items, commit.expectedSource->items);
    const auto replay = database.commitInventoryTake(commit);
    EXPECT_EQ(replay.result.detectionRoll, 97);
    EXPECT_TRUE(replay.result.detected);
}
