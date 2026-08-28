#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>

#include <apps/openmw-server/PlayerDatabase.hpp>
#include <apps/openmw/mwmp/sync/InventoryIdentity.hpp>
#include <components/openmw-mp/Sha256.hpp>
#include <components/openmw-mp/InventoryPut.hpp>
#include <sqlite3.h>

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

TEST(InventoryTakeIdentity, AuthoritativeContainerInstanceIdRoundTripsThroughClientRefNum)
{
    constexpr std::uint32_t AuthoritativeInstanceId = 6335;
    const ESM::RefNum reconstructedRefNum = mwmp::inventoryInstanceRefNum(AuthoritativeInstanceId);

    EXPECT_TRUE(reconstructedRefNum.isSet());
    EXPECT_EQ(mwmp::inventoryInstanceId(reconstructedRefNum), AuthoritativeInstanceId);
}

TEST(InventoryTakePersistence, ContainerIdentityMigrationPreservesLegacyRowsAndSeparatesSpawnedInstances)
{
    TemporaryDatabase temporary;
    sqlite3* raw = nullptr;
    ASSERT_EQ(sqlite3_open(temporary.path.string().c_str(), &raw), SQLITE_OK);
    const char* legacySchema = R"SQL(
CREATE TABLE world_containers (
    cell_id TEXT NOT NULL,
    ref_id TEXT NOT NULL,
    ref_num INTEGER NOT NULL DEFAULT 0,
    mp_num INTEGER NOT NULL DEFAULT 0,
    has_authority INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY(cell_id, ref_id, ref_num)
);
CREATE TABLE world_container_items (
    cell_id TEXT NOT NULL,
    ref_id TEXT NOT NULL,
    ref_num INTEGER NOT NULL DEFAULT 0,
    item_index INTEGER NOT NULL,
    item_ref_id TEXT NOT NULL,
    item_count INTEGER NOT NULL DEFAULT 0,
    charge INTEGER NOT NULL DEFAULT -1,
    PRIMARY KEY(cell_id, ref_id, ref_num, item_index),
    FOREIGN KEY(cell_id, ref_id, ref_num)
        REFERENCES world_containers(cell_id, ref_id, ref_num) ON DELETE CASCADE
);
INSERT INTO world_containers(cell_id, ref_id, ref_num, mp_num, has_authority)
    VALUES('Balmora', 'crate_01', 42, 0, 1);
INSERT INTO world_container_items(cell_id, ref_id, ref_num, item_index, item_ref_id, item_count, charge)
    VALUES('Balmora', 'crate_01', 42, 0, 'gold_001', 7, -1);
)SQL";
    char* error = nullptr;
    ASSERT_EQ(sqlite3_exec(raw, legacySchema, nullptr, nullptr, &error), SQLITE_OK)
        << (error ? error : "unknown sqlite error");
    if (error)
        sqlite3_free(error);
    ASSERT_EQ(sqlite3_close(raw), SQLITE_OK);

    {
        mwmp::PlayerDatabase database(temporary.path.string());
        const auto migrated = database.loadContainerRecords();
        ASSERT_EQ(migrated.size(), 1u);
        EXPECT_EQ(migrated.front().cellId, "Balmora");
        EXPECT_EQ(migrated.front().refId, "crate_01");
        EXPECT_EQ(migrated.front().refNum, 42u);
        EXPECT_EQ(migrated.front().mpNum, 0u);
        ASSERT_EQ(migrated.front().items.size(), 1u);
        EXPECT_EQ(migrated.front().items.front().refId, "gold_001");
        EXPECT_EQ(migrated.front().items.front().count, 7);

        mwmp::ContainerRecord first;
        first.cellId = "Balmora";
        first.refId = "r_bc_dyn_bard_fargoth";
        first.refNum = 0;
        first.mpNum = 1001;
        first.hasAuthority = true;
        first.items = { { "r_bc_fiddle", 1, -1 } };
        database.upsertContainerRecord(first);

        mwmp::ContainerRecord second = first;
        second.mpNum = 1002;
        second.items = { { "r_bc_ocarina", 1, -1 } };
        database.upsertContainerRecord(second);

        const auto records = database.loadContainerRecords();
        ASSERT_EQ(records.size(), 3u);
        const auto firstIt = std::find_if(records.begin(), records.end(),
            [](const mwmp::ContainerRecord& record) { return record.mpNum == 1001; });
        const auto secondIt = std::find_if(records.begin(), records.end(),
            [](const mwmp::ContainerRecord& record) { return record.mpNum == 1002; });
        ASSERT_NE(firstIt, records.end());
        ASSERT_NE(secondIt, records.end());
        ASSERT_EQ(firstIt->items.size(), 1u);
        ASSERT_EQ(secondIt->items.size(), 1u);
        EXPECT_EQ(firstIt->items.front().refId, "r_bc_fiddle");
        EXPECT_EQ(secondIt->items.front().refId, "r_bc_ocarina");
    }

    mwmp::PlayerDatabase reopened(temporary.path.string());
    const auto records = reopened.loadContainerRecords();
    EXPECT_EQ(std::count_if(records.begin(), records.end(),
                  [](const mwmp::ContainerRecord& record) { return record.mpNum != 0; }),
        2);
}

TEST(InventoryTakePersistence, SpawnedCorpseTakeDoesNotMutateSiblingInstance)
{
    TemporaryDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const std::int64_t account = database.createAccount("spawned-corpse-account");
    const std::int64_t character = database.createCharacter(account, "Spawned Corpse Tester").characterId;

    mwmp::ContainerRecord first;
    first.cellId = "Balmora";
    first.refId = "r_bc_dyn_bard_fargoth";
    first.refNum = 0;
    first.mpNum = 2001;
    first.hasAuthority = true;
    first.items = { { "gold_001", 10, -1 } };
    database.upsertContainerRecord(first);

    mwmp::ContainerRecord second = first;
    second.mpNum = 2002;
    second.items.front().count = 20;
    database.upsertContainerRecord(second);

    mwmp::InventoryTakeCommit commit;
    commit.accountId = account;
    commit.characterId = character;
    commit.requestId = "spawned-corpse-take-1";
    commit.requestHash = mwmp::crypto::sha256hex(commit.requestId);
    commit.expectedInventoryRevision = 0;
    commit.resultingInventoryRevision = 1;
    commit.expectedSource = first;
    commit.resultingSource = first;
    commit.resultingSource->items.front().count = 5;
    commit.result.requestId = commit.requestId;
    commit.result.accepted = true;
    commit.result.kind = mwmp::InventoryTakeKind::Corpse;
    commit.result.source.cellId = first.cellId;
    commit.result.source.refId = first.refId;
    commit.result.source.refNum = first.refNum;
    commit.result.source.mpNum = first.mpNum;
    commit.result.source.actorInstanceId = (1ull << 32) | first.mpNum;
    commit.result.source.migrationGeneration = 1;
    commit.result.itemRefId = "gold_001";
    commit.result.itemCharge = -1;
    commit.result.itemCount = 5;
    commit.result.inventoryRevision = 1;
    mwmp::Item received;
    received.instanceId = 1901;
    received.refId = "gold_001";
    received.count = 5;
    commit.inventory = { received };

    ASSERT_EQ(database.commitInventoryTake(commit).status, mwmp::InventoryTakeCommitStatus::Committed);
    const auto records = database.loadContainerRecords();
    const auto firstIt = std::find_if(records.begin(), records.end(),
        [](const mwmp::ContainerRecord& record) { return record.mpNum == 2001; });
    const auto secondIt = std::find_if(records.begin(), records.end(),
        [](const mwmp::ContainerRecord& record) { return record.mpNum == 2002; });
    ASSERT_NE(firstIt, records.end());
    ASSERT_NE(secondIt, records.end());
    ASSERT_EQ(firstIt->items.size(), 1u);
    ASSERT_EQ(secondIt->items.size(), 1u);
    EXPECT_EQ(firstIt->items.front().count, 5);
    EXPECT_EQ(secondIt->items.front().count, 20);
}

TEST(InventoryPutPersistence, SpawnedCorpsePutDoesNotMutateSiblingInstance)
{
    TemporaryDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const std::int64_t account = database.createAccount("spawned-corpse-put-account");
    const std::int64_t character = database.createCharacter(account, "Spawned Corpse Put Tester").characterId;

    mwmp::Item carried;
    carried.instanceId = 2901;
    carried.refId = "iron_cuirass";
    carried.count = 1;
    carried.charge = 300;
    database.saveCharacterInventory(character, { carried }, false, 0);

    mwmp::ContainerRecord first;
    first.cellId = "Balmora";
    first.refId = "r_bc_dyn_bard_fargoth";
    first.refNum = 0;
    first.mpNum = 3001;
    first.hasAuthority = true;
    first.items = { { "gold_001", 10, -1 } };
    database.upsertContainerRecord(first);

    mwmp::ContainerRecord second = first;
    second.mpNum = 3002;
    second.items.front().count = 20;
    database.upsertContainerRecord(second);

    mwmp::InventoryPutRequest request;
    request.requestId = "spawned-corpse-put-1";
    request.destination.cellId = first.cellId;
    request.destination.refId = first.refId;
    request.destination.mpNum = first.mpNum;
    request.destination.actorInstanceId
        = mwmp::packActorInstanceKey({ mwmp::ActorKeyKind::SpawnedMpNum, first.mpNum });
    request.destination.migrationGeneration = 1;
    request.itemRefId = carried.refId;
    request.itemInstanceId = carried.instanceId;
    request.itemCharge = carried.charge;
    request.requestedCount = 1;
    request.expectedInventoryRevision = 0;

    mwmp::InventoryTakeCommit commit;
    commit.accountId = account;
    commit.characterId = character;
    commit.requestId = request.requestId;
    commit.requestHash = mwmp::crypto::sha256hex(mwmp::canonicalInventoryPutRequest(request));
    commit.expectedInventoryRevision = 0;
    commit.resultingInventoryRevision = 1;
    commit.inventory = {};
    commit.expectedSource = first;
    commit.resultingSource = first;
    mwmp::ContainerItem returned;
    returned.refId = carried.refId;
    returned.count = 1;
    returned.charge = carried.charge;
    returned.instanceId = carried.instanceId;
    commit.resultingSource->items.push_back(returned);
    commit.result.requestId = request.requestId;
    commit.result.accepted = true;
    commit.result.kind = mwmp::InventoryTakeKind::Corpse;
    commit.result.source = request.destination;
    commit.result.itemRefId = request.itemRefId;
    commit.result.itemCharge = request.itemCharge;
    commit.result.itemCount = request.requestedCount;
    commit.result.inventoryRevision = 1;

    ASSERT_EQ(database.commitInventoryTake(commit).status, mwmp::InventoryTakeCommitStatus::Committed);
    EXPECT_TRUE(database.loadCharacterInventory(character).empty());

    const auto records = database.loadContainerRecords();
    const auto firstIt = std::find_if(records.begin(), records.end(),
        [](const mwmp::ContainerRecord& record) { return record.mpNum == 3001; });
    const auto secondIt = std::find_if(records.begin(), records.end(),
        [](const mwmp::ContainerRecord& record) { return record.mpNum == 3002; });
    ASSERT_NE(firstIt, records.end());
    ASSERT_NE(secondIt, records.end());
    ASSERT_EQ(firstIt->items.size(), 2u);
    ASSERT_EQ(secondIt->items.size(), 1u);
    EXPECT_NE(std::find_if(firstIt->items.begin(), firstIt->items.end(), [](const mwmp::ContainerItem& item) {
                  return item.refId == "iron_cuirass" && item.count == 1 && item.instanceId == 2901;
              }),
        firstIt->items.end());
    EXPECT_EQ(secondIt->items.front().refId, "gold_001");
    EXPECT_EQ(secondIt->items.front().count, 20);
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

TEST(InventoryTakePersistence, AggregateBackingRowsCommitAndReplayAtomicallyAcrossRestart)
{
    TemporaryDatabase temporary;
    std::int64_t account = 0;
    std::int64_t character = 0;
    mwmp::InventoryTakeCommit commit;
    {
        mwmp::PlayerDatabase database(temporary.path.string());
        account = database.createAccount("aggregate-take-account");
        character = database.createCharacter(account, "Aggregate Take Tester").characterId;

        mwmp::ContainerRecord source;
        source.cellId = "Taris, Lower City Black Market";
        source.refId = "SW_TarisChestLCVend1";
        source.refNum = 29598;
        source.hasAuthority = true;
        for (const auto [count, instanceId] : {
                 std::pair{ 3, 6337u }, std::pair{ 1, 6338u },
                 std::pair{ 1, 6339u }, std::pair{ 1, 6340u } })
        {
            mwmp::ContainerItem item;
            item.refId = "sw_medkit";
            item.count = count;
            item.instanceId = instanceId;
            source.items.push_back(item);
        }
        database.upsertContainerRecord(source);

        commit.accountId = account;
        commit.characterId = character;
        commit.requestId = "aggregate-take-6";
        commit.requestHash = mwmp::crypto::sha256hex(commit.requestId);
        commit.expectedInventoryRevision = 0;
        commit.resultingInventoryRevision = 1;
        commit.expectedSource = source;
        commit.resultingSource = source;
        commit.resultingSource->items.clear();
        commit.result.requestId = commit.requestId;
        commit.result.accepted = true;
        commit.result.kind = mwmp::InventoryTakeKind::Container;
        commit.result.source.cellId = source.cellId;
        commit.result.source.refId = source.refId;
        commit.result.source.refNum = source.refNum;
        commit.result.itemRefId = "sw_medkit";
        commit.result.itemCount = 6;
        commit.result.inventoryRevision = 1;
        mwmp::Item received;
        received.instanceId = 6337;
        received.refId = "sw_medkit";
        received.count = 6;
        commit.inventory = { received };

        EXPECT_EQ(database.commitInventoryTake(commit).status, mwmp::InventoryTakeCommitStatus::Committed);
        EXPECT_TRUE(database.loadContainerRecords().front().items.empty());
        ASSERT_EQ(database.loadCharacterInventory(character).size(), 1u);
        EXPECT_EQ(database.loadCharacterInventory(character).front().count, 6);
    }

    mwmp::PlayerDatabase reopened(temporary.path.string());
    const auto replay = reopened.commitInventoryTake(commit);
    EXPECT_EQ(replay.status, mwmp::InventoryTakeCommitStatus::DuplicateRequest);
    EXPECT_TRUE(replay.result.replayed);
    EXPECT_EQ(reopened.loadInventoryRevision(character), 1u);
    EXPECT_TRUE(reopened.loadContainerRecords().front().items.empty());
    ASSERT_EQ(reopened.loadCharacterInventory(character).size(), 1u);
    EXPECT_EQ(reopened.loadCharacterInventory(character).front().count, 6);
}

TEST(InventoryTakePersistence, BarterGoldAndVendorStockCommitAtomicallyAcrossRestart)
{
    TemporaryDatabase temporary;
    std::int64_t account = 0;
    std::int64_t character = 0;
    mwmp::InventoryTakeCommit commit;
    {
        mwmp::PlayerDatabase database(temporary.path.string());
        account = database.createAccount("barter-account");
        character = database.createCharacter(account, "Barter Tester").characterId;

        mwmp::Item startingGold;
        startingGold.instanceId = 7001;
        startingGold.refId = "gold_001";
        startingGold.count = 10000;
        database.saveCharacterInventory(character, { startingGold }, false, 0);

        mwmp::ContainerRecord source;
        source.cellId = "Taris, Lower City Black Market";
        source.refId = "SW_TarisChestLCVend1";
        source.refNum = 29598;
        source.hasAuthority = true;
        source.items = { { "SW_BlasterRepeater", 1, -1 } };
        database.upsertContainerRecord(source);

        commit.accountId = account;
        commit.characterId = character;
        commit.requestId = "barter-take-1";
        commit.requestHash = mwmp::crypto::sha256hex("barter-take-1:5625");
        commit.expectedInventoryRevision = 0;
        commit.resultingInventoryRevision = 1;
        commit.expectedSource = source;
        commit.resultingSource = source;
        commit.resultingSource->items.clear();
        commit.result.requestId = commit.requestId;
        commit.result.accepted = true;
        commit.result.kind = mwmp::InventoryTakeKind::Barter;
        commit.result.source.cellId = source.cellId;
        commit.result.source.refId = source.refId;
        commit.result.source.refNum = source.refNum;
        commit.result.itemRefId = "SW_BlasterRepeater";
        commit.result.itemCount = 1;
        commit.result.inventoryRevision = 1;

        mwmp::Item remainingGold = startingGold;
        remainingGold.count = 4375;
        mwmp::Item purchased;
        purchased.instanceId = 7002;
        purchased.refId = "SW_BlasterRepeater";
        purchased.count = 1;
        commit.inventory = { remainingGold, purchased };

        const auto committed = database.commitInventoryTake(commit);
        ASSERT_EQ(committed.status, mwmp::InventoryTakeCommitStatus::Committed);
        EXPECT_EQ(database.loadInventoryRevision(character), 1u);
        const auto inventory = database.loadCharacterInventory(character);
        ASSERT_EQ(inventory.size(), 2u);
        const auto gold = std::find_if(inventory.begin(), inventory.end(),
            [](const mwmp::Item& item) { return item.refId == "gold_001"; });
        ASSERT_NE(gold, inventory.end());
        EXPECT_EQ(gold->count, 4375);
        const auto blaster = std::find_if(inventory.begin(), inventory.end(),
            [](const mwmp::Item& item) { return item.refId == "SW_BlasterRepeater"; });
        ASSERT_NE(blaster, inventory.end());
        EXPECT_EQ(blaster->count, 1);
        const auto containers = database.loadContainerRecords();
        ASSERT_EQ(containers.size(), 1u);
        EXPECT_TRUE(containers.front().items.empty());

        const auto replay = database.commitInventoryTake(commit);
        EXPECT_EQ(replay.status, mwmp::InventoryTakeCommitStatus::DuplicateRequest);
        EXPECT_TRUE(replay.result.replayed);
        EXPECT_EQ(database.loadCharacterInventory(character), inventory);
        EXPECT_TRUE(database.loadContainerRecords().front().items.empty());
    }

    mwmp::PlayerDatabase reopened(temporary.path.string());
    EXPECT_EQ(reopened.loadInventoryRevision(character), 1u);
    const auto inventory = reopened.loadCharacterInventory(character);
    ASSERT_EQ(inventory.size(), 2u);
    EXPECT_EQ(std::count_if(inventory.begin(), inventory.end(),
                  [](const mwmp::Item& item) { return item.refId == "SW_BlasterRepeater" && item.count == 1; }),
        1);
    const auto gold = std::find_if(inventory.begin(), inventory.end(),
        [](const mwmp::Item& item) { return item.refId == "gold_001"; });
    ASSERT_NE(gold, inventory.end());
    EXPECT_EQ(gold->count, 4375);
    ASSERT_EQ(reopened.loadContainerRecords().size(), 1u);
    EXPECT_TRUE(reopened.loadContainerRecords().front().items.empty());
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
