#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "../../openmw-server/PlayerDatabase.hpp"

namespace
{
    struct TemporarySpellbookDatabase
    {
        std::filesystem::path path = std::filesystem::temp_directory_path()
            / ("openmw-spellbook-test-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".db");

        ~TemporarySpellbookDatabase()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
            std::filesystem::remove(path.string() + "-wal", error);
            std::filesystem::remove(path.string() + "-shm", error);
        }
    };

    struct TemporaryLegacyDatabase
    {
        std::filesystem::path path = std::filesystem::temp_directory_path()
            / ("openmw-spellbook-legacy-test-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".db");

        ~TemporaryLegacyDatabase()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
            std::filesystem::remove(path.string() + "-wal", error);
            std::filesystem::remove(path.string() + "-shm", error);
        }
    };

    int64_t createCharacter(mwmp::PlayerDatabase& database, const std::string& accountName,
        const std::string& characterName)
    {
        const int64_t account = database.createAccount(accountName);
        const auto character = database.createCharacter(account, characterName);
        return character.characterId;
    }

    std::vector<std::string> queryLinkRecordIds(const std::filesystem::path& dbPath, const std::string& ownerA)
    {
        sqlite3* db = nullptr;
        EXPECT_EQ(sqlite3_open(dbPath.string().c_str(), &db), SQLITE_OK);
        sqlite3_stmt* stmt = nullptr;
        EXPECT_EQ(sqlite3_prepare_v2(db,
                     "SELECT record_id FROM world_dynamic_record_links"
                     " WHERE link_kind='spellbook_spell' AND owner_a=?1 ORDER BY owner_index",
                     -1, &stmt, nullptr),
            SQLITE_OK);
        sqlite3_bind_text(stmt, 1, ownerA.c_str(), static_cast<int>(ownerA.size()), SQLITE_TRANSIENT);

        std::vector<std::string> recordIds;
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (text)
                recordIds.emplace_back(text);
        }
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return recordIds;
    }
}

TEST(PlayerSpellbookDatabase, PersistsAndReloadsExactSet)
{
    TemporarySpellbookDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const int64_t characterId = createCharacter(database, "spellbook-test", "Book Keeper");

    database.saveCharacterSpellbook(characterId, { "fireball", "frostbite", "summon_scamp" });

    EXPECT_EQ(database.loadCharacterSpellbook(characterId),
        (std::vector<std::string>{ "fireball", "frostbite", "summon_scamp" }));
}

TEST(PlayerSpellbookDatabase, AddsRemovesAndReplaces)
{
    TemporarySpellbookDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const int64_t characterId = createCharacter(database, "spellbook-test", "Book Keeper");

    database.saveCharacterSpellbook(characterId, { "fireball", "frostbite" });
    database.saveCharacterSpellbook(characterId, { "fireball", "frostbite", "summon_scamp" });
    EXPECT_EQ(database.loadCharacterSpellbook(characterId),
        (std::vector<std::string>{ "fireball", "frostbite", "summon_scamp" }));

    database.saveCharacterSpellbook(characterId, { "fireball" });
    EXPECT_EQ(database.loadCharacterSpellbook(characterId), (std::vector<std::string>{ "fireball" }));
}

TEST(PlayerSpellbookDatabase, PreventsDuplicateRows)
{
    TemporarySpellbookDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const int64_t characterId = createCharacter(database, "spellbook-test", "Book Keeper");

    database.saveCharacterSpellbook(characterId, { "fireball", "fireball", "frostbite", "fireball" });

    EXPECT_EQ(database.loadCharacterSpellbook(characterId),
        (std::vector<std::string>{ "fireball", "frostbite" }));
}

TEST(PlayerSpellbookDatabase, EmptySetIsSavedAndLoadsEmpty)
{
    TemporarySpellbookDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const int64_t characterId = createCharacter(database, "spellbook-test", "Book Keeper");

    database.saveCharacterSpellbook(characterId, {}, true, 3);

    EXPECT_TRUE(database.loadCharacterSpellbook(characterId).empty());
    EXPECT_EQ(database.loadSpellbookRevision(characterId), 3u);

    const auto character = database.lookupCharacter(database.lookupAccount("spellbook-test"), "Book Keeper");
    ASSERT_TRUE(character.has_value());
    EXPECT_TRUE(character->hasSavedSpellbook);
}

TEST(PlayerSpellbookDatabase, RevisionPersistsAndDefaultsToZero)
{
    TemporarySpellbookDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const int64_t characterId = createCharacter(database, "spellbook-test", "Book Keeper");

    EXPECT_EQ(database.loadSpellbookRevision(characterId), 0u);

    database.saveCharacterSpellbook(characterId, { "fireball" }, true, 7);
    EXPECT_EQ(database.loadSpellbookRevision(characterId), 7u);

    // Saving without an explicit revision keeps the stored revision untouched.
    database.saveCharacterSpellbook(characterId, { "fireball", "frostbite" }, false);
    EXPECT_EQ(database.loadSpellbookRevision(characterId), 7u);
}

TEST(PlayerSpellbookDatabase, CharacterDeletionCleansRows)
{
    TemporarySpellbookDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const int64_t account = database.createAccount("spellbook-test");
    const auto character = database.createCharacter(account, "Book Keeper");

    database.saveCharacterSpellbook(character.characterId, { "fireball", "frostbite" });
    ASSERT_EQ(database.loadCharacterSpellbook(character.characterId).size(), 2u);

    EXPECT_TRUE(database.deleteCharacter(account, "Book Keeper"));
    EXPECT_TRUE(database.loadCharacterSpellbook(character.characterId).empty());
}

TEST(PlayerSpellbookDatabase, RestartPersistence)
{
    TemporarySpellbookDatabase temporary;
    {
        mwmp::PlayerDatabase database(temporary.path.string());
        const int64_t characterId = createCharacter(database, "spellbook-test", "Book Keeper");
        database.saveCharacterSpellbook(characterId, { "fireball", "frostbite" }, true, 4);
    }

    // Simulate a server restart: reopen the same file in a fresh instance.
    mwmp::PlayerDatabase reopened(temporary.path.string());
    const int64_t account = reopened.lookupAccount("spellbook-test");
    const auto character = reopened.lookupCharacter(account, "Book Keeper");
    ASSERT_TRUE(character.has_value());
    EXPECT_TRUE(character->hasSavedSpellbook);
    EXPECT_EQ(reopened.loadCharacterSpellbook(character->characterId),
        (std::vector<std::string>{ "fireball", "frostbite" }));
    EXPECT_EQ(reopened.loadSpellbookRevision(character->characterId), 4u);
}

TEST(PlayerSpellbookDatabase, OneCharacterDoesNotAffectAnother)
{
    TemporarySpellbookDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const int64_t account = database.createAccount("spellbook-test");
    const auto first = database.createCharacter(account, "Book Keeper A");
    const auto second = database.createCharacter(account, "Book Keeper B");

    database.saveCharacterSpellbook(first.characterId, { "fireball" });
    database.saveCharacterSpellbook(second.characterId, { "frostbite", "summon_scamp" });

    EXPECT_EQ(database.loadCharacterSpellbook(first.characterId), (std::vector<std::string>{ "fireball" }));
    EXPECT_EQ(database.loadCharacterSpellbook(second.characterId),
        (std::vector<std::string>{ "frostbite", "summon_scamp" }));
}

TEST(PlayerSpellbookDatabase, DynamicSpellLinksAreRebuiltTransactionally)
{
    TemporarySpellbookDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const int64_t characterId = createCharacter(database, "spellbook-test", "Book Keeper");
    const std::string ownerA = std::to_string(characterId);

    // The server catalogs every dynamic record before a spellbook may
    // reference it; mirror that so the catalog link-count assertion below is
    // meaningful.
    for (const std::string& recordId : { "$custom_spell_1", "$custom_spell_2" })
    {
        mwmp::DynamicRecordCatalogEntry entry;
        entry.recordType = "spell";
        entry.recordId = recordId;
        entry.recordScope = "generated";
        entry.persistent = true;
        database.upsertDynamicRecordCatalog(entry);
    }

    // No links before any spellbook exists.
    EXPECT_TRUE(queryLinkRecordIds(temporary.path, ownerA).empty());

    // Every spellbook entry gets a link (content spells included, mirroring
    // the inventory/equipment link model). Content links are inert for GC
    // because only catalog entries can become GC candidates; dynamic spells
    // are kept alive while any link remains.
    database.saveCharacterSpellbook(characterId, { "fireball", "$custom_spell_1" });
    EXPECT_EQ(queryLinkRecordIds(temporary.path, ownerA),
        (std::vector<std::string>{ "$custom_spell_1", "fireball" }));

    // Replacing the set rebuilds links: the removed dynamic spell loses its
    // link (and becomes a GC candidate via the catalog link count).
    database.saveCharacterSpellbook(characterId, { "fireball" });
    EXPECT_EQ(queryLinkRecordIds(temporary.path, ownerA), (std::vector<std::string>{ "fireball" }));

    // Adding a second dynamic spell links both.
    database.saveCharacterSpellbook(characterId, { "fireball", "$custom_spell_1", "$custom_spell_2" });
    EXPECT_EQ(queryLinkRecordIds(temporary.path, ownerA),
        (std::vector<std::string>{ "$custom_spell_1", "$custom_spell_2", "fireball" }));

    // The catalog link count drives the dynamic-record GC decision.
    const auto catalog = database.loadDynamicRecordCatalog();
    std::size_t linkedDynamicSpells = 0;
    for (const auto& entry : catalog)
    {
        if (entry.recordId == "$custom_spell_1" || entry.recordId == "$custom_spell_2")
        {
            ++linkedDynamicSpells;
            EXPECT_EQ(entry.linkCount, 1);
        }
    }
    EXPECT_EQ(linkedDynamicSpells, 2u);
}

TEST(PlayerSpellbookDatabase, MigratesLegacyDatabaseWithoutSpellbookTable)
{
    TemporaryLegacyDatabase temporary;
    {
        // Build a database with the original pre-spellbook schema (no chargen
        // columns, no spellbook table) exactly as a Stage-1-era playerdata.db
        // would look before migrations run.
        sqlite3* db = nullptr;
        ASSERT_EQ(sqlite3_open(temporary.path.string().c_str(), &db), SQLITE_OK);
        const char* oldSchema = R"SQL(
            CREATE TABLE accounts (
                id            INTEGER PRIMARY KEY AUTOINCREMENT,
                username      TEXT    UNIQUE NOT NULL,
                created_at    INTEGER NOT NULL DEFAULT 0
            );
            CREATE TABLE characters (
                id          INTEGER PRIMARY KEY AUTOINCREMENT,
                account_id  INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
                name        TEXT    NOT NULL,
                cell        TEXT    NOT NULL DEFAULT '',
                pos_x       REAL    NOT NULL DEFAULT 0,
                pos_y       REAL    NOT NULL DEFAULT 0,
                pos_z       REAL    NOT NULL DEFAULT 0,
                rot_x       REAL    NOT NULL DEFAULT 0,
                rot_y       REAL    NOT NULL DEFAULT 0,
                rot_z       REAL    NOT NULL DEFAULT 0,
                is_new      INTEGER NOT NULL DEFAULT 1,
                last_seen   INTEGER NOT NULL DEFAULT 0
            );
        )SQL";
        ASSERT_EQ(sqlite3_exec(db, oldSchema, nullptr, nullptr, nullptr), SQLITE_OK);
        sqlite3_close(db);
    }

    // Opening the legacy file must apply all migrations without failing.
    mwmp::PlayerDatabase database(temporary.path.string());
    const int64_t characterId = createCharacter(database, "spellbook-test", "Book Keeper");

    database.saveCharacterSpellbook(characterId, { "fireball", "frostbite" }, true, 2);
    EXPECT_EQ(database.loadCharacterSpellbook(characterId),
        (std::vector<std::string>{ "fireball", "frostbite" }));
    EXPECT_EQ(database.loadSpellbookRevision(characterId), 2u);

    const auto character = database.lookupCharacter(database.lookupAccount("spellbook-test"), "Book Keeper");
    ASSERT_TRUE(character.has_value());
    EXPECT_TRUE(character->hasSavedSpellbook);
}
