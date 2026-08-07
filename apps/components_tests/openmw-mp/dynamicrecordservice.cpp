#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <limits>
#include <unordered_map>

#include <components/openmw-mp/Records/DynamicRecordCodec.hpp>
#include <components/openmw-mp/Records/DynamicRecordFingerprint.hpp>

#include "../../openmw-server/DynamicRecordService.hpp"

namespace
{
    struct TemporaryServiceDatabase
    {
        std::filesystem::path path = std::filesystem::temp_directory_path()
            / ("openmw-dynamic-record-service-test-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".db");
        ~TemporaryServiceDatabase()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
            std::filesystem::remove(path.string() + "-wal", error);
            std::filesystem::remove(path.string() + "-shm", error);
        }
    };

    mwmp::records::RecordCreateRequest makeEnchantedWeaponRequest(std::string requestId)
    {
        using namespace mwmp::records;
        RecordCreateRequest request;
        request.requestId = std::move(requestId);
        request.operation = CreateOperation::CustomRecord;

        Enchantment enchantment;
        enchantment.type = 1;
        enchantment.cost = 10;
        enchantment.charge = 100;
        enchantment.effects.push_back({ "fire damage", {}, {}, 1, 0, 1, 2, 3 });
        request.bundle.records.push_back({ "enchantment", { CurrentSchemaVersion, enchantment } });

        Weapon weapon;
        weapon.item.name = "Server Blade";
        weapon.item.model = "meshes/w/server_blade.nif";
        weapon.item.icon = "icons/w/server_blade.dds";
        weapon.item.weight = 4.f;
        weapon.item.value = 25;
        weapon.type = 1;
        weapon.health = 100;
        weapon.speed = 1.f;
        weapon.reach = 1.f;
        weapon.enchantment = { ReferenceKind::TemporaryKey, "enchantment" };
        request.bundle.records.push_back({ "weapon", { CurrentSchemaVersion, weapon } });
        request.bundle.dependencies.push_back({ "weapon", "enchantment" });
        return request;
    }
}

TEST(DynamicRecordService, CommitsDependencyBundleReplaysAndDeduplicates)
{
    TemporaryServiceDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const int64_t account = database.createAccount("service-author");
    const int64_t character = database.createCharacter(account, "Service Crafter").characterId;
    mwmp::DynamicRecordService service(database);

    mwmp::DynamicRecordService::Context context;
    context.accountId = account;
    context.characterId = character;
    context.allowCustomDefinitions = true;
    context.creationSource = "client_lua:test/script.lua";
    context.permittedTypes = { mwmp::records::RecordType::Enchantment, mwmp::records::RecordType::Weapon };
    context.isContentIdAllowed = [](std::string_view) { return true; };
    context.isAssetAllowed = [](std::string_view) { return true; };

    std::unordered_map<std::string, mwmp::DynamicRecordService::CatalogRecord> equivalents;
    uint64_t nextId = 1;
    uint64_t sequence = 1;
    auto find = [&](mwmp::records::RecordType type, std::string_view fingerprint)
        -> std::optional<mwmp::DynamicRecordService::CatalogRecord> {
        const std::string key = std::string(mwmp::records::getRecordTypeName(type)) + ":" + std::string(fingerprint);
        auto it = equivalents.find(key);
        return it == equivalents.end() ? std::nullopt : std::optional(it->second);
    };
    auto allocate = [&](mwmp::records::RecordType type) {
        return "$custom_" + std::string(mwmp::records::getRecordTypeName(type)) + "_" + std::to_string(nextId++);
    };

    auto request = makeEnchantedWeaponRequest("bundle-1");
    auto first = service.execute(request, "hash-1", context, find, allocate, [&] { return sequence++; });
    ASSERT_TRUE(first.result.accepted);
    ASSERT_EQ(first.newRecords.size(), 2u);
    EXPECT_FALSE(first.replayed);
    ASSERT_EQ(database.loadDynamicRecords().size(), 2u);
    const auto catalog = database.loadDynamicRecordCatalog();
    ASSERT_EQ(catalog.size(), 2u);
    const auto enchantmentCatalog = std::find_if(catalog.begin(), catalog.end(),
        [](const auto& value) { return value.recordType == "enchantment"; });
    ASSERT_NE(enchantmentCatalog, catalog.end());
    EXPECT_EQ(enchantmentCatalog->linkCount, 1);

    for (const auto& record : first.newRecords)
    {
        const auto definition = mwmp::records::decodeDefinition(record.definition);
        const std::string key = record.recordType + ":" + mwmp::records::fingerprint(definition);
        equivalents.emplace(key, mwmp::DynamicRecordService::CatalogRecord{
            record.recordType, record.recordId, mwmp::records::fingerprint(definition), record.definition });
    }

    auto replay = service.execute(request, "hash-1", context, find, allocate, [&] { return sequence++; });
    EXPECT_TRUE(replay.result.accepted);
    EXPECT_TRUE(replay.replayed);
    EXPECT_TRUE(replay.newRecords.empty());
    EXPECT_EQ(replay.result.records, first.result.records);

    request.requestId = "bundle-2";
    auto deduplicated = service.execute(request, "hash-2", context, find, allocate, [&] { return sequence++; });
    ASSERT_TRUE(deduplicated.result.accepted);
    EXPECT_TRUE(deduplicated.newRecords.empty());
    ASSERT_EQ(deduplicated.result.records.size(), 2u);
    EXPECT_TRUE(std::all_of(deduplicated.result.records.begin(), deduplicated.result.records.end(),
        [](const auto& value) { return value.reused; }));
    EXPECT_EQ(database.loadDynamicRecords().size(), 2u);
}

TEST(DynamicRecordService, DefaultDenyRejectionIsRestartPersistent)
{
    TemporaryServiceDatabase temporary;
    int64_t account = 0;
    int64_t character = 0;
    {
        mwmp::PlayerDatabase database(temporary.path.string());
        account = database.createAccount("denied-author");
        character = database.createCharacter(account, "Denied Crafter").characterId;
        mwmp::DynamicRecordService service(database);
        mwmp::DynamicRecordService::Context context;
        context.accountId = account;
        context.characterId = character;
        auto result = service.execute(makeEnchantedWeaponRequest("denied"), "denied-hash", context,
            [](auto, auto) { return std::optional<mwmp::DynamicRecordService::CatalogRecord>{}; },
            [](auto) { return std::string{}; }, [] { return 1u; });
        EXPECT_FALSE(result.result.accepted);
        EXPECT_EQ(result.result.error, mwmp::records::CreateError::Unauthorized);
    }

    mwmp::PlayerDatabase reopened(temporary.path.string());
    const auto journal = reopened.loadCraftRequest(account, character, "denied");
    ASSERT_TRUE(journal.has_value());
    EXPECT_EQ(journal->status, "rejected");
}

TEST(DynamicRecordService, ReferenceAdmissionRateAndQuotaFailuresAreTerminal)
{
    TemporaryServiceDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const int64_t account = database.createAccount("policy-author");
    const int64_t character = database.createCharacter(account, "Policy Crafter").characterId;
    mwmp::DynamicRecordService service(database);

    mwmp::DynamicRecordService::Context context;
    context.accountId = account;
    context.characterId = character;
    context.allowCustomDefinitions = true;
    context.permittedTypes = { mwmp::records::RecordType::Enchantment, mwmp::records::RecordType::Weapon };
    context.isContentIdAllowed = [](std::string_view) { return true; };
    context.isAssetAllowed = [](std::string_view) { return false; };
    const auto find = [](auto, auto) { return std::optional<mwmp::DynamicRecordService::CatalogRecord>{}; };
    const auto allocate = [](auto) { return std::string("$custom_test_1"); };

    auto invalidAsset = service.execute(makeEnchantedWeaponRequest("invalid-asset"), "asset-hash",
        context, find, allocate, [] { return 1u; });
    EXPECT_EQ(invalidAsset.result.error, mwmp::records::CreateError::InvalidAsset);

    context.isAssetAllowed = [](std::string_view) { return true; };
    context.maximumNewRecords = 0;
    auto quota = service.execute(makeEnchantedWeaponRequest("quota"), "quota-hash",
        context, find, allocate, [] { return 2u; });
    EXPECT_EQ(quota.result.error, mwmp::records::CreateError::QuotaExceeded);

    context.maximumNewRecords = std::numeric_limits<std::size_t>::max();
    context.admissionError = mwmp::records::CreateError::RateLimited;
    auto rate = service.execute(makeEnchantedWeaponRequest("rate"), "rate-hash",
        context, find, allocate, [] { return 3u; });
    EXPECT_EQ(rate.result.error, mwmp::records::CreateError::RateLimited);

    EXPECT_EQ(database.loadCraftRequest(account, character, "invalid-asset")->status, "rejected");
    EXPECT_EQ(database.loadCraftRequest(account, character, "quota")->status, "rejected");
    EXPECT_EQ(database.loadCraftRequest(account, character, "rate")->status, "rejected");
    EXPECT_TRUE(database.loadDynamicRecords().empty());
}

TEST(DynamicRecordService, TrustedServerLuaUsesCanonicalJournalAndReplaysAfterRestart)
{
    TemporaryServiceDatabase temporary;
    auto request = makeEnchantedWeaponRequest("server-lua-request");
    request.operation = mwmp::records::CreateOperation::ServerScript;

    mwmp::DynamicRecordService::Context context;
    context.trustedServerRequest = true;
    context.serverRequestSource = "server_lua";
    context.creationSource = "server_lua:scripts/test.lua";
    context.recordScope = "permanent";
    context.fixedRecordIds = {
        { "enchantment", "server_test_enchantment" }, { "weapon", "server_test_weapon" } };
    context.isContentIdAllowed = [](std::string_view) { return true; };
    context.isAssetAllowed = [](std::string_view) { return true; };

    mwmp::records::RecordCreateResult committedResult;
    {
        mwmp::PlayerDatabase database(temporary.path.string());
        mwmp::DynamicRecordService service(database);
        uint64_t sequence = 10;
        auto outcome = service.execute(request, "server-lua-hash", context,
            [](auto, auto) { return std::optional<mwmp::DynamicRecordService::CatalogRecord>{}; },
            [](auto) { return std::string{}; }, [&] { return sequence++; });
        ASSERT_TRUE(outcome.result.accepted);
        EXPECT_FALSE(outcome.replayed);
        ASSERT_EQ(outcome.newRecords.size(), 2u);
        EXPECT_EQ(outcome.result.records[0].recordId, "server_test_enchantment");
        EXPECT_EQ(outcome.result.records[1].recordId, "server_test_weapon");
        committedResult = outcome.result;

        const auto catalog = database.loadDynamicRecordCatalog();
        ASSERT_EQ(catalog.size(), 2u);
        const auto weapon = std::find_if(catalog.begin(), catalog.end(),
            [](const auto& entry) { return entry.recordType == "weapon"; });
        const auto enchantment = std::find_if(catalog.begin(), catalog.end(),
            [](const auto& entry) { return entry.recordType == "enchantment"; });
        ASSERT_NE(weapon, catalog.end());
        ASSERT_NE(enchantment, catalog.end());
        EXPECT_EQ(weapon->creationSource, "server_lua:scripts/test.lua");
        EXPECT_EQ(weapon->linkCount, 0);
        EXPECT_EQ(enchantment->linkCount, 1);
    }

    mwmp::PlayerDatabase reopened(temporary.path.string());
    const auto journal = reopened.loadServerRecordRequest("server_lua", request.requestId);
    ASSERT_TRUE(journal.has_value());
    EXPECT_EQ(journal->status, "accepted");
    mwmp::DynamicRecordService service(reopened);
    auto replay = service.execute(request, "server-lua-hash", context,
        [](auto, auto) { return std::optional<mwmp::DynamicRecordService::CatalogRecord>{}; },
        [](auto) { return std::string{}; }, [] { return 99u; });
    EXPECT_TRUE(replay.result.accepted);
    EXPECT_TRUE(replay.replayed);
    EXPECT_EQ(replay.result, committedResult);
    EXPECT_TRUE(replay.newRecords.empty());
}
