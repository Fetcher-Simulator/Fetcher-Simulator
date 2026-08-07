#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>

#include "../../openmw-server/PlayerDatabase.hpp"

namespace
{
    struct TemporaryDynamicRecordDatabase
    {
        std::filesystem::path path = std::filesystem::temp_directory_path()
            / ("openmw-dynamic-record-test-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".db");

        ~TemporaryDynamicRecordDatabase()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
            std::filesystem::remove(path.string() + "-wal", error);
            std::filesystem::remove(path.string() + "-shm", error);
        }
    };
}

TEST(DynamicRecordDatabase, PersistsTypedSchemaAndProvenanceMetadata)
{
    TemporaryDynamicRecordDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const int64_t account = database.createAccount("record-author");
    const auto character = database.createCharacter(account, "Artificer");

    mwmp::PersistedDynamicRecord definition;
    definition.recordType = "potion";
    definition.recordId = "$custom_potion_1";
    definition.recordScope = "generated";
    definition.data = "typed-definition";
    definition.schemaVersion = 1;
    database.upsertDynamicRecord(definition);

    mwmp::DynamicRecordCatalogEntry catalog;
    catalog.recordType = definition.recordType;
    catalog.recordId = definition.recordId;
    catalog.recordScope = definition.recordScope;
    catalog.definitionFingerprint = "fingerprint";
    catalog.creatorAccountId = account;
    catalog.creatorCharacterId = character.characterId;
    catalog.creationSource = "alchemy";
    catalog.schemaVersion = 1;
    catalog.validationVersion = 2;
    database.upsertDynamicRecordCatalog(catalog);

    const auto definitions = database.loadDynamicRecords();
    ASSERT_EQ(definitions.size(), 1u);
    EXPECT_EQ(definitions.front().schemaVersion, 1);
    EXPECT_EQ(definitions.front().data, "typed-definition");

    const auto entries = database.loadDynamicRecordCatalog();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries.front().definitionFingerprint, "fingerprint");
    EXPECT_EQ(entries.front().creatorAccountId, account);
    EXPECT_EQ(entries.front().creatorCharacterId, character.characterId);
    EXPECT_EQ(entries.front().creationSource, "alchemy");
    EXPECT_EQ(entries.front().schemaVersion, 1);
    EXPECT_EQ(entries.front().validationVersion, 2);
}

TEST(DynamicRecordDatabase, CraftRequestIdempotencySurvivesReopenAndRejectsHashReplacement)
{
    TemporaryDynamicRecordDatabase temporary;
    int64_t account = 0;
    int64_t characterId = 0;
    {
        mwmp::PlayerDatabase database(temporary.path.string());
        account = database.createAccount("idempotency-author");
        characterId = database.createCharacter(account, "Crafter").characterId;

        mwmp::CraftRequestRecord request;
        request.accountId = account;
        request.characterId = characterId;
        request.requestId = "request-42";
        request.requestHash = "hash-a";
        EXPECT_TRUE(database.insertPendingCraftRequest(request));
        EXPECT_FALSE(database.insertPendingCraftRequest(request));
        database.completeCraftRequest(account, characterId, request.requestId, request.requestHash, "accepted", "result");
    }

    mwmp::PlayerDatabase reopened(temporary.path.string());
    const auto saved = reopened.loadCraftRequest(account, characterId, "request-42");
    ASSERT_TRUE(saved.has_value());
    EXPECT_EQ(saved->requestHash, "hash-a");
    EXPECT_EQ(saved->status, "accepted");
    EXPECT_EQ(saved->resultPayload, "result");
    EXPECT_THROW(reopened.completeCraftRequest(
                     account, characterId, "request-42", "hash-b", "rejected", "different"),
        std::runtime_error);
}

TEST(DynamicRecordDatabase, AtomicRecordBundleAndInventoryCommitAdvanceOneRevision)
{
    TemporaryDynamicRecordDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const int64_t account = database.createAccount("atomic-author");
    const int64_t character = database.createCharacter(account, "Atomic Crafter").characterId;

    mwmp::Item source;
    source.refId = "ingred_bread_01";
    source.count = 2;
    source.instanceId = 10;
    database.saveCharacterInventory(character, { source });

    mwmp::DynamicRecordCommit commit;
    commit.accountId = account;
    commit.characterId = character;
    commit.requestId = "atomic-request";
    commit.requestHash = "atomic-hash";
    commit.resultPayload = "accepted-result";
    commit.expectedInventoryRevision = 0;
    commit.resultingInventoryRevision = 1;

    mwmp::DynamicRecordCommitEntry enchantment;
    enchantment.record.recordType = "enchantment";
    enchantment.record.recordId = "$custom_enchantment_1";
    enchantment.record.recordScope = "generated";
    enchantment.record.data = "enchantment-definition";
    enchantment.record.schemaVersion = 1;
    enchantment.catalog.recordType = enchantment.record.recordType;
    enchantment.catalog.recordId = enchantment.record.recordId;
    enchantment.catalog.recordScope = "generated";
    enchantment.catalog.definitionFingerprint = "enchantment-fingerprint";
    enchantment.catalog.creatorAccountId = account;
    enchantment.catalog.creatorCharacterId = character;
    enchantment.catalog.creationSource = "enchanting";
    enchantment.catalog.schemaVersion = 1;

    mwmp::DynamicRecordCommitEntry weapon = enchantment;
    weapon.record.recordType = "weapon";
    weapon.record.recordId = "$custom_weapon_1";
    weapon.record.data = "weapon-definition";
    weapon.catalog.recordType = weapon.record.recordType;
    weapon.catalog.recordId = weapon.record.recordId;
    weapon.catalog.definitionFingerprint = "weapon-fingerprint";
    weapon.dependencyRecordIds = { enchantment.record.recordId };
    commit.records = { enchantment, weapon };

    mwmp::Item result;
    result.refId = weapon.record.recordId;
    result.count = 1;
    result.instanceId = 11;
    commit.inventory = std::vector<mwmp::Item>{ result };

    EXPECT_EQ(database.commitDynamicRecordRequest(commit), mwmp::DynamicRecordCommitStatus::Committed);
    EXPECT_EQ(database.loadInventoryRevision(character), 1u);
    ASSERT_EQ(database.loadDynamicRecords().size(), 2u);
    const auto inventory = database.loadCharacterInventory(character);
    ASSERT_EQ(inventory.size(), 1u);
    EXPECT_EQ(inventory.front().refId, weapon.record.recordId);
    const auto request = database.loadCraftRequest(account, character, commit.requestId);
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->status, "accepted");
    EXPECT_EQ(request->resultPayload, commit.resultPayload);

    EXPECT_EQ(database.commitDynamicRecordRequest(commit), mwmp::DynamicRecordCommitStatus::DuplicateRequest);
    commit.requestHash = "different";
    EXPECT_EQ(database.commitDynamicRecordRequest(commit),
        mwmp::DynamicRecordCommitStatus::DuplicateRequestConflict);
}

TEST(DynamicRecordDatabase, AtomicRecordCommitRejectsStaleRevisionWithoutPartialRows)
{
    TemporaryDynamicRecordDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const int64_t account = database.createAccount("stale-author");
    const int64_t character = database.createCharacter(account, "Stale Crafter").characterId;

    mwmp::DynamicRecordCommit commit;
    commit.accountId = account;
    commit.characterId = character;
    commit.requestId = "stale-request";
    commit.requestHash = "stale-hash";
    commit.resultPayload = "must-not-persist";
    commit.expectedInventoryRevision = 9;
    commit.resultingInventoryRevision = 10;

    mwmp::DynamicRecordCommitEntry entry;
    entry.record.recordType = "potion";
    entry.record.recordId = "$custom_potion_1";
    entry.record.recordScope = "generated";
    entry.record.data = "definition";
    entry.catalog.recordType = entry.record.recordType;
    entry.catalog.recordId = entry.record.recordId;
    entry.catalog.recordScope = "generated";
    commit.records.push_back(entry);

    EXPECT_EQ(database.commitDynamicRecordRequest(commit),
        mwmp::DynamicRecordCommitStatus::StaleInventoryRevision);
    EXPECT_TRUE(database.loadDynamicRecords().empty());
    EXPECT_FALSE(database.loadCraftRequest(account, character, commit.requestId).has_value());
}

TEST(DynamicRecordDatabase, FailedLegacyReplacementRollsBackAndPreservesBackup)
{
    TemporaryDynamicRecordDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());

    mwmp::PersistedDynamicRecord legacy;
    legacy.recordType = "potion";
    legacy.recordId = "legacy_potion";
    legacy.recordScope = "permanent";
    legacy.data = "legacy-lua-bytes";
    database.upsertDynamicRecord(legacy);
    database.backupLegacyDynamicRecord(legacy);

    mwmp::DynamicRecordCommit commit;
    commit.serverSource = "legacy_migration";
    commit.requestId = "migrate-legacy-potion";
    commit.requestHash = "migration-hash";
    commit.resultPayload = "accepted-result";

    mwmp::DynamicRecordCommitEntry replacement;
    replacement.record = legacy;
    replacement.record.data = "OMDR replacement";
    replacement.record.schemaVersion = 1;
    replacement.catalog.recordType = legacy.recordType;
    replacement.catalog.recordId = legacy.recordId;
    replacement.catalog.recordScope = legacy.recordScope;
    replacement.catalog.persistent = true;
    replacement.catalog.definitionFingerprint = "fingerprint";

    mwmp::DynamicRecordCommitEntry invalid = replacement;
    invalid.record.recordId.clear();
    invalid.catalog.recordId.clear();
    commit.records = { replacement, invalid };

    EXPECT_THROW(database.commitDynamicRecordRequest(commit), std::invalid_argument);
    const auto records = database.loadDynamicRecords();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records.front().data, legacy.data);
    EXPECT_EQ(records.front().schemaVersion, 0);
    EXPECT_FALSE(database.loadServerRecordRequest(commit.serverSource, commit.requestId).has_value());

    const auto backup = database.browseTable("world_dynamic_record_legacy_backup", 0, 10);
    ASSERT_TRUE(backup.has_value());
    EXPECT_EQ(backup->totalRows, 1);
}
