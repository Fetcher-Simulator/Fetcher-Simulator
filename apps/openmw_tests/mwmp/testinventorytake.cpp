#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>

#include <apps/openmw-server/PlayerDatabase.hpp>
#include <components/openmw-mp/Sha256.hpp>
#include <components/openmw-mp/InventoryPut.hpp>

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

    mwmp::CrimeMutationCommit makeCrimeCommit(std::int64_t account, std::int64_t character,
        std::string requestId = "inventory-take-crime")
    {
        mwmp::CrimeMutationCommit crime;
        crime.service = "crime-event";
        crime.accountId = account;
        crime.characterId = character;
        crime.requestId = std::move(requestId);
        crime.requestHash = mwmp::crypto::sha256hex(crime.requestId);
        crime.resultPayload = "terminal-crime-result";
        crime.source = "inventory-take-test";
        crime.expectedRevision = 0;
        crime.resultingState.bounty = 5;
        crime.resultingState.currentCrimeId = 0;
        crime.resultingState.paidCrimeId = -1;
        crime.resultingState.revision = 1;
        return crime;
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

TEST(InventoryTakePersistence, CrimeResultCommitsInSameTransactionAsTransfer)
{
    TemporaryDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const std::int64_t account = database.createAccount("inventory-crime-account");
    const std::int64_t character = database.createCharacter(account, "Inventory Crime Tester").characterId;
    auto commit = makeCommit(account, character);
    database.upsertContainerRecord(*commit.expectedSource);
    commit.crimeMutation = makeCrimeCommit(account, character);

    EXPECT_EQ(database.commitInventoryTake(commit).status, mwmp::InventoryTakeCommitStatus::Committed);
    const mwmp::PlayerCrimeState state = database.loadPlayerCrimeState(character);
    EXPECT_EQ(state.bounty, 5);
    EXPECT_EQ(state.currentCrimeId, 0);
    EXPECT_EQ(state.revision, 1u);
    EXPECT_TRUE(database.loadSemanticRequest(
        "crime-event", account, character, commit.crimeMutation->requestId).has_value());
    EXPECT_TRUE(database.loadInventoryTake(account, character, commit.requestId).has_value());
}

TEST(InventoryTakePersistence, CrimeFailureRollsBackTransferSourceAndJournal)
{
    TemporaryDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const std::int64_t account = database.createAccount("inventory-rollback-account");
    const std::int64_t character = database.createCharacter(account, "Inventory Rollback Tester").characterId;
    auto commit = makeCommit(account, character);
    database.upsertContainerRecord(*commit.expectedSource);
    commit.crimeMutation = makeCrimeCommit(account, character, "inventory-crime-failure");
    commit.crimeMutation->failurePoint = mwmp::CrimeCommitFailurePoint::AfterStateWrite;

    EXPECT_THROW(database.commitInventoryTake(commit), std::runtime_error);
    EXPECT_EQ(database.loadInventoryRevision(character), 0u);
    EXPECT_TRUE(database.loadCharacterInventory(character).empty());
    ASSERT_EQ(database.loadContainerRecords().size(), 1u);
    EXPECT_EQ(database.loadContainerRecords().front().items, commit.expectedSource->items);
    EXPECT_FALSE(database.loadInventoryTake(account, character, commit.requestId).has_value());
    EXPECT_FALSE(database.loadSemanticRequest(
        "crime-event", account, character, commit.crimeMutation->requestId).has_value());
    EXPECT_EQ(database.loadPlayerCrimeState(character), mwmp::PlayerCrimeState{});
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

TEST(InventoryPutPersistence, PlayerAndContainerCommitAtomicallyAndReplayAcrossRestart)
{
    TemporaryDatabase temporary;
    std::int64_t account = 0;
    std::int64_t character = 0;
    mwmp::InventoryTakeCommit putCommit;
    {
        mwmp::PlayerDatabase database(temporary.path.string());
        account = database.createAccount("inventory-put-account");
        character = database.createCharacter(account, "Inventory Put Tester").characterId;

        mwmp::Item initial;
        initial.instanceId = 450;
        initial.refId = "iron_cuirass";
        initial.count = 2;
        initial.charge = 300;
        database.saveCharacterInventory(character, { initial }, false, 0);

        mwmp::ContainerRecord destination;
        destination.cellId = "Balmora";
        destination.refId = "crate_01";
        destination.refNum = 42;
        destination.hasAuthority = true;
        destination.items = { { "iron dagger", 1, 100 } };
        database.upsertContainerRecord(destination);

        mwmp::InventoryPutRequest request;
        request.requestId = "inventory-put-atomic-1";
        request.destination.cellId = destination.cellId;
        request.destination.refId = destination.refId;
        request.destination.refNum = destination.refNum;
        request.itemRefId = initial.refId;
        request.itemInstanceId = initial.instanceId;
        request.itemCharge = initial.charge;
        request.requestedCount = 1;
        request.expectedInventoryRevision = 0;

        putCommit.accountId = account;
        putCommit.characterId = character;
        putCommit.requestId = request.requestId;
        putCommit.requestHash = mwmp::crypto::sha256hex(mwmp::canonicalInventoryPutRequest(request));
        putCommit.expectedInventoryRevision = 0;
        putCommit.resultingInventoryRevision = 1;
        initial.count = 1;
        putCommit.inventory = { initial };
        putCommit.expectedSource = destination;
        putCommit.resultingSource = destination;
        putCommit.resultingSource->items.push_back({ request.itemRefId, 1, request.itemCharge });
        putCommit.result.requestId = request.requestId;
        putCommit.result.accepted = true;
        putCommit.result.kind = mwmp::InventoryTakeKind::Container;
        putCommit.result.source = request.destination;
        putCommit.result.itemRefId = request.itemRefId;
        putCommit.result.itemCharge = request.itemCharge;
        putCommit.result.itemCount = 1;
        putCommit.result.inventoryRevision = 1;

        EXPECT_EQ(database.commitInventoryTake(putCommit).status,
            mwmp::InventoryTakeCommitStatus::Committed);
        ASSERT_EQ(database.loadCharacterInventory(character).size(), 1u);
        EXPECT_EQ(database.loadCharacterInventory(character).front().count, 1);
        ASSERT_EQ(database.loadContainerRecords().size(), 1u);
        EXPECT_EQ(database.loadContainerRecords().front().items, putCommit.resultingSource->items);
        EXPECT_EQ(database.commitInventoryTake(putCommit).status,
            mwmp::InventoryTakeCommitStatus::DuplicateRequest);
    }

    mwmp::PlayerDatabase reopened(temporary.path.string());
    EXPECT_EQ(reopened.loadInventoryRevision(character), 1u);
    ASSERT_EQ(reopened.loadCharacterInventory(character).size(), 1u);
    EXPECT_EQ(reopened.loadCharacterInventory(character).front().count, 1);
    ASSERT_TRUE(reopened.loadInventoryTake(account, character, putCommit.requestId));
    EXPECT_EQ(reopened.loadContainerRecords().front().items, putCommit.resultingSource->items);
}
