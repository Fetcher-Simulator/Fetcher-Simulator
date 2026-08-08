#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <components/esm3/loadappa.hpp>
#include <components/esm3/loadclas.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadingr.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadskil.hpp>
#include <components/debug/debuglog.hpp>
#include <components/openmw-mp/Packets/Records/PacketAlchemyRequest.hpp>
#include <components/openmw-mp/Records/DynamicRecordCodec.hpp>
#include <components/openmw-mp/Sha256.hpp>

#include <apps/openmw-server/AlchemyService.hpp>
#include <apps/openmw-server/DynamicRecordService.hpp>
#include <apps/openmw-server/PlayerDatabase.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>

namespace
{
    struct TemporaryAlchemyDatabase
    {
        std::filesystem::path path = std::filesystem::temp_directory_path()
            / ("openmw-alchemy-service-test-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".db");
        ~TemporaryAlchemyDatabase()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
            std::filesystem::remove(path.string() + "-wal", error);
            std::filesystem::remove(path.string() + "-shm", error);
        }
    };

    // ---------------------------------------------------------------------
    // Content fixture: an in-memory ESMStore with the records authoritative
    // alchemy consumes.
    // ---------------------------------------------------------------------
    struct ContentFixture
    {
        MWWorld::ESMStore store;

        ContentFixture()
        {
            auto clearUnusedEffects = [](ESM::Ingredient& ingredient) {
                for (int i = 1; i < 4; ++i)
                    ingredient.mData.mEffectID[i] = ESM::RefId();
            };

            ESM::Ingredient ingredA;
            ingredA.blank();
            ingredA.mId = ESM::RefId::stringRefId("ingred_a");
            ingredA.mData.mWeight = 1.f;
            ingredA.mData.mValue = 5;
            ingredA.mData.mEffectID[0] = ESM::MagicEffect::FireDamage;
            clearUnusedEffects(ingredA);
            store.insertStatic(ingredA);

            ESM::Ingredient ingredB;
            ingredB.blank();
            ingredB.mId = ESM::RefId::stringRefId("ingred_b");
            ingredB.mData.mWeight = 2.f;
            ingredB.mData.mValue = 3;
            ingredB.mData.mEffectID[0] = ESM::MagicEffect::FireDamage;
            clearUnusedEffects(ingredB);
            store.insertStatic(ingredB);

            // Shares no effect with A or B.
            ESM::Ingredient ingredC;
            ingredC.blank();
            ingredC.mId = ESM::RefId::stringRefId("ingred_c");
            ingredC.mData.mWeight = 4.f;
            ingredC.mData.mEffectID[0] = ESM::MagicEffect::FrostDamage;
            clearUnusedEffects(ingredC);
            store.insertStatic(ingredC);

            ESM::Apparatus mortar;
            mortar.blank();
            mortar.mId = ESM::RefId::stringRefId("appa_mortar");
            mortar.mData.mType = ESM::Apparatus::MortarPestle;
            mortar.mData.mQuality = 1.f;
            store.insertStatic(mortar);

            ESM::Apparatus alembic;
            alembic.blank();
            alembic.mId = ESM::RefId::stringRefId("appa_alembic");
            alembic.mData.mType = ESM::Apparatus::Alembic;
            alembic.mData.mQuality = 1.f;
            store.insertStatic(alembic);

            ESM::Apparatus calcinator;
            calcinator.blank();
            calcinator.mId = ESM::RefId::stringRefId("appa_calcinator");
            calcinator.mData.mType = ESM::Apparatus::Calcinator;
            calcinator.mData.mQuality = 1.f;
            store.insertStatic(calcinator);

            ESM::Apparatus retort;
            retort.blank();
            retort.mId = ESM::RefId::stringRefId("appa_retort");
            retort.mData.mType = ESM::Apparatus::Retort;
            retort.mData.mQuality = 1.f;
            store.insertStatic(retort);

            ESM::MagicEffect fireDamage;
            fireDamage.blank();
            fireDamage.mId = ESM::MagicEffect::FireDamage;
            fireDamage.mData.mBaseCost = 1.f;
            fireDamage.mData.mFlags = 0;
            store.insertStatic(fireDamage);

            ESM::MagicEffect frostDamage;
            frostDamage.blank();
            frostDamage.mId = ESM::MagicEffect::FrostDamage;
            frostDamage.mData.mBaseCost = 1.f;
            frostDamage.mData.mFlags = 0;
            store.insertStatic(frostDamage);

            auto gmst = [&](std::string_view id, float value) {
                ESM::GameSetting setting;
                setting.blank();
                setting.mId = ESM::RefId::stringRefId(id);
                setting.mValue.setType(ESM::VT_Float);
                setting.mValue.setFloat(value);
                store.insertStatic(setting);
            };
            gmst("fPotionStrengthMult", 1.f);
            gmst("iAlchemyMod", 1.f);
            gmst("fPotionT1MagMult", 1.f);
            gmst("fPotionT1DurMult", 1.f);
            gmst("fMiscSkillBonus", 1.f);
            gmst("fMajorSkillBonus", 1.5f);
            gmst("fMinorSkillBonus", 1.25f);
            gmst("fSpecialSkillBonus", 1.5f);
            gmst("iLevelUpMajorMult", 2.f);
            gmst("iLevelUpMinorMult", 1.f);

            ESM::Skill alchemy;
            alchemy.blank();
            alchemy.mId = ESM::Skill::Alchemy;
            alchemy.mData.mAttribute = 1; // Intelligence
            alchemy.mData.mSpecialization = ESM::Class::Magic;
            alchemy.mData.mUseValue[0] = 1.f; // Alchemy_CreatePotion
            store.insertStatic(alchemy);
        }
    };

    mwmp::BasePlayer makePlayer(bool alchemyIsMajor = true)
    {
        mwmp::BasePlayer player;
        // Skill 90 gives factor 90 + 5 + 5 = 100, which always beats the
        // [0, 99] roll, so success tests are deterministic.
        player.skills[ESM::Skill::refIdToIndex(ESM::Skill::Alchemy)].base = 90.f;
        player.attributes[ESM::Attribute::refIdToIndex(ESM::Attribute::Intelligence)].base = 50;
        player.attributes[ESM::Attribute::refIdToIndex(ESM::Attribute::Luck)].base = 50;
        player.level = 1;
        player.levelProgress = 0.f;
        player.charClass.mName = "Test Class";
        player.charClass.mData.mSpecialization = ESM::Class::Magic;
        const int alchemyIndex = ESM::Skill::refIdToIndex(ESM::Skill::Alchemy);
        if (alchemyIsMajor)
            player.charClass.mData.mSkills[0][1] = alchemyIndex; // major
        else
            player.charClass.mData.mSkills[0][0] = alchemyIndex; // minor
        return player;
    }

    std::vector<mwmp::Item> makeInventory()
    {
        std::vector<mwmp::Item> items;
        mwmp::Item ingredA;
        ingredA.instanceId = 101;
        ingredA.refId = "ingred_a";
        ingredA.count = 3;
        ingredA.charge = -1;
        ingredA.enchantmentCharge = -1.f;
        items.push_back(ingredA);

        mwmp::Item ingredB;
        ingredB.instanceId = 102;
        ingredB.refId = "ingred_b";
        ingredB.count = 3;
        ingredB.charge = -1;
        ingredB.enchantmentCharge = -1.f;
        items.push_back(ingredB);

        mwmp::Item mortar;
        mortar.instanceId = 201;
        mortar.refId = "appa_mortar";
        mortar.count = 1;
        items.push_back(mortar);

        mwmp::Item retort;
        retort.instanceId = 204;
        retort.refId = "appa_retort";
        retort.count = 1;
        items.push_back(retort);
        return items;
    }

    // ---------------------------------------------------------------------
    // Runtime registry mirroring the server's in-memory dynamic record store.
    // ---------------------------------------------------------------------
    struct RuntimeRegistry
    {
        std::unordered_map<std::string, mwmp::DynamicRecordService::CommittedRecord> records;
        std::uint64_t nextId = 1;
        std::uint64_t sequence = 1;

        std::string allocate(mwmp::records::RecordType type)
        {
            return "$custom_" + std::string(mwmp::records::getRecordTypeName(type)) + "_" + std::to_string(nextId++);
        }

        void absorb(const mwmp::AlchemyService::Outcome& outcome)
        {
            for (const auto& record : outcome.newRecords)
                records[record.recordType + ":" + record.recordId] = record;
        }
    };

    struct Fixture
    {
        TemporaryAlchemyDatabase temporary;
        ContentFixture content;
        RuntimeRegistry registry;
        mwmp::PlayerDatabase database;
        int64_t account = 0;
        int64_t character = 0;

        Fixture()
            : database(temporary.path.string())
        {
            account = database.createAccount("alchemy-author");
            character = database.createCharacter(account, "Alchemy Crafter").characterId;
        }

        mwmp::AlchemyService::Context context(mwmp::BasePlayer& player, std::vector<mwmp::Item>& inventory,
            std::uint64_t inventoryRevision, int64_t accountIdOverride = 0, int64_t characterIdOverride = 0)
        {
            mwmp::AlchemyService::Context context;
            context.accountId = accountIdOverride != 0 ? accountIdOverride : account;
            context.characterId = characterIdOverride != 0 ? characterIdOverride : character;
            context.inventoryRevision = inventoryRevision;
            context.player = &player;
            context.inventory = &inventory;
            context.store = &content.store;
            context.creationSource = "alchemy";
            context.recordScope = "generated";
            context.persistent = true;
            context.validationVersion = 1;
            context.isContentIdAllowed = [&](std::string_view id) {
                ESM::RefId refId = ESM::RefId::deserializeText(id);
                if (refId.empty())
                    refId = ESM::RefId::stringRefId(id);
                return content.store.find(refId) != 0;
            };
            context.isModelAllowed = [](std::string_view) { return true; };
            context.isIconAllowed = [](std::string_view) { return true; };
            context.findEquivalent = [&](mwmp::records::RecordType type, std::string_view fingerprint)
                -> std::optional<mwmp::DynamicRecordService::CatalogRecord> {
                const std::string key = std::string(mwmp::records::getRecordTypeName(type)) + ":" + std::string(fingerprint);
                const auto it = registry.records.find(key);
                if (it == registry.records.end())
                    return std::nullopt;
                return mwmp::DynamicRecordService::CatalogRecord{
                    it->second.recordType, it->second.recordId, std::string(fingerprint), it->second.definition };
            };
            context.allocateId = [&](mwmp::records::RecordType type) { return registry.allocate(type); };
            context.nextCommitSequence = [&]() { return registry.sequence++; };
            context.listDynamicPotions = [&]() {
                std::vector<std::pair<std::string, std::string>> potions;
                for (const auto& [key, record] : registry.records)
                {
                    if (record.recordType == "potion")
                        potions.emplace_back(record.recordId, record.definition);
                }
                return potions;
            };
            context.reconcileInventory = [](std::vector<mwmp::Item>& items) {
                std::uint32_t nextInstanceId = 10000;
                std::unordered_set<std::uint32_t> used;
                for (auto& item : items)
                {
                    if (item.instanceId != 0)
                        used.insert(item.instanceId);
                }
                for (auto& item : items)
                {
                    if (item.instanceId == 0)
                    {
                        while (used.count(nextInstanceId) != 0)
                            ++nextInstanceId;
                        item.instanceId = nextInstanceId++;
                    }
                }
            };
            context.rngSeed = 424242; // deterministic rolls for tests
            return context;
        }

        std::string hashOf(const mwmp::records::AlchemyRequest& request)
        {
            mwmp::PacketAlchemyRequest packet;
            packet.request = request;
            const std::vector<uint8_t> bytes = packet.encode();
            return mwmp::crypto::sha256hex(std::string_view(
                reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        }

        mwmp::records::AlchemyRequest request(std::string_view requestId, std::uint32_t count = 1)
        {
            mwmp::records::AlchemyRequest request;
            request.protocolVersion = mwmp::records::CurrentAlchemyProtocolVersion;
            request.requestId = std::string(requestId);
            request.potionName = "Test Potion";
            request.count = count;
            request.ingredientInstanceIds = { 101, 102 };
            request.apparatusInstanceIds = { 201, 204 }; // mortar, retort
            return request;
        }
    };

    mwmp::Item findItem(const std::vector<mwmp::Item>& items, std::uint32_t instanceId)
    {
        const auto it = std::find_if(items.begin(), items.end(),
            [&](const mwmp::Item& item) { return item.instanceId == instanceId; });
        return it == items.end() ? mwmp::Item{} : *it;
    }

    int totalCount(const std::vector<mwmp::Item>& items, std::string_view refId)
    {
        int total = 0;
        for (const auto& item : items)
            if (item.refId == refId)
                total += item.count;
        return total;
    }
}

TEST(AlchemyService, SuccessfulBrewCommitsPotionAndConsumesExactly)
{
    Fixture fixture;
    mwmp::AlchemyService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    auto request = fixture.request("brew-1");
    auto outcome = service.execute(request, fixture.hashOf(request), fixture.context(player, inventory, 0));
    fixture.registry.absorb(outcome);

    ASSERT_TRUE(outcome.result.accepted)
        << "error=" << static_cast<int>(outcome.result.error);
    EXPECT_TRUE(outcome.committed);
    ASSERT_EQ(outcome.result.attempts.size(), 1u);
    EXPECT_TRUE(outcome.result.attempts[0].success);
    EXPECT_TRUE(outcome.result.attempts[0].recordId.starts_with("$custom_potion_"));
    EXPECT_FALSE(outcome.result.attempts[0].reused);
    EXPECT_EQ(outcome.result.inventoryRevision, 1u);
    EXPECT_EQ(outcome.resultingInventoryRevision, 1u);
    ASSERT_EQ(outcome.newRecords.size(), 1u);
    EXPECT_EQ(outcome.newRecords[0].recordType, "potion");

    // Exactly one of each ingredient consumed; the potion stack granted.
    const auto& items = outcome.resultingInventory;
    EXPECT_EQ(findItem(items, 101).count, 2);
    EXPECT_EQ(findItem(items, 102).count, 2);
    EXPECT_EQ(totalCount(items, "$custom_potion_1"), 1);
    // The granted potion received a stable instance identity.
    const auto potionIt = std::find_if(items.begin(), items.end(),
        [](const mwmp::Item& item) { return item.refId.starts_with("$custom_potion_"); });
    ASSERT_NE(potionIt, items.end());
    EXPECT_NE(potionIt->instanceId, 0u);

    // The potion definition was persisted through the canonical record layer.
    const auto records = fixture.database.loadDynamicRecords();
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].recordType, "potion");
    EXPECT_EQ(records[0].recordId, "$custom_potion_1");

    // Journal is terminal and accepted.
    const auto journal = fixture.database.loadCraftRequest(fixture.account, fixture.character, "brew-1");
    ASSERT_TRUE(journal.has_value());
    EXPECT_EQ(journal->status, "accepted");

    // The inventory revision persisted.
    EXPECT_EQ(fixture.database.loadInventoryRevision(fixture.character), 1u);
}

TEST(AlchemyService, PotionDefinitionMatchesNativeMechanics)
{
    Fixture fixture;
    mwmp::AlchemyService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    auto request = fixture.request("brew-def");
    auto outcome = service.execute(request, fixture.hashOf(request), fixture.context(player, inventory, 0));
    fixture.registry.absorb(outcome);
    ASSERT_TRUE(outcome.result.accepted);

    const auto definition = mwmp::records::decodeDefinition(outcome.newRecords[0].definition);
    const auto* potion = std::get_if<mwmp::records::Potion>(&definition.data);
    ASSERT_NE(potion, nullptr);
    EXPECT_EQ(potion->item.name, "Test Potion");
    // factor = 90 + 5 + 5 = 100; value = 100; magnitude/duration = 100 + retort 1 = 101.
    EXPECT_EQ(potion->item.value, 100);
    EXPECT_FLOAT_EQ(potion->item.weight, 1.5f);
    ASSERT_EQ(potion->effects.size(), 1u);
    EXPECT_EQ(potion->effects[0].effectId, "firedamage");
    EXPECT_EQ(potion->effects[0].magnitudeMin, 101);
    EXPECT_EQ(potion->effects[0].magnitudeMax, 101);
    EXPECT_EQ(potion->effects[0].duration, 101);
    EXPECT_FALSE(potion->item.model.empty());
    EXPECT_FALSE(potion->item.icon.empty());
}

TEST(AlchemyService, FailedRollConsumesIngredientsAndAwardsNoSkill)
{
    Fixture fixture;
    mwmp::AlchemyService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    player.skills[ESM::Skill::refIdToIndex(ESM::Skill::Alchemy)].base = 0.f;
    player.attributes[ESM::Attribute::refIdToIndex(ESM::Attribute::Intelligence)].base = 0;
    player.attributes[ESM::Attribute::refIdToIndex(ESM::Attribute::Luck)].base = 0;
    std::vector<mwmp::Item> inventory = makeInventory();

    // With factor 0 the attempt succeeds only when the roll is exactly 0.
    // Reproduce the seeded roll to make the expectation deterministic.
    Misc::Rng::Generator prng(424242);
    const int roll = Misc::Rng::roll0to99(prng);
    const bool expectedSuccess = !(0.f < static_cast<float>(roll));

    auto request = fixture.request("brew-fail");
    auto outcome = service.execute(request, fixture.hashOf(request), fixture.context(player, inventory, 0));
    fixture.registry.absorb(outcome);

    ASSERT_TRUE(outcome.result.accepted);
    ASSERT_EQ(outcome.result.attempts.size(), 1u);
    EXPECT_EQ(outcome.result.attempts[0].success, expectedSuccess);
    EXPECT_TRUE(outcome.committed);

    // Native semantics: every attempt consumes the ingredients, success or
    // failure.
    EXPECT_EQ(findItem(outcome.resultingInventory, 101).count, 2);
    EXPECT_EQ(findItem(outcome.resultingInventory, 102).count, 2);
    EXPECT_EQ(outcome.newRecords.empty(), !expectedSuccess);
    // Skill progression is awarded exactly for successes.
    EXPECT_EQ(outcome.resultingStats.has_value(), expectedSuccess);
}

TEST(AlchemyService, NoSharedEffectsCommitsFailureConsumingOneSet)
{
    Fixture fixture;
    mwmp::AlchemyService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();
    // Replace ingredient B with the unrelated ingredient C.
    inventory[1].refId = "ingred_c";

    auto request = fixture.request("brew-noeffects");
    request.count = 3; // native NoEffects consumes exactly one set, whatever the count
    auto outcome = service.execute(request, fixture.hashOf(request), fixture.context(player, inventory, 0));
    fixture.registry.absorb(outcome);

    ASSERT_TRUE(outcome.result.accepted);
    ASSERT_EQ(outcome.result.attempts.size(), 1u);
    EXPECT_FALSE(outcome.result.attempts[0].success);
    // Exactly one set consumed (native NoEffects semantics), not three.
    EXPECT_EQ(findItem(outcome.resultingInventory, 101).count, 2);
    EXPECT_EQ(findItem(outcome.resultingInventory, 102).count, 2);
    EXPECT_TRUE(outcome.newRecords.empty());
}

TEST(AlchemyService, StaleInventoryRevisionRejectsWithoutMutation)
{
    Fixture fixture;
    mwmp::AlchemyService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    auto request = fixture.request("stale");
    request.inventoryRevision = 7; // server is at 0
    auto outcome = service.execute(request, fixture.hashOf(request), fixture.context(player, inventory, 0));

    EXPECT_FALSE(outcome.result.accepted);
    EXPECT_EQ(outcome.result.error, mwmp::records::AlchemyError::StaleInventoryRevision);
    EXPECT_FALSE(outcome.committed);
    EXPECT_TRUE(fixture.database.loadDynamicRecords().empty());
    EXPECT_EQ(fixture.database.loadInventoryRevision(fixture.character), 0u);
    // Rejection is terminal in the journal.
    const auto journal = fixture.database.loadCraftRequest(fixture.account, fixture.character, "stale");
    ASSERT_TRUE(journal.has_value());
    EXPECT_EQ(journal->status, "rejected");
}

TEST(AlchemyService, MissingIngredientInstanceRejects)
{
    Fixture fixture;
    mwmp::AlchemyService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    auto request = fixture.request("missing-ingredient");
    request.ingredientInstanceIds[1] = 999;
    auto outcome = service.execute(request, fixture.hashOf(request), fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome.result.accepted);
    EXPECT_EQ(outcome.result.error, mwmp::records::AlchemyError::IngredientNotFound);
}

TEST(AlchemyService, DepletedIngredientInstanceIsNotOwned)
{
    Fixture fixture;
    mwmp::AlchemyService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();
    inventory[0].count = 0;

    auto request = fixture.request("depleted");
    auto outcome = service.execute(request, fixture.hashOf(request), fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome.result.accepted);
    EXPECT_EQ(outcome.result.error, mwmp::records::AlchemyError::IngredientNotOwned);
}

TEST(AlchemyService, WrongItemTypeAsIngredientRejects)
{
    Fixture fixture;
    mwmp::AlchemyService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();
    inventory[1].refId = "appa_mortar"; // an apparatus in the ingredient slot

    auto request = fixture.request("wrong-ingredient");
    auto outcome = service.execute(request, fixture.hashOf(request), fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome.result.accepted);
    EXPECT_EQ(outcome.result.error, mwmp::records::AlchemyError::InvalidIngredient);
}

TEST(AlchemyService, DuplicateSourceInstanceRejects)
{
    Fixture fixture;
    mwmp::AlchemyService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    auto request = fixture.request("duplicate");
    request.ingredientInstanceIds[1] = 101; // same stack twice
    auto outcome = service.execute(request, fixture.hashOf(request), fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome.result.accepted);
    EXPECT_EQ(outcome.result.error, mwmp::records::AlchemyError::DuplicateSourceInstance);

    // Same instance as ingredient and apparatus is also a duplicate.
    auto request2 = fixture.request("duplicate-2");
    request2.apparatusInstanceIds = { 101, 204 };
    auto outcome2 = service.execute(request2, fixture.hashOf(request2), fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome2.result.accepted);
    EXPECT_EQ(outcome2.result.error, mwmp::records::AlchemyError::DuplicateSourceInstance);
}

TEST(AlchemyService, MissingApparatusRejects)
{
    Fixture fixture;
    mwmp::AlchemyService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    auto request = fixture.request("missing-apparatus");
    request.apparatusInstanceIds = { 999, 204 };
    auto outcome = service.execute(request, fixture.hashOf(request), fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome.result.accepted);
    EXPECT_EQ(outcome.result.error, mwmp::records::AlchemyError::ApparatusNotFound);
}

TEST(AlchemyService, WrongApparatusTypeRejects)
{
    Fixture fixture;
    mwmp::AlchemyService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    // Two apparatus of the same type are an invalid combination because the
    // native UI has exactly one slot per type.
    inventory.push_back([] {
        mwmp::Item secondMortar;
        secondMortar.instanceId = 203;
        secondMortar.refId = "appa_mortar2";
        secondMortar.count = 1;
        return secondMortar;
    }());
    ESM::Apparatus secondMortar;
    secondMortar.blank();
    secondMortar.mId = ESM::RefId::stringRefId("appa_mortar2");
    secondMortar.mData.mType = ESM::Apparatus::MortarPestle;
    secondMortar.mData.mQuality = 2.f;
    fixture.content.store.insertStatic(secondMortar);

    auto request = fixture.request("wrong-apparatus");
    request.apparatusInstanceIds = { 201, 203 }; // two mortars
    auto outcome = service.execute(request, fixture.hashOf(request), fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome.result.accepted);
    EXPECT_EQ(outcome.result.error, mwmp::records::AlchemyError::InvalidApparatus);
}

TEST(AlchemyService, MissingMortarRejects)
{
    Fixture fixture;
    mwmp::AlchemyService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    auto request = fixture.request("no-mortar");
    request.apparatusInstanceIds = { 204 }; // only a retort
    auto outcome = service.execute(request, fixture.hashOf(request), fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome.result.accepted);
    EXPECT_EQ(outcome.result.error, mwmp::records::AlchemyError::InvalidApparatus);
}

TEST(AlchemyService, MultipleAttemptsConsumeExactAmountsAndGrantPerSuccess)
{
    Fixture fixture;
    mwmp::AlchemyService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    player.skills[ESM::Skill::refIdToIndex(ESM::Skill::Alchemy)].base = 500.f; // always succeeds
    std::vector<mwmp::Item> inventory = makeInventory();

    auto request = fixture.request("brew-3");
    request.count = 3;
    auto outcome = service.execute(request, fixture.hashOf(request), fixture.context(player, inventory, 0));
    fixture.registry.absorb(outcome);

    ASSERT_TRUE(outcome.result.accepted);
    ASSERT_EQ(outcome.result.attempts.size(), 3u);
    EXPECT_TRUE(outcome.result.attempts[0].success);
    EXPECT_TRUE(outcome.result.attempts[1].success);
    EXPECT_TRUE(outcome.result.attempts[2].success);
    EXPECT_EQ(findItem(outcome.resultingInventory, 101).count, 0);
    EXPECT_EQ(findItem(outcome.resultingInventory, 102).count, 0);
    EXPECT_EQ(totalCount(outcome.resultingInventory, "$custom_potion_1"), 3);
    EXPECT_EQ(fixture.database.loadInventoryRevision(fixture.character), 1u);
}

TEST(AlchemyService, SkillProgressionIsAwardedPerSuccess)
{
    Fixture fixture;
    mwmp::AlchemyService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    const int index = ESM::Skill::refIdToIndex(ESM::Skill::Alchemy);
    player.skills[index].base = 90.f;
    player.skills[index].progress = 0.f;
    std::vector<mwmp::Item> inventory = makeInventory();

    auto request = fixture.request("skill");
    auto outcome = service.execute(request, fixture.hashOf(request), fixture.context(player, inventory, 0));
    fixture.registry.absorb(outcome);
    ASSERT_TRUE(outcome.result.accepted);
    ASSERT_TRUE(outcome.result.attempts[0].success);

    ASSERT_TRUE(outcome.resultingStats.has_value());
    const auto& skill = outcome.resultingStats->skills[index];
    // Major skill: requirement = (90 + 1) * fMajorSkillBonus(1.5) *
    // fSpecialSkillBonus(1.5) = 204.75; gain = mUseValue[0] = 1.
    EXPECT_NEAR(skill.progress, 1.f / 204.75f, 0.0001f);
    EXPECT_EQ(skill.base, 90.f);
    EXPECT_EQ(fixture.database.loadCraftRequest(fixture.account, fixture.character, "skill")->status, "accepted");
}

TEST(AlchemyService, SkillLevelUpAdvancesBaseAndLevelProgress)
{
    Fixture fixture;
    mwmp::AlchemyService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    const int index = ESM::Skill::refIdToIndex(ESM::Skill::Alchemy);
    player.skills[index].base = 10.f;
    player.skills[index].progress = 0.99f;
    // High attributes keep the roll winning while the skill base stays low so
    // the progression requirement is small.
    player.attributes[ESM::Attribute::refIdToIndex(ESM::Attribute::Intelligence)].base = 500;
    player.attributes[ESM::Attribute::refIdToIndex(ESM::Attribute::Luck)].base = 500;
    std::vector<mwmp::Item> inventory = makeInventory();

    // gain 1 / requirement 24.75 pushes progress from 0.99 past 1.0.
    auto request = fixture.request("levelup");
    auto outcome = service.execute(request, fixture.hashOf(request), fixture.context(player, inventory, 0));
    fixture.registry.absorb(outcome);
    ASSERT_TRUE(outcome.result.accepted);
    ASSERT_TRUE(outcome.result.attempts[0].success);
    ASSERT_TRUE(outcome.resultingStats.has_value());
    EXPECT_EQ(outcome.resultingStats->skills[index].base, 11.f);
    EXPECT_FLOAT_EQ(outcome.resultingStats->skills[index].progress, 0.f);
    // Major skill level-up adds iLevelUpMajorMult = 2 to level progress.
    EXPECT_FLOAT_EQ(outcome.resultingStats->levelProgress, 2.f);
    EXPECT_EQ(outcome.resultingStats->level, 1);
}

TEST(AlchemyService, EquivalentPotionReusesCanonicalRecord)
{
    Fixture fixture;
    mwmp::AlchemyService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    auto first = fixture.request("dedup-1");
    auto outcome1 = service.execute(first, fixture.hashOf(first), fixture.context(player, inventory, 0));
    fixture.registry.absorb(outcome1);
    ASSERT_TRUE(outcome1.result.accepted);
    const std::string firstId = outcome1.result.attempts[0].recordId;
    EXPECT_EQ(firstId, "$custom_potion_1");

    // A second identical brew reuses the canonical record, even though the
    // rolled mesh differs, because the native getRecord-equivalent search
    // ignores model/icon. The server mirror now reflects the first commit.
    auto second = fixture.request("dedup-2");
    second.inventoryRevision = 1;
    std::vector<mwmp::Item> inventory2 = outcome1.resultingInventory;
    auto outcome2 = service.execute(
        second, fixture.hashOf(second), fixture.context(player, inventory2, 1));
    fixture.registry.absorb(outcome2);
    ASSERT_TRUE(outcome2.result.accepted);
    EXPECT_TRUE(outcome2.result.attempts[0].success);
    EXPECT_EQ(outcome2.result.attempts[0].recordId, firstId);
    EXPECT_TRUE(outcome2.result.attempts[0].reused);
    EXPECT_TRUE(outcome2.newRecords.empty());
    EXPECT_EQ(fixture.database.loadDynamicRecords().size(), 1u);
}

TEST(AlchemyService, RetryReplaysAcceptedRequestWithoutMutation)
{
    Fixture fixture;
    mwmp::AlchemyService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    auto request = fixture.request("retry-accepted");
    const std::string hash = fixture.hashOf(request);
    auto first = service.execute(request, hash, fixture.context(player, inventory, 0));
    fixture.registry.absorb(first);
    ASSERT_TRUE(first.result.accepted);

    auto retry = service.execute(request, hash, fixture.context(player, inventory, 0));
    EXPECT_TRUE(retry.result.accepted);
    EXPECT_TRUE(retry.replayed);
    EXPECT_EQ(retry.result, first.result);
    EXPECT_TRUE(retry.newRecords.empty());
    EXPECT_FALSE(retry.committed);
    // Nothing was consumed a second time.
    EXPECT_EQ(fixture.database.loadInventoryRevision(fixture.character), 1u);
    EXPECT_EQ(fixture.database.loadDynamicRecords().size(), 1u);
}

TEST(AlchemyService, RetryReplaysRejectedRequest)
{
    Fixture fixture;
    mwmp::AlchemyService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    auto request = fixture.request("retry-rejected");
    request.ingredientInstanceIds[1] = 999; // missing ingredient
    const std::string hash = fixture.hashOf(request);
    auto first = service.execute(request, hash, fixture.context(player, inventory, 0));
    EXPECT_FALSE(first.result.accepted);
    EXPECT_EQ(first.result.error, mwmp::records::AlchemyError::IngredientNotFound);

    auto retry = service.execute(request, hash, fixture.context(player, inventory, 0));
    EXPECT_FALSE(retry.result.accepted);
    EXPECT_TRUE(retry.replayed);
    EXPECT_EQ(retry.result, first.result);
}

TEST(AlchemyService, DuplicateRequestIdWithDifferentHashConflicts)
{
    Fixture fixture;
    mwmp::AlchemyService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    auto request = fixture.request("conflict");
    const std::string hash = fixture.hashOf(request);
    auto first = service.execute(request, hash, fixture.context(player, inventory, 0));
    ASSERT_TRUE(first.result.accepted);

    auto tampered = fixture.request("conflict");
    tampered.potionName = "Tampered Potion"; // same ID, different content
    auto second = service.execute(tampered, fixture.hashOf(tampered), fixture.context(player, inventory, 0));
    EXPECT_FALSE(second.result.accepted);
    EXPECT_EQ(second.result.error, mwmp::records::AlchemyError::DuplicateRequestConflict);
    // The first commit is untouched.
    EXPECT_EQ(fixture.database.loadInventoryRevision(fixture.character), 1u);
    EXPECT_EQ(fixture.database.loadDynamicRecords().size(), 1u);
}

TEST(AlchemyService, UnsupportedProtocolRejects)
{
    Fixture fixture;
    mwmp::AlchemyService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    auto request = fixture.request("version");
    request.protocolVersion = mwmp::records::CurrentAlchemyProtocolVersion + 1;
    auto outcome = service.execute(request, fixture.hashOf(request), fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome.result.accepted);
    EXPECT_EQ(outcome.result.error, mwmp::records::AlchemyError::UnsupportedProtocol);
}

TEST(AlchemyService, DatabaseFailureRollsBackEverything)
{
    Fixture fixture;
    mwmp::AlchemyService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    // Remove the character row through a second connection; the commit must
    // detect the identity mismatch and roll back every statement.
    {
        mwmp::PlayerDatabase second(fixture.temporary.path.string());
        ASSERT_TRUE(second.deleteCharacter(fixture.account, "Alchemy Crafter"));
    }

    auto request = fixture.request("rollback");
    EXPECT_THROW(service.execute(request, fixture.hashOf(request), fixture.context(player, inventory, 0)),
        std::runtime_error);

    // Nothing partial may persist: no journal, no records.
    EXPECT_FALSE(fixture.database.loadCraftRequest(fixture.account, fixture.character, "rollback").has_value());
    EXPECT_TRUE(fixture.database.loadDynamicRecords().empty());
    EXPECT_TRUE(fixture.database.loadCharacterInventory(fixture.character).empty());
}

TEST(AlchemyService, ServerRestartReplaysCommittedRequest)
{
    mwmp::records::AlchemyResult committed;
    {
        Fixture fixture;
        mwmp::AlchemyService service(fixture.database);
        mwmp::BasePlayer player = makePlayer();
        std::vector<mwmp::Item> inventory = makeInventory();

        auto request = fixture.request("restart");
        auto outcome = service.execute(request, fixture.hashOf(request), fixture.context(player, inventory, 0));
        fixture.registry.absorb(outcome);
        ASSERT_TRUE(outcome.result.accepted);
        committed = outcome.result;

        // The potion record is persisted and loads on restart.
        const auto records = fixture.database.loadDynamicRecords();
        ASSERT_EQ(records.size(), 1u);
        const auto definition = mwmp::records::decodeDefinition(records[0].data);
        EXPECT_EQ(records[0].recordId, "$custom_potion_1");
        EXPECT_EQ(std::get_if<mwmp::records::Potion>(&definition.data)->item.name, "Test Potion");

        // Simulate a fresh server process: reopen the database.
        mwmp::PlayerDatabase reopened(fixture.temporary.path.string());
        mwmp::AlchemyService restarted(reopened);
        auto retry = restarted.execute(request, fixture.hashOf(request),
            [&] {
                mwmp::AlchemyService::Context context = fixture.context(player, inventory, 0);
                return context;
            }());
        EXPECT_TRUE(retry.result.accepted);
        EXPECT_TRUE(retry.replayed);
        EXPECT_EQ(retry.result, committed);
        EXPECT_TRUE(retry.newRecords.empty());
        EXPECT_FALSE(retry.committed);
        EXPECT_EQ(reopened.loadInventoryRevision(fixture.character), 1u);
        EXPECT_EQ(reopened.loadDynamicRecords().size(), 1u);
    }
}

TEST(AlchemyService, TwoPlayersShareOneCanonicalDefinition)
{
    Fixture fixture;
    mwmp::AlchemyService service(fixture.database);
    const int64_t characterB = fixture.database.createCharacter(fixture.account, "Alchemy Crafter B").characterId;

    mwmp::BasePlayer playerA = makePlayer();
    mwmp::BasePlayer playerB = makePlayer();
    std::vector<mwmp::Item> inventoryA = makeInventory();
    std::vector<mwmp::Item> inventoryB = makeInventory();

    // Both players run against the same registry and content, exactly like
    // two clients connected to one server.
    auto contextA = fixture.context(playerA, inventoryA, 0);
    auto contextB = fixture.context(playerB, inventoryB, 0, fixture.account, characterB);

    auto requestA = fixture.request("player-a");
    auto outcomeA = service.execute(requestA, fixture.hashOf(requestA), contextA);
    fixture.registry.absorb(outcomeA);
    ASSERT_TRUE(outcomeA.result.accepted);
    const std::string sharedId = outcomeA.result.attempts[0].recordId;

    auto requestB = fixture.request("player-b");
    auto outcomeB = service.execute(requestB, fixture.hashOf(requestB), contextB);
    fixture.registry.absorb(outcomeB);
    ASSERT_TRUE(outcomeB.result.accepted);
    EXPECT_TRUE(outcomeB.result.attempts[0].success);
    EXPECT_EQ(outcomeB.result.attempts[0].recordId, sharedId);
    EXPECT_TRUE(outcomeB.result.attempts[0].reused);

    // Exactly one reusable dynamic definition, two independent grants.
    EXPECT_EQ(fixture.database.loadDynamicRecords().size(), 1u);
    EXPECT_EQ(totalCount(outcomeA.resultingInventory, sharedId), 1);
    EXPECT_EQ(totalCount(outcomeB.resultingInventory, sharedId), 1);
    const auto potionA = std::find_if(outcomeA.resultingInventory.begin(), outcomeA.resultingInventory.end(),
        [&](const mwmp::Item& item) { return item.refId == sharedId; });
    ASSERT_NE(potionA, outcomeA.resultingInventory.end());
    EXPECT_NE(potionA->instanceId, 0u);
}

TEST(AlchemyService, InventoryRevisionRaceFailsDeterministically)
{
    Fixture fixture;
    mwmp::AlchemyService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    // First request commits at revision 0 -> 1.
    auto first = fixture.request("race-1");
    auto outcome1 = service.execute(first, fixture.hashOf(first), fixture.context(player, inventory, 0));
    fixture.registry.absorb(outcome1);
    ASSERT_TRUE(outcome1.result.accepted);

    // A second request that was captured against the same revision must fail
    // deterministically; the commit detects the advanced authoritative
    // revision and rejects without consuming anything.
    auto second = fixture.request("race-2");
    mwmp::AlchemyService::Context stale = fixture.context(player, inventory, 0);
    auto outcome2 = service.execute(second, fixture.hashOf(second), stale);
    EXPECT_FALSE(outcome2.result.accepted);
    EXPECT_EQ(outcome2.result.error, mwmp::records::AlchemyError::StaleInventoryRevision);
    EXPECT_FALSE(outcome2.committed);
    EXPECT_EQ(outcome2.result.inventoryRevision, 1u); // authoritative revision reported
    EXPECT_EQ(fixture.database.loadInventoryRevision(fixture.character), 1u);
    EXPECT_EQ(fixture.database.loadDynamicRecords().size(), 1u);
}

TEST(AlchemyService, PendingJournalEntryReportsRequestPending)
{
    Fixture fixture;
    mwmp::AlchemyService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    auto request = fixture.request("pending");
    const std::string hash = fixture.hashOf(request);
    mwmp::CraftRequestRecord pending;
    pending.accountId = fixture.account;
    pending.characterId = fixture.character;
    pending.requestId = "pending";
    pending.requestHash = hash;
    ASSERT_TRUE(fixture.database.insertPendingCraftRequest(pending));

    auto outcome = service.execute(request, hash, fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome.result.accepted);
    EXPECT_EQ(outcome.result.error, mwmp::records::AlchemyError::RequestPending);
}

TEST(AlchemyService, QuotaLimitsNewPotionRecords)
{
    Fixture fixture;
    mwmp::AlchemyService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    auto context = fixture.context(player, inventory, 0);
    context.maximumNewRecords = 0;
    auto request = fixture.request("quota");
    auto outcome = service.execute(request, fixture.hashOf(request), context);
    EXPECT_FALSE(outcome.result.accepted);
    EXPECT_EQ(outcome.result.error, mwmp::records::AlchemyError::QuotaExceeded);
    EXPECT_TRUE(fixture.database.loadDynamicRecords().empty());
}

TEST(AlchemyService, RateLimitAdmissionRejects)
{
    Fixture fixture;
    mwmp::AlchemyService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    auto context = fixture.context(player, inventory, 0);
    context.admissionError = mwmp::records::CreateError::RateLimited;
    auto request = fixture.request("rate");
    auto outcome = service.execute(request, fixture.hashOf(request), context);
    EXPECT_FALSE(outcome.result.accepted);
    EXPECT_EQ(outcome.result.error, mwmp::records::AlchemyError::RateLimited);
}

TEST(AlchemyService, MechanicsValidationFailureIsTerminal)
{
    Fixture fixture;
    mwmp::AlchemyService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();
    // Override FireDamage with a dynamic record whose base cost is invalid;
    // the dynamic store wins lookups and the mechanics must fail validation.
    ESM::MagicEffect broken;
    broken.blank();
    broken.mId = ESM::MagicEffect::FireDamage;
    broken.mData.mBaseCost = 0.f;
    fixture.content.store.getWritable<ESM::MagicEffect>().insert(broken);

    auto request = fixture.request("invalid-mechanics");
    auto outcome = service.execute(request, fixture.hashOf(request), fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome.result.accepted);
    EXPECT_EQ(outcome.result.error, mwmp::records::AlchemyError::MechanicsValidationFailed);
    EXPECT_TRUE(fixture.database.loadDynamicRecords().empty());
}
