#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <sqlite3.h>
#include <stdexcept>
#include <components/openmw-mp/InventorySync.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerInventory.hpp>

#include "../../openmw-server/PlayerDatabase.hpp"

namespace
{
    struct TemporaryInventoryDatabase
    {
        std::filesystem::path path = std::filesystem::temp_directory_path()
            / ("openmw-inventory-test-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".db");

        ~TemporaryInventoryDatabase()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
            std::filesystem::remove(path.string() + "-wal", error);
            std::filesystem::remove(path.string() + "-shm", error);
        }
    };

    TEST(PlayerInventoryDatabase, PersistsStableInventoryAndEquipmentIdentity)
    {
        TemporaryInventoryDatabase temporary;
        mwmp::PlayerDatabase database(temporary.path.string());
        const int64_t account = database.createAccount("inventory-test");
        const auto character = database.createCharacter(account, "Stack Keeper");

        mwmp::Item item;
        item.instanceId = 7723;
        item.refId = "server_custom_blade";
        item.count = 1;
        item.charge = 42;
        item.enchantmentCharge = 9.f;
        item.soul = "golden saint";
        database.saveCharacterInventory(character.characterId, { item });

        mwmp::EquipmentItem equipment;
        equipment.slot = 0;
        equipment.item = item;
        database.saveCharacterEquipment(character.characterId, { equipment });

        const auto loadedInventory = database.loadCharacterInventory(character.characterId);
        ASSERT_EQ(loadedInventory.size(), 1u);
        EXPECT_EQ(loadedInventory.front().instanceId, 7723u);
        EXPECT_EQ(loadedInventory.front().refId, "server_custom_blade");

        const auto loadedEquipment = database.loadCharacterEquipment(character.characterId);
        ASSERT_EQ(loadedEquipment.size(), 1u);
        EXPECT_EQ(loadedEquipment.front().item.instanceId, 7723u);
    }
}

namespace
{
    // A separate connection observes real SQLite writes, including trigger
    // effects, without exposing the database implementation's private handle.
    struct InventorySqlObserver
    {
        sqlite3* db = nullptr;
        explicit InventorySqlObserver(const std::filesystem::path& path)
        {
            if (sqlite3_open(path.string().c_str(), &db) != SQLITE_OK)
                throw std::runtime_error("open inventory observer");
        }
        ~InventorySqlObserver() { sqlite3_close(db); }
        void exec(const char* sql)
        {
            if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
                throw std::runtime_error(sqlite3_errmsg(db));
        }
        int scalar(const char* sql)
        {
            sqlite3_stmt* statement = nullptr;
            if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
                throw std::runtime_error(sqlite3_errmsg(db));
            const int status = sqlite3_step(statement);
            const int value = status == SQLITE_ROW ? sqlite3_column_int(statement, 0) : -1;
            sqlite3_finalize(statement);
            return value;
        }
    };

    std::vector<mwmp::Item> largeInventory()
    {
        std::vector<mwmp::Item> items(254);
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            items[i].instanceId = static_cast<uint32_t>(i + 1);
            items[i].refId = "test_blade_" + std::to_string(i);
            items[i].count = 1;
            items[i].enchantmentCharge = 10.f;
        }
        items[0].refId = "gold_001";
        items[0].count = 1000;
        return items;
    }

    TEST(PlayerInventoryDatabase, RechargeWritesOnlyChangedRowAndRestoresAfterReopen)
    {
        TemporaryInventoryDatabase temporary;
        auto items = largeInventory();
        int64_t characterId = 0;
        {
            mwmp::PlayerDatabase database(temporary.path.string());
            characterId = database.createCharacter(database.createAccount("recharge"), "Sith Soldier").characterId;
            database.saveCharacterInventory(characterId, items, false, 7);
            mwmp::EquipmentItem equipment;
            equipment.slot = 16;
            equipment.item = items[42];
            database.saveCharacterEquipment(characterId, { equipment });
            InventorySqlObserver observer(temporary.path);
            observer.exec(
                "CREATE TABLE inventory_writes(kind TEXT);"
                "CREATE TRIGGER inventory_insert AFTER INSERT ON character_inventory BEGIN "
                "INSERT INTO inventory_writes VALUES('insert'); END;"
                "CREATE TRIGGER inventory_delete AFTER DELETE ON character_inventory BEGIN "
                "INSERT INTO inventory_writes VALUES('delete'); END;"
                "CREATE TRIGGER inventory_update AFTER UPDATE ON character_inventory BEGIN "
                "INSERT INTO inventory_writes VALUES('update'); END;"
                "CREATE TRIGGER links_delete AFTER DELETE ON world_dynamic_record_links BEGIN "
                "INSERT INTO inventory_writes VALUES('link'); END;"
                "CREATE TRIGGER links_insert AFTER INSERT ON world_dynamic_record_links BEGIN "
                "INSERT INTO inventory_writes VALUES('link'); END;");
            items[42].enchantmentCharge += 0.02f;
            database.saveCharacterInventory(characterId, items, true, 8);
            EXPECT_EQ(observer.scalar("SELECT count(*) FROM inventory_writes"), 1);
            EXPECT_EQ(observer.scalar("SELECT count(*) FROM inventory_writes WHERE kind='update'"), 1);
            EXPECT_EQ(database.loadInventoryRevision(characterId), 8u);
            EXPECT_EQ(database.loadCharacterEquipment(characterId)[0].item.instanceId, items[42].instanceId);

            // An unchanged snapshot may advance the revision but must not churn rows or links.
            database.saveCharacterInventory(characterId, items, false, 9);
            EXPECT_EQ(observer.scalar("SELECT count(*) FROM inventory_writes"), 1);
            EXPECT_EQ(database.loadInventoryRevision(characterId), 9u);
        }
        {
            mwmp::PlayerDatabase reopened(temporary.path.string());
            const auto loaded = reopened.loadCharacterInventory(characterId);
            ASSERT_EQ(loaded.size(), 254u);
            EXPECT_TRUE(mwmp::inventoryAckMatchesSentSnapshot(loaded, items));
            EXPECT_FLOAT_EQ(loaded[42].enchantmentCharge, items[42].enchantmentCharge);
            EXPECT_EQ(loaded[0].count, 1000);
            EXPECT_EQ(loaded[0].instanceId, 1u);
            EXPECT_EQ(reopened.loadInventoryRevision(characterId), 9u);

            // The unchanged Set replication format delivers the same full state to
            // a reconnecting player and a peer, with stable equipment identity.
            mwmp::BasePlayer authoritative;
            authoritative.inventoryChanges.items = loaded;
            authoritative.inventoryChanges.revision = reopened.loadInventoryRevision(characterId);
            mwmp::PacketPlayerInventory encoder;
            encoder.setPlayer(&authoritative);
            mwmp::BasePlayer peer;
            mwmp::PacketPlayerInventory decoder;
            decoder.setPlayer(&peer);
            ASSERT_TRUE(decoder.decode(encoder.encode()));
            EXPECT_TRUE(mwmp::inventoryAckMatchesSentSnapshot(peer.inventoryChanges.items, items));
            EXPECT_EQ(peer.inventoryChanges.revision, 9u);
        }
    }

    TEST(PlayerInventoryDatabase, RevisionFailureRollsBackRechargeAndAllowsRetry)
    {
        TemporaryInventoryDatabase temporary;
        mwmp::PlayerDatabase database(temporary.path.string());
        const auto characterId = database.createCharacter(database.createAccount("rollback"), "Sith Soldier").characterId;
        auto items = largeInventory();
        database.saveCharacterInventory(characterId, items, false, 7);
        InventorySqlObserver observer(temporary.path);
        observer.exec("CREATE TRIGGER reject_revision BEFORE UPDATE OF inventory_revision ON characters "
            "BEGIN SELECT RAISE(ABORT, 'injected revision failure'); END;");
        const float previousCharge = items[42].enchantmentCharge;
        items[42].enchantmentCharge += 0.02f;
        EXPECT_THROW(database.saveCharacterInventory(characterId, items, true, 8), std::runtime_error);
        EXPECT_EQ(database.loadInventoryRevision(characterId), 7u);
        EXPECT_FLOAT_EQ(database.loadCharacterInventory(characterId)[42].enchantmentCharge, previousCharge);
        observer.exec("DROP TRIGGER reject_revision");
        EXPECT_NO_THROW(database.saveCharacterInventory(characterId, items, true, 8));
        EXPECT_EQ(database.loadInventoryRevision(characterId), 8u);
        EXPECT_FLOAT_EQ(database.loadCharacterInventory(characterId)[42].enchantmentCharge, items[42].enchantmentCharge);
    }

    TEST(PlayerInventoryDatabase, StructuralMutationsStillReplaceSnapshotAndOwnershipLinks)
    {
        TemporaryInventoryDatabase temporary;
        mwmp::PlayerDatabase database(temporary.path.string());
        const auto characterId = database.createCharacter(database.createAccount("structure"), "Stack Keeper").characterId;
        auto items = largeInventory();
        items.resize(3);
        items[1].count = 4;
        database.saveCharacterInventory(characterId, items, false, 1);
        // A split creates a separate stable identity, even with matching metadata.
        auto split = items[1];
        split.count = 3;
        split.instanceId = 400;
        items[1].count = 1;
        items.push_back(split);
        database.saveCharacterInventory(characterId, items, false, 2);
        EXPECT_TRUE(mwmp::inventoryAckMatchesSentSnapshot(database.loadCharacterInventory(characterId), items));
        // Reordering cannot update the wrong row; a changed RefId rebuilds its link.
        std::swap(items[1], items[3]);
        items[1].refId = "generated_blade";
        items[1].charge = 73;
        items[1].soul = "golden saint";
        database.saveCharacterInventory(characterId, items, false, 3);
        InventorySqlObserver observer(temporary.path);
        EXPECT_EQ(observer.scalar("SELECT count(*) FROM world_dynamic_record_links "
            "WHERE link_kind='inventory_item' AND record_id='generated_blade' AND owner_index=400"), 1);
        EXPECT_TRUE(mwmp::inventoryAckMatchesSentSnapshot(database.loadCharacterInventory(characterId), items));
        items.pop_back();
        database.saveCharacterInventory(characterId, items, false, 4);
        EXPECT_TRUE(mwmp::inventoryAckMatchesSentSnapshot(database.loadCharacterInventory(characterId), items));
        database.saveCharacterInventory(characterId, {}, false, 5);
        EXPECT_TRUE(database.loadCharacterInventory(characterId).empty());
        EXPECT_EQ(observer.scalar("SELECT count(*) FROM world_dynamic_record_links WHERE link_kind='inventory_item'"), 0);
        EXPECT_EQ(database.loadInventoryRevision(characterId), 5u);
    }
}
