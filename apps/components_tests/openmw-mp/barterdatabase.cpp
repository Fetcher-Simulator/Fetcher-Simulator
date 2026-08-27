#include <gtest/gtest.h>

#include <apps/openmw-server/PlayerDatabase.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>

namespace
{
    struct TemporaryDatabase
    {
        std::filesystem::path path = std::filesystem::temp_directory_path()
            / ("openmw-barter-test-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".db");

        ~TemporaryDatabase()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
            std::filesystem::remove(path.string() + "-wal", error);
            std::filesystem::remove(path.string() + "-shm", error);
        }
    };

    mwmp::ContainerRecord container(std::string refId, std::uint32_t refNum,
        std::vector<mwmp::ContainerItem> items)
    {
        mwmp::ContainerRecord value;
        value.cellId = "Taris";
        value.refId = std::move(refId);
        value.refNum = refNum;
        value.items = std::move(items);
        value.hasAuthority = true;
        return value;
    }

    mwmp::InventoryTakeCommit commit(std::int64_t accountId, std::int64_t characterId,
        std::string requestId, std::string requestHash, std::uint64_t revision,
        const std::vector<mwmp::Item>& inventory)
    {
        mwmp::InventoryTakeCommit value;
        value.accountId = accountId;
        value.characterId = characterId;
        value.requestId = std::move(requestId);
        value.requestHash = std::move(requestHash);
        value.expectedInventoryRevision = revision;
        value.resultingInventoryRevision = revision + 1;
        value.inventory = inventory;
        value.result.requestId = value.requestId;
        value.result.accepted = true;
        value.result.kind = mwmp::InventoryTakeKind::Barter;
        value.result.itemRefId = "__barter_batch__";
        value.result.itemCharge = 1100;
        value.result.inventoryRevision = revision + 1;
        return value;
    }

    void expectMerchantGold(mwmp::PlayerDatabase& database, mwmp::ActorInstanceId actorInstanceId,
        std::int32_t gold, double lastRestockTime)
    {
        const auto state = database.loadMerchantGold(actorInstanceId);
        ASSERT_TRUE(state.has_value());
        EXPECT_EQ(state->gold, gold);
        EXPECT_DOUBLE_EQ(state->lastRestockTime, lastRestockTime);
    }
}

TEST(BarterDatabase, CommitsMultipleContainersWorldTombstoneInventoryAndGoldAtomically)
{
    TemporaryDatabase temporary;
    std::int64_t accountId = 0;
    std::int64_t characterId = 0;
    mwmp::InventoryTakeCommit transaction;
    {
        mwmp::PlayerDatabase database(temporary.path.string());
        accountId = database.createAccount("barter-atomic");
        const auto character = database.createCharacter(accountId, "Trader");
        characterId = character.characterId;

        mwmp::Item initialGold { 7001, "gold_001", 500, -1, -1.f, "" };
        mwmp::Item soldStack { 7002, "repair_prongs", 2, 8, -1.f, "" };
        database.saveCharacterInventory(characterId, { initialGold, soldStack }, false, 5);

        const auto finite = container("vendor_chest", 10,
            { { "finite_blaster", 2, 50, 8001, -1.f, "" } });
        const auto restock = container("restock_chest", 11,
            { { "spice", 10, -1, 0, -1.f, "", true } });
        const auto merchant = container("merchant", 12, {});
        database.upsertContainerRecord(finite);
        database.upsertContainerRecord(restock);
        database.upsertContainerRecord(merchant);

        mwmp::Item resultingGold = initialGold;
        resultingGold.count = 400;
        mwmp::Item remainingSoldStack = soldStack;
        remainingSoldStack.count = 1;
        mwmp::Item purchased { 9001, "finite_blaster", 1, 50, -1.f, "" };
        mwmp::Item restocked { 9002, "spice", 2, -1, -1.f, "" };
        transaction = commit(accountId, characterId, "barter-1", "hash-1", 5,
            { resultingGold, remainingSoldStack, purchased, restocked });

        auto finiteResult = finite;
        finiteResult.items[0].count = 1;
        auto merchantResult = merchant;
        merchantResult.items.push_back({ "repair_prongs", 1, 8, 9100, -1.f, "" });
        transaction.containerMutations = {
            { finite, finiteResult }, { restock, restock }, { merchant, merchantResult }
        };
        mwmp::PlacedObjectIdentity loose;
        loose.kind = mwmp::PlacedObjectKind::ContentReference;
        loose.cellId = "Taris";
        loose.refId = "loose_part";
        loose.refIndex = 33;
        loose.refContentFile = 1;
        transaction.worldItemMutations.push_back({ loose, 1 });
        transaction.merchantGoldMutation = mwmp::MerchantGoldMutation{
            mwmp::packActorInstanceKey({ mwmp::ActorKeyKind::VanillaRefNum, 12 }),
            "merchant", 1000, 1100, 0.0, 100.0 };

        const auto accepted = database.commitInventoryTake(transaction);
        EXPECT_EQ(accepted.status, mwmp::InventoryTakeCommitStatus::Committed);
        expectMerchantGold(database, transaction.merchantGoldMutation->actorInstanceId, 1100, 100.0);
        EXPECT_EQ(database.loadTakenWorldItemReferences(), std::vector<mwmp::PlacedObjectIdentity>{ loose });

        const auto replay = database.commitInventoryTake(transaction);
        EXPECT_EQ(replay.status, mwmp::InventoryTakeCommitStatus::DuplicateRequest);
        auto conflict = transaction;
        conflict.requestHash = "different-hash";
        EXPECT_EQ(database.commitInventoryTake(conflict).status,
            mwmp::InventoryTakeCommitStatus::DuplicateRequestConflict);

        auto staleWorld = commit(accountId, characterId, "barter-2", "hash-2", 6,
            transaction.inventory);
        staleWorld.containerMutations = {
            { finiteResult, finiteResult }, { restock, restock }, { merchantResult, merchantResult }
        };
        staleWorld.worldItemMutations = transaction.worldItemMutations;
        staleWorld.merchantGoldMutation = mwmp::MerchantGoldMutation{
            transaction.merchantGoldMutation->actorInstanceId, "merchant", 1100, 1200, 100.0, 124.0 };
        EXPECT_EQ(database.commitInventoryTake(staleWorld).status,
            mwmp::InventoryTakeCommitStatus::StaleSource);
        expectMerchantGold(database, transaction.merchantGoldMutation->actorInstanceId, 1100, 100.0);
    }

    mwmp::PlayerDatabase reopened(temporary.path.string());
    const auto inventory = reopened.loadCharacterInventory(characterId);
    ASSERT_EQ(inventory.size(), 4u);
    EXPECT_EQ(inventory[0].count, 400);
    EXPECT_EQ(inventory[1].refId, "repair_prongs");
    EXPECT_EQ(inventory[1].count, 1);
    expectMerchantGold(reopened, transaction.merchantGoldMutation->actorInstanceId, 1100, 100.0);
    const auto containers = reopened.loadContainerRecords();
    ASSERT_EQ(containers.size(), 3u);
    const auto restock = std::find_if(containers.begin(), containers.end(),
        [](const auto& value) { return value.refId == "restock_chest"; });
    ASSERT_NE(restock, containers.end());
    ASSERT_EQ(restock->items.size(), 1u);
    EXPECT_TRUE(restock->items[0].restocking);
    EXPECT_EQ(restock->items[0].count, 10);
    const auto finite = std::find_if(containers.begin(), containers.end(),
        [](const auto& value) { return value.refId == "vendor_chest"; });
    ASSERT_NE(finite, containers.end());
    EXPECT_EQ(finite->items[0].count, 1);
    const auto merchant = std::find_if(containers.begin(), containers.end(),
        [](const auto& value) { return value.refId == "merchant"; });
    ASSERT_NE(merchant, containers.end());
    ASSERT_EQ(merchant->items.size(), 1u);
    EXPECT_EQ(merchant->items[0].refId, "repair_prongs");
    EXPECT_EQ(merchant->items[0].count, 1);
}

TEST(BarterDatabase, StaleOneOfManySourcesRollsBackEverything)
{
    TemporaryDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const auto accountId = database.createAccount("barter-stale");
    const auto character = database.createCharacter(accountId, "Trader");
    mwmp::Item gold { 7001, "gold_001", 500, -1, -1.f, "" };
    database.saveCharacterInventory(character.characterId, { gold }, false, 2);

    const auto first = container("first", 10, { { "item_a", 2, -1, 1, -1.f, "" } });
    const auto second = container("second", 11, { { "item_b", 2, -1, 2, -1.f, "" } });
    database.upsertContainerRecord(first);
    database.upsertContainerRecord(second);
    auto staleSecond = second;
    staleSecond.items[0].count = 99;
    auto firstResult = first;
    firstResult.items[0].count = 1;

    auto transaction = commit(accountId, character.characterId, "barter-stale", "hash-stale", 2, {});
    transaction.containerMutations = { { first, firstResult }, { staleSecond, second } };
    transaction.merchantGoldMutation = { mwmp::packActorInstanceKey(
        { mwmp::ActorKeyKind::VanillaRefNum, 12 }), "merchant", 1000, 1100 };
    mwmp::PlacedObjectIdentity loose;
    loose.kind = mwmp::PlacedObjectKind::ContentReference;
    loose.cellId = "Taris";
    loose.refId = "loose_part";
    loose.refIndex = 33;
    loose.refContentFile = 1;
    transaction.worldItemMutations.push_back({ loose, 1 });

    EXPECT_EQ(database.commitInventoryTake(transaction).status, mwmp::InventoryTakeCommitStatus::StaleSource);
    const auto inventory = database.loadCharacterInventory(character.characterId);
    ASSERT_EQ(inventory.size(), 1u);
    EXPECT_EQ(inventory[0], gold);
    EXPECT_FALSE(database.loadMerchantGold(transaction.merchantGoldMutation->actorInstanceId).has_value());
    EXPECT_TRUE(database.loadTakenWorldItemReferences().empty());
    const auto containers = database.loadContainerRecords();
    const auto storedFirst = std::find_if(containers.begin(), containers.end(),
        [](const auto& value) { return value.refId == "first"; });
    ASSERT_NE(storedFirst, containers.end());
    EXPECT_EQ(storedFirst->items[0].count, 2);

    auto staleRevision = transaction;
    staleRevision.requestId = "barter-stale-revision";
    staleRevision.requestHash = "hash-stale-revision";
    staleRevision.expectedInventoryRevision = 99;
    staleRevision.resultingInventoryRevision = 100;
    staleRevision.containerMutations = { { first, firstResult }, { second, second } };
    EXPECT_EQ(database.commitInventoryTake(staleRevision).status,
        mwmp::InventoryTakeCommitStatus::StaleInventoryRevision);
    EXPECT_FALSE(database.loadMerchantGold(transaction.merchantGoldMutation->actorInstanceId).has_value());
    EXPECT_TRUE(database.loadTakenWorldItemReferences().empty());
}

TEST(BarterDatabase, PartialLooseWorldStackPersistsAndOrdinaryTakeConsumesRemainderAtomically)
{
    TemporaryDatabase temporary;
    std::int64_t accountId = 0;
    std::int64_t characterId = 0;
    mwmp::PlacedObjectIdentity loose;
    loose.kind = mwmp::PlacedObjectKind::ContentReference;
    loose.cellId = "Taris";
    loose.refId = "loose_stack";
    loose.refIndex = 44;
    loose.refContentFile = 1;

    {
        mwmp::PlayerDatabase database(temporary.path.string());
        accountId = database.createAccount("barter-partial-world");
        const auto character = database.createCharacter(accountId, "Trader");
        characterId = character.characterId;
        database.saveCharacterInventory(characterId, {}, false, 1);

        mwmp::Item purchased { 9200, "loose_stack", 3, -1, -1.f, "" };
        auto partial = commit(accountId, characterId, "barter-partial", "hash-partial", 1, { purchased });
        partial.worldItemMutations.push_back({ loose, 10, 7 });
        EXPECT_EQ(database.commitInventoryTake(partial).status, mwmp::InventoryTakeCommitStatus::Committed);

        const auto overrides = database.loadWorldItemCountOverrides();
        ASSERT_EQ(overrides.size(), 1u);
        EXPECT_EQ(overrides[0].object, loose);
        EXPECT_EQ(overrides[0].resultingWorldCount, 7);
        EXPECT_TRUE(database.loadTakenWorldItemReferences().empty());
    }

    mwmp::PlayerDatabase reopened(temporary.path.string());
    const auto overrides = reopened.loadWorldItemCountOverrides();
    ASSERT_EQ(overrides.size(), 1u);
    EXPECT_EQ(overrides[0].object, loose);
    EXPECT_EQ(overrides[0].resultingWorldCount, 7);

    mwmp::Item fullStack { 9200, "loose_stack", 10, -1, -1.f, "" };
    mwmp::WorldItemTakeCommit stale;
    stale.accountId = accountId;
    stale.characterId = characterId;
    stale.requestId = "world-take-stale";
    stale.requestHash = "hash-world-stale";
    stale.object = loose;
    stale.result.requestId = stale.requestId;
    stale.result.accepted = true;
    stale.result.object = loose;
    stale.result.itemRefId = "loose_stack";
    stale.result.itemCount = 7;
    stale.result.inventoryRevision = 3;
    stale.expectedWorldCount = 10;
    stale.expectedInventoryRevision = 2;
    stale.resultingInventoryRevision = 3;
    stale.inventory = { fullStack };
    EXPECT_EQ(reopened.commitWorldItemTake(stale).status, mwmp::WorldItemTakeCommitStatus::StaleSource);
    ASSERT_EQ(reopened.loadWorldItemCountOverrides().size(), 1u);
    EXPECT_TRUE(reopened.loadTakenWorldItemReferences().empty());
    const auto afterStaleInventory = reopened.loadCharacterInventory(characterId);
    ASSERT_EQ(afterStaleInventory.size(), 1u);
    EXPECT_EQ(afterStaleInventory[0].count, 3);

    auto take = stale;
    take.requestId = "world-take-remainder";
    take.requestHash = "hash-world-remainder";
    take.result.requestId = take.requestId;
    take.expectedWorldCount = 7;
    const auto accepted = reopened.commitWorldItemTake(take);
    EXPECT_EQ(accepted.status, mwmp::WorldItemTakeCommitStatus::Committed);
    EXPECT_TRUE(reopened.loadWorldItemCountOverrides().empty());
    EXPECT_EQ(reopened.loadTakenWorldItemReferences(), std::vector<mwmp::PlacedObjectIdentity>{ loose });
    const auto inventory = reopened.loadCharacterInventory(characterId);
    ASSERT_EQ(inventory.size(), 1u);
    EXPECT_EQ(inventory[0].refId, "loose_stack");
    EXPECT_EQ(inventory[0].count, 10);

    const auto replay = reopened.commitWorldItemTake(take);
    EXPECT_EQ(replay.status, mwmp::WorldItemTakeCommitStatus::DuplicateRequest);
    EXPECT_TRUE(reopened.loadWorldItemCountOverrides().empty());
    EXPECT_EQ(reopened.loadTakenWorldItemReferences(), std::vector<mwmp::PlacedObjectIdentity>{ loose });
}
