#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadbook.hpp>
#include <components/esm3/loadclas.hpp>
#include <components/esm3/loadclot.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadench.hpp>
#include <components/esm3/loadgmst.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadmisc.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadskil.hpp>
#include <components/esm3/loadweap.hpp>
#include <components/debug/debuglog.hpp>
#include <components/openmw-mp/Packets/Records/PacketEnchantingRequest.hpp>
#include <components/openmw-mp/Records/DynamicRecordCodec.hpp>
#include <components/openmw-mp/Records/DynamicRecordFingerprint.hpp>
#include <components/openmw-mp/Sha256.hpp>

#include <apps/openmw-server/DynamicRecordService.hpp>
#include <apps/openmw-server/EnchantingService.hpp>
#include <apps/openmw-server/PlayerDatabase.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwmechanics/enchanting.hpp>

TEST(EnchantingNative, EmptyTargetPreviewIsSafe)
{
    MWMechanics::Enchanting enchanting;
    EXPECT_TRUE(enchanting.itemEmpty());
    EXPECT_EQ(enchanting.getMaxEnchantValue(), 0);
}

namespace
{
    struct TemporaryEnchantingDatabase
    {
        std::filesystem::path path = std::filesystem::temp_directory_path()
            / ("openmw-enchanting-service-test-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".db");
        ~TemporaryEnchantingDatabase()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
            std::filesystem::remove(path.string() + "-wal", error);
            std::filesystem::remove(path.string() + "-shm", error);
        }
    };

    // ---------------------------------------------------------------------
    // Content fixture: an in-memory ESMStore with the records authoritative
    // enchanting consumes.
    // ---------------------------------------------------------------------
    struct ContentFixture
    {
        MWWorld::ESMStore store;

        ContentFixture()
        {
            auto gmst = [&](std::string_view id, float value) {
                ESM::GameSetting setting;
                setting.blank();
                setting.mId = ESM::RefId::stringRefId(id);
                setting.mValue.setType(ESM::VT_Float);
                setting.mValue.setFloat(value);
                store.insertStatic(setting);
            };
            gmst("fEffectCostMult", 1.f);
            gmst("fEnchantmentConstantDurationMult", 1.f);
            gmst("fEnchantmentMult", 1.f);
            gmst("fEnchantmentChanceMult", 1.f);
            gmst("fEnchantmentConstantChanceMult", 1.f);
            gmst("fEnchantmentValueMult", 1.f);
            gmst("iSoulAmountForConstantEffect", 400.f);
            gmst("fFatigueBase", 1.f);
            gmst("fFatigueMult", 0.f);
            gmst("fDispRaceMod", 0.f);
            gmst("fDispPersonalityMult", 0.f);
            gmst("fDispPersonalityBase", 0.f);
            gmst("fDispCrimeMod", 0.f);
            gmst("fMiscSkillBonus", 1.f);
            gmst("fMajorSkillBonus", 1.5f);
            gmst("fMinorSkillBonus", 1.25f);
            gmst("fSpecialSkillBonus", 1.5f);
            gmst("iLevelUpMajorMult", 2.f);
            gmst("iLevelUpMinorMult", 1.f);

            auto magicEffect = [&](ESM::RefId id, float baseCost, std::int32_t flags) {
                ESM::MagicEffect effect;
                effect.blank();
                effect.mId = id;
                effect.mData.mBaseCost = baseCost;
                effect.mData.mFlags = flags;
                store.insertStatic(effect);
            };
            constexpr std::int32_t EnchantingFlags = ESM::MagicEffect::AllowEnchanting | ESM::MagicEffect::CastSelf
                | ESM::MagicEffect::CastTouch | ESM::MagicEffect::CastTarget;
            magicEffect(ESM::MagicEffect::FireDamage, 1.f, EnchantingFlags);
            magicEffect(ESM::MagicEffect::FrostDamage, 2.f, EnchantingFlags);
            magicEffect(ESM::MagicEffect::Soultrap, 1.f, 0); // no AllowEnchanting
            magicEffect(ESM::MagicEffect::DrainSkill, 1.f,
                ESM::MagicEffect::AllowEnchanting | ESM::MagicEffect::CastSelf | ESM::MagicEffect::TargetSkill);

            auto skill = [&](ESM::SkillId id, int specialization, float useValue) {
                ESM::Skill record;
                record.blank();
                record.mId = id;
                record.mData.mSpecialization = specialization;
                record.mData.mUseValue[ESM::Skill::Enchant_CreateMagicItem] = useValue;
                store.insertStatic(record);
            };
            skill(ESM::Skill::Enchant, ESM::Class::Magic, 1.f);
            skill(ESM::Skill::Mercantile, ESM::Class::Stealth, 1.f);
            skill(ESM::Skill::LongBlade, ESM::Class::Combat, 1.f);

            ESM::Weapon dagger;
            dagger.blank();
            dagger.mId = ESM::RefId::stringRefId("iron_dagger");
            dagger.mName = "Iron Dagger";
            dagger.mData.mType = ESM::Weapon::LongBladeOneHand;
            dagger.mData.mEnchant = 500;
            dagger.mData.mWeight = 1.f;
            dagger.mData.mValue = 5;
            dagger.mData.mHealth = 200;
            dagger.mData.mSpeed = 1.f;
            dagger.mData.mReach = 1.f;
            dagger.mData.mChop = { 1, 4 };
            dagger.mData.mSlash = { 1, 5 };
            dagger.mData.mThrust = { 1, 3 };
            dagger.mData.mFlags = 0;
            store.insertStatic(dagger);

            ESM::Weapon arrow;
            arrow.blank();
            arrow.mId = ESM::RefId::stringRefId("iron_arrow");
            arrow.mName = "Iron Arrow";
            arrow.mData.mType = ESM::Weapon::Arrow;
            arrow.mData.mEnchant = 500;
            arrow.mData.mWeight = 0.05f;
            arrow.mData.mValue = 1;
            store.insertStatic(arrow);

            ESM::Armor cuirass;
            cuirass.blank();
            cuirass.mId = ESM::RefId::stringRefId("iron_cuirass");
            cuirass.mName = "Iron Cuirass";
            cuirass.mData.mType = ESM::Armor::Cuirass;
            cuirass.mData.mEnchant = 500;
            cuirass.mData.mWeight = 5.f;
            cuirass.mData.mValue = 50;
            store.insertStatic(cuirass);

            ESM::Clothing robe;
            robe.blank();
            robe.mId = ESM::RefId::stringRefId("common_robe");
            robe.mName = "Common Robe";
            robe.mData.mType = ESM::Clothing::Robe;
            robe.mData.mEnchant = 500;
            robe.mData.mWeight = 1.f;
            robe.mData.mValue = 10;
            store.insertStatic(robe);

            ESM::Book scroll;
            scroll.blank();
            scroll.mId = ESM::RefId::stringRefId("scroll_ivory");
            scroll.mName = "Scroll of Ivory";
            scroll.mData.mIsScroll = 1;
            scroll.mData.mEnchant = 500;
            scroll.mData.mWeight = 0.1f;
            scroll.mData.mValue = 20;
            store.insertStatic(scroll);

            ESM::Book tome;
            tome.blank();
            tome.mId = ESM::RefId::stringRefId("tome_ordinary");
            tome.mName = "Ordinary Tome";
            tome.mData.mIsScroll = 0;
            tome.mData.mEnchant = 500;
            store.insertStatic(tome);

            auto gem = [&](const char* id) {
                ESM::Miscellaneous misc;
                misc.blank();
                misc.mId = ESM::RefId::stringRefId(id);
                misc.mName = id;
                misc.mData.mWeight = 0.1f;
                misc.mData.mValue = 10;
                store.insertStatic(misc);
            };
            gem("Misc_SoulGem_Common");
            gem("Misc_SoulGem_Grand");
            gem("Misc_SoulGem_Azura");

            auto creature = [&](const char* id, int soul) {
                ESM::Creature record;
                record.blank();
                record.mId = ESM::RefId::stringRefId(id);
                record.mName = id;
                record.mData.mSoul = soul;
                store.insertStatic(record);
            };
            creature("soul_rat", 100);
            creature("soul_ancestorghost", 400);

            auto npc = [&](const char* id, int services) {
                ESM::NPC record;
                record.blank();
                record.mId = ESM::RefId::stringRefId(id);
                record.mName = id;
                record.mRace = ESM::RefId::stringRefId("breton");
                record.mClass = ESM::RefId::stringRefId("enchant_class");
                record.mNpdt.mLevel = 5;
                record.mNpdt.mAttributes.fill(50);
                record.mNpdt.mSkills.fill(50);
                record.mNpdt.mDisposition = 50;
                record.mNpdt.mGold = 500;
                record.mAiData.mServices = services;
                store.insertStatic(record);
            };
            npc("enchanter_npc", ESM::NPC::Enchanting);
            npc("no_service_npc", 0);

            ESM::Class charClass;
            charClass.blank();
            charClass.mId = ESM::RefId::stringRefId("enchant_class");
            charClass.mData.mServices = ESM::NPC::Enchanting;
            store.insertStatic(charClass);
        }
    };

    mwmp::BasePlayer makePlayer()
    {
        mwmp::BasePlayer player;
        // Skill 90 + attributes 50/50 gives a chance >= 100 for cheap
        // enchantments, so success tests are deterministic.
        player.skills[ESM::Skill::refIdToIndex(ESM::Skill::Enchant)].base = 90.f;
        player.attributes[ESM::Attribute::refIdToIndex(ESM::Attribute::Intelligence)].base = 50;
        player.attributes[ESM::Attribute::refIdToIndex(ESM::Attribute::Luck)].base = 50;
        player.dynamicStats.fatigue.base = 1.f;
        player.dynamicStats.fatigue.current = 1.f;
        player.level = 1;
        player.levelProgress = 0.f;
        player.charClass.mName = "Test Class";
        player.charClass.mData.mSpecialization = ESM::Class::Magic;
        const int enchantIndex = ESM::Skill::refIdToIndex(ESM::Skill::Enchant);
        player.charClass.mData.mSkills[0][1] = enchantIndex; // major
        return player;
    }

    std::vector<mwmp::Item> makeInventory()
    {
        std::vector<mwmp::Item> items;
        mwmp::Item dagger;
        dagger.instanceId = 101;
        dagger.refId = "iron_dagger";
        dagger.count = 1;
        dagger.charge = 200;
        dagger.enchantmentCharge = -1.f;
        items.push_back(dagger);

        mwmp::Item gem;
        gem.instanceId = 102;
        gem.refId = "Misc_SoulGem_Common";
        gem.count = 1;
        gem.charge = -1;
        gem.enchantmentCharge = -1.f;
        gem.soul = "soul_rat";
        items.push_back(gem);

        mwmp::Item gold;
        gold.instanceId = 103;
        gold.refId = "gold_001";
        gold.count = 1000;
        gold.charge = -1;
        gold.enchantmentCharge = -1.f;
        items.push_back(gold);
        return items;
    }

    // ---------------------------------------------------------------------
    // Runtime registry mirroring the server's in-memory dynamic record store,
    // including fingerprint-based equivalent lookup.
    // ---------------------------------------------------------------------
    struct RuntimeRegistry
    {
        std::unordered_map<std::string, mwmp::DynamicRecordService::CommittedRecord> records;
        std::unordered_map<std::string, std::string> byFingerprint; // type:fingerprint -> recordId
        std::uint64_t nextId = 1;
        std::uint64_t sequence = 1;

        std::string allocate(mwmp::records::RecordType type)
        {
            return "$custom_" + std::string(mwmp::records::getRecordTypeName(type)) + "_" + std::to_string(nextId++);
        }

        void absorb(const mwmp::EnchantingService::Outcome& outcome)
        {
            for (const auto& record : outcome.newRecords)
            {
                records[record.recordType + ":" + record.recordId] = record;
                byFingerprint[record.recordType + ":" + mwmp::records::fingerprint(
                                                                    mwmp::records::decodeDefinition(record.definition))]
                    = record.recordId;
            }
        }
    };

    struct Fixture
    {
        TemporaryEnchantingDatabase temporary;
        ContentFixture content;
        RuntimeRegistry registry;
        mwmp::PlayerDatabase database;
        int64_t account = 0;
        int64_t character = 0;

        Fixture()
            : database(temporary.path.string())
        {
            account = database.createAccount("enchanting-author");
            character = database.createCharacter(account, "Enchanting Crafter").characterId;
        }

        mwmp::EnchantingService::Context context(mwmp::BasePlayer& player, std::vector<mwmp::Item>& inventory,
            std::uint64_t inventoryRevision,
            std::function<std::optional<mwmp::EnchantingService::Context::EnchanterInfo>(std::uint64_t)> resolveEnchanter
            = {})
        {
            mwmp::EnchantingService::Context context;
            context.accountId = account;
            context.characterId = character;
            context.inventoryRevision = inventoryRevision;
            context.player = &player;
            context.inventory = &inventory;
            context.store = &content.store;
            context.creationSource = "enchanting";
            context.recordScope = "generated";
            context.persistent = true;
            context.validationVersion = 1;
            context.projectilesEnchantMultiplier = 0.f;
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
                const std::string key
                    = std::string(mwmp::records::getRecordTypeName(type)) + ":" + std::string(fingerprint);
                const auto it = registry.byFingerprint.find(key);
                if (it == registry.byFingerprint.end())
                    return std::nullopt;
                const auto stored = recordsEntry(type, it->second);
                if (!stored)
                    return std::nullopt;
                return mwmp::DynamicRecordService::CatalogRecord{
                    stored->recordType, stored->recordId, std::string(fingerprint), stored->definition };
            };
            context.allocateId = [&](mwmp::records::RecordType type) { return registry.allocate(type); };
            context.nextCommitSequence = [&]() { return registry.sequence++; };
            context.resolveEnchanter = std::move(resolveEnchanter);
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

        std::optional<mwmp::DynamicRecordService::CommittedRecord> recordsEntry(
            mwmp::records::RecordType type, const std::string& recordId)
        {
            const auto it = registry.records.find(
                std::string(mwmp::records::getRecordTypeName(type)) + ":" + recordId);
            if (it == registry.records.end())
                return std::nullopt;
            return it->second;
        }

        std::string hashOf(const mwmp::records::EnchantingRequest& request)
        {
            mwmp::PacketEnchantingRequest packet;
            packet.request = request;
            const std::vector<uint8_t> bytes = packet.encode();
            return mwmp::crypto::sha256hex(
                std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        }

        mwmp::records::EnchantingRequest request(std::string_view requestId)
        {
            mwmp::records::EnchantingRequest request;
            request.protocolVersion = mwmp::records::CurrentEnchantingProtocolVersion;
            request.requestId = std::string(requestId);
            request.targetInstanceId = 101;
            request.soulGemInstanceId = 102;
            request.castStyle = ESM::Enchantment::WhenStrikes;
            request.itemName = "Dagger of Testing";
            request.selfEnchanting = true;
            mwmp::records::EnchantingEffectChoice choice;
            choice.effectId = "FireDamage";
            choice.range = ESM::RT_Touch;
            choice.magnitudeMin = 10;
            choice.magnitudeMax = 20;
            choice.duration = 5;
            choice.area = 0;
            request.effects.push_back(choice);
            return request;
        }

        mwmp::EnchantingService::Context::EnchanterInfo enchanter(const char* refId, bool cellLoaded = true)
        {
            mwmp::EnchantingService::Context::EnchanterInfo info;
            info.refId = refId;
            mwmp::DynamicStats stats;
            stats.fatigue.base = 1.f;
            stats.fatigue.current = 1.f;
            info.dynamicStats = stats;
            info.cellLoaded = cellLoaded;
            return info;
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

    std::optional<mwmp::records::Enchantment> asEnchantment(
        const std::vector<mwmp::DynamicRecordService::CommittedRecord>& records, const std::string& recordId)
    {
        for (const auto& record : records)
        {
            if (record.recordId != recordId)
                continue;
            const auto definition = mwmp::records::decodeDefinition(record.definition);
            const auto* enchantment = std::get_if<mwmp::records::Enchantment>(&definition.data);
            if (enchantment == nullptr)
                return std::nullopt;
            return *enchantment;
        }
        return std::nullopt;
    }
}

TEST(EnchantingService, SuccessfulSelfEnchantCommitsRecordPairAtomically)
{
    Fixture fixture;
    mwmp::EnchantingService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    auto request = fixture.request("enchant-1");
    auto outcome = service.execute(request, fixture.hashOf(request), fixture.context(player, inventory, 0));
    fixture.registry.absorb(outcome);

    ASSERT_TRUE(outcome.result.accepted) << "error=" << static_cast<int>(outcome.result.error);
    EXPECT_TRUE(outcome.committed);
    EXPECT_TRUE(outcome.result.success);
    EXPECT_TRUE(outcome.result.enchantmentRecordId.starts_with("$custom_enchantment_"));
    EXPECT_TRUE(outcome.result.itemRecordId.starts_with("$custom_weapon_"));
    EXPECT_FALSE(outcome.result.enchantmentReused);
    EXPECT_FALSE(outcome.result.itemReused);
    EXPECT_EQ(outcome.result.inventoryRevision, 1u);
    EXPECT_EQ(outcome.resultingInventoryRevision, 1u);

    // The record pair is committed dependency-first: enchantment, then item.
    ASSERT_EQ(outcome.newRecords.size(), 2u);
    EXPECT_EQ(outcome.newRecords[0].recordType, "enchantment");
    EXPECT_EQ(outcome.newRecords[1].recordType, "weapon");

    // The item definition references the canonical enchantment id.
    const auto itemDefinition = mwmp::records::decodeDefinition(outcome.newRecords[1].definition);
    const auto* weapon = std::get_if<mwmp::records::Weapon>(&itemDefinition.data);
    ASSERT_NE(weapon, nullptr);
    EXPECT_EQ(weapon->enchantment.kind, mwmp::records::ReferenceKind::ContentId);
    EXPECT_EQ(weapon->enchantment.value, outcome.result.enchantmentRecordId);
    EXPECT_EQ(weapon->item.name, "Dagger of Testing");
    // The item carries the full gem charge, the enchantment record the
    // charge divided by the enchanted count.
    EXPECT_EQ(weapon->enchantCapacity, 100);
    EXPECT_NE(weapon->flags & ESM::Weapon::Magical, 0);
    const auto enchantment = asEnchantment(outcome.newRecords, outcome.result.enchantmentRecordId);
    ASSERT_TRUE(enchantment.has_value());
    EXPECT_EQ(enchantment->type, ESM::Enchantment::WhenStrikes);
    EXPECT_EQ(enchantment->charge, 100);
    ASSERT_EQ(enchantment->effects.size(), 1u);
    EXPECT_EQ(enchantment->effects[0].effectId, "firedamage");
    EXPECT_EQ(enchantment->effects[0].magnitudeMin, 10);
    EXPECT_EQ(enchantment->effects[0].magnitudeMax, 20);
    EXPECT_EQ(enchantment->effects[0].range, ESM::RT_Touch);

    // Exact consumption: one soul gem and one target item gone; the
    // enchanted item granted; gold untouched.
    const auto& items = outcome.resultingInventory;
    EXPECT_EQ(findItem(items, 101).count, 0);
    EXPECT_EQ(findItem(items, 102).count, 0);
    EXPECT_EQ(totalCount(items, outcome.result.itemRecordId), 1);
    EXPECT_EQ(totalCount(items, "gold_001"), 1000);
    // The granted stack received a stable instance identity.
    const auto granted = std::find_if(items.begin(), items.end(),
        [&](const mwmp::Item& item) { return item.refId == outcome.result.itemRecordId; });
    ASSERT_NE(granted, items.end());
    EXPECT_NE(granted->instanceId, 0u);

    // Skill progression awarded exactly once for the successful self-enchant.
    ASSERT_TRUE(outcome.resultingStats.has_value());
    const int index = ESM::Skill::refIdToIndex(ESM::Skill::Enchant);
    EXPECT_GT(outcome.resultingStats->skills[index].progress, 0.f);

    // Both records persisted; the journal is terminal; one revision.
    const auto records = fixture.database.loadDynamicRecords();
    ASSERT_EQ(records.size(), 2u);

    // The owning item must keep its generated Enchantment alive in the GC
    // graph. This is separate from commit ordering: without the persisted
    // record_dependency link, the Enchantment can be collected while the
    // weapon remains in inventory/equipment.
    const auto catalog = fixture.database.loadDynamicRecordCatalog();
    const auto enchantmentCatalog = std::find_if(catalog.begin(), catalog.end(), [&](const auto& entry) {
        return entry.recordId == outcome.result.enchantmentRecordId;
    });
    ASSERT_NE(enchantmentCatalog, catalog.end());
    EXPECT_GE(enchantmentCatalog->linkCount, 1);

    EXPECT_EQ(fixture.database.loadInventoryRevision(fixture.character), 1u);
    EXPECT_EQ(fixture.database.loadCraftRequest(fixture.account, fixture.character, "enchant-1")->status, "accepted");
}

TEST(EnchantingService, FailedSelfEnchantConsumesGemAndPreservesItem)
{
    Fixture fixture;
    mwmp::EnchantingService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    player.skills[ESM::Skill::refIdToIndex(ESM::Skill::Enchant)].base = 0.f;
    player.attributes[ESM::Attribute::refIdToIndex(ESM::Attribute::Intelligence)].base = 0;
    player.attributes[ESM::Attribute::refIdToIndex(ESM::Attribute::Luck)].base = 0;
    std::vector<mwmp::Item> inventory = makeInventory();

    auto request = fixture.request("enchant-fail");
    auto outcome = service.execute(request, fixture.hashOf(request), fixture.context(player, inventory, 0));
    fixture.registry.absorb(outcome);

    ASSERT_TRUE(outcome.result.accepted);
    EXPECT_TRUE(outcome.committed);
    EXPECT_FALSE(outcome.result.success);
    EXPECT_TRUE(outcome.result.enchantmentRecordId.empty());
    EXPECT_TRUE(outcome.result.itemRecordId.empty());
    EXPECT_TRUE(outcome.newRecords.empty());
    EXPECT_FALSE(outcome.resultingStats.has_value());
    // Native semantics: the soul gem is consumed on failure, the target is not.
    EXPECT_EQ(findItem(outcome.resultingInventory, 102).count, 0);
    EXPECT_EQ(findItem(outcome.resultingInventory, 101).count, 1);
    EXPECT_EQ(outcome.result.inventoryRevision, 1u);
    EXPECT_EQ(fixture.database.loadDynamicRecords().size(), 0u);
}

TEST(EnchantingService, AzuraStarIsReturnedOnAnyOutcome)
{
    Fixture fixture;
    mwmp::EnchantingService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();
    inventory[1].refId = "Misc_SoulGem_Azura";
    inventory[1].soul = "soul_ancestorghost";

    auto request = fixture.request("azura");
    auto outcome = service.execute(request, fixture.hashOf(request), fixture.context(player, inventory, 0));
    fixture.registry.absorb(outcome);
    ASSERT_TRUE(outcome.result.accepted);
    EXPECT_TRUE(outcome.result.success);
    // The consumed Azura instance is gone and a fresh star was granted.
    EXPECT_EQ(findItem(outcome.resultingInventory, 102).count, 0);
    EXPECT_EQ(totalCount(outcome.resultingInventory, "Misc_SoulGem_Azura"), 1);
}

TEST(EnchantingService, AmmoEnchantsTheWholeStack)
{
    Fixture fixture;
    mwmp::EnchantingService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();
    inventory[0].refId = "iron_arrow";
    inventory[0].count = 10;
    mwmp::Item gem;
    gem.instanceId = 102;
    gem.refId = "Misc_SoulGem_Grand";
    gem.count = 1;
    gem.soul = "soul_ancestorghost"; // 400 charge
    inventory[1] = gem;

    auto request = fixture.request("arrows");
    request.castStyle = ESM::Enchantment::WhenStrikes;
    auto context = fixture.context(player, inventory, 0);
    context.projectilesEnchantMultiplier = 2.f;
    auto outcome = service.execute(request, fixture.hashOf(request), context);
    fixture.registry.absorb(outcome);

    ASSERT_TRUE(outcome.result.accepted) << "error=" << static_cast<int>(outcome.result.error);
    ASSERT_TRUE(outcome.result.success);
    // count = clamp(int(400 * 2 / points), 1, 10). The whole stack is
    // consumed and re-granted as enchanted arrows.
    EXPECT_EQ(findItem(outcome.resultingInventory, 101).count, 0);
    EXPECT_EQ(totalCount(outcome.resultingInventory, outcome.result.itemRecordId), 10);
    const auto itemDefinition = mwmp::records::decodeDefinition(outcome.newRecords[1].definition);
    const auto* weapon = std::get_if<mwmp::records::Weapon>(&itemDefinition.data);
    ASSERT_NE(weapon, nullptr);
    EXPECT_EQ(weapon->enchantCapacity, 400); // full gem charge on the item
    const auto enchantment = asEnchantment(outcome.newRecords, outcome.result.enchantmentRecordId);
    ASSERT_TRUE(enchantment.has_value());
    EXPECT_EQ(enchantment->charge, 400 / 10);
}

TEST(EnchantingService, PaidEnchantAlwaysSucceedsAndDeductsGold)
{
    Fixture fixture;
    mwmp::EnchantingService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    auto request = fixture.request("paid-1");
    request.selfEnchanting = false;
    request.enchanterNetId = 7;
    auto context = fixture.context(player, inventory, 0, [&](std::uint64_t netId) {
        EXPECT_EQ(netId, 7u);
        return std::optional(fixture.enchanter("enchanter_npc"));
    });
    auto outcome = service.execute(request, fixture.hashOf(request), context);
    fixture.registry.absorb(outcome);

    ASSERT_TRUE(outcome.result.accepted) << "error=" << static_cast<int>(outcome.result.error);
    EXPECT_TRUE(outcome.result.success);
    EXPECT_FALSE(outcome.result.enchantmentRecordId.empty());
    // Paid enchanting always succeeds and never rolls: the player's Enchant
    // skill is irrelevant here.
    EXPECT_FALSE(outcome.resultingStats.has_value());
    // The deterministic barter price with the fixture NPC (all stats 50) and
    // player (mercantile 0, luck 50): base 7, pcTerm 5, npcTerm 65,
    // buyTerm 1.3 -> offer int(7*1.3) = 9.
    EXPECT_EQ(totalCount(outcome.resultingInventory, "gold_001"), 1000 - 9);
    EXPECT_EQ(findItem(outcome.resultingInventory, 102).count, 0);
}

TEST(EnchantingService, PaidEnchantInsufficientGoldRejects)
{
    Fixture fixture;
    mwmp::EnchantingService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();
    inventory[2].count = 5;

    auto request = fixture.request("paid-poor");
    request.selfEnchanting = false;
    request.enchanterNetId = 7;
    auto context = fixture.context(player, inventory, 0, [&](std::uint64_t) {
        return std::optional(fixture.enchanter("enchanter_npc"));
    });
    auto outcome = service.execute(request, fixture.hashOf(request), context);
    EXPECT_FALSE(outcome.result.accepted);
    EXPECT_EQ(outcome.result.error, mwmp::records::EnchantingError::InsufficientGold);
    EXPECT_FALSE(outcome.committed);
    EXPECT_EQ(fixture.database.loadInventoryRevision(fixture.character), 0u);
}

TEST(EnchantingService, PaidEnchantValidatesEnchanterIdentityAndService)
{
    Fixture fixture;
    mwmp::EnchantingService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    auto makePaid = [&](std::string_view requestId) {
        auto request = fixture.request(requestId);
        request.selfEnchanting = false;
        request.enchanterNetId = 7;
        return request;
    };

    // Unknown actor.
    auto unknown = makePaid("paid-unknown");
    auto outcome = service.execute(unknown, fixture.hashOf(unknown), fixture.context(player, inventory, 0, [&](std::uint64_t) {
        return std::optional<mwmp::EnchantingService::Context::EnchanterInfo>{};
    }));
    EXPECT_EQ(outcome.result.error, mwmp::records::EnchantingError::InvalidEnchanter);

    // Actor without the Enchanting service.
    auto noService = makePaid("paid-no-service");
    outcome = service.execute(noService, fixture.hashOf(noService),
        fixture.context(player, inventory, 0, [&](std::uint64_t) {
            return std::optional(fixture.enchanter("no_service_npc"));
        }));
    EXPECT_EQ(outcome.result.error, mwmp::records::EnchantingError::EnchanterUnavailable);

    // Actor in a cell the requester has not loaded.
    auto unloaded = makePaid("paid-unloaded");
    outcome = service.execute(unloaded, fixture.hashOf(unloaded),
        fixture.context(player, inventory, 0, [&](std::uint64_t) {
            return std::optional(fixture.enchanter("enchanter_npc", false));
        }));
    EXPECT_EQ(outcome.result.error, mwmp::records::EnchantingError::EnchanterUnavailable);
}

TEST(EnchantingService, StaleInventoryRevisionRejectsWithoutMutation)
{
    Fixture fixture;
    mwmp::EnchantingService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    auto request = fixture.request("stale");
    request.inventoryRevision = 7;
    auto outcome = service.execute(request, fixture.hashOf(request), fixture.context(player, inventory, 0));

    EXPECT_FALSE(outcome.result.accepted);
    EXPECT_EQ(outcome.result.error, mwmp::records::EnchantingError::StaleInventoryRevision);
    EXPECT_FALSE(outcome.committed);
    EXPECT_TRUE(fixture.database.loadDynamicRecords().empty());
    EXPECT_EQ(fixture.database.loadInventoryRevision(fixture.character), 0u);
    EXPECT_EQ(fixture.database.loadCraftRequest(fixture.account, fixture.character, "stale")->status, "rejected");
}

TEST(EnchantingService, MissingInstancesReject)
{
    Fixture fixture;
    mwmp::EnchantingService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    auto request = fixture.request("missing-target");
    request.targetInstanceId = 999;
    auto outcome = service.execute(request, fixture.hashOf(request), fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome.result.accepted);
    EXPECT_EQ(outcome.result.error, mwmp::records::EnchantingError::TargetItemNotFound);

    auto request2 = fixture.request("missing-gem");
    request2.soulGemInstanceId = 999;
    auto outcome2 = service.execute(request2, fixture.hashOf(request2), fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome2.result.accepted);
    EXPECT_EQ(outcome2.result.error, mwmp::records::EnchantingError::SoulGemNotFound);

    // A depleted stack is not owned.
    inventory[0].count = 0;
    auto request3 = fixture.request("depleted");
    auto outcome3 = service.execute(request3, fixture.hashOf(request3), fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome3.result.accepted);
    EXPECT_EQ(outcome3.result.error, mwmp::records::EnchantingError::TargetItemNotOwned);
}

TEST(EnchantingService, WrongItemTypesReject)
{
    Fixture fixture;
    mwmp::EnchantingService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    // A soul gem as the target.
    auto request = fixture.request("wrong-target");
    request.targetInstanceId = 102;
    request.soulGemInstanceId = 103; // gold is also not a gem
    auto outcome = service.execute(request, fixture.hashOf(request), fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome.result.accepted);
    EXPECT_EQ(outcome.result.error, mwmp::records::EnchantingError::InvalidTargetItem);

    // The target item as the soul gem (a different weapon instance).
    mwmp::Item secondDagger;
    secondDagger.instanceId = 104;
    secondDagger.refId = "iron_dagger";
    secondDagger.count = 1;
    secondDagger.charge = 200;
    inventory.push_back(secondDagger);
    auto request2 = fixture.request("wrong-gem");
    request2.soulGemInstanceId = 104;
    auto outcome2 = service.execute(request2, fixture.hashOf(request2), fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome2.result.accepted);
    EXPECT_EQ(outcome2.result.error, mwmp::records::EnchantingError::InvalidSoulGem);

    // A non-scroll book cannot be enchanted.
    inventory[0].refId = "tome_ordinary";
    auto request3 = fixture.request("tome");
    auto outcome3 = service.execute(request3, fixture.hashOf(request3), fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome3.result.accepted);
    EXPECT_EQ(outcome3.result.error, mwmp::records::EnchantingError::InvalidTargetItem);

    // A scroll can.
    inventory[0].refId = "scroll_ivory";
    auto request4 = fixture.request("scroll");
    request4.castStyle = ESM::Enchantment::CastOnce;
    auto outcome4 = service.execute(request4, fixture.hashOf(request4), fixture.context(player, inventory, 0));
    fixture.registry.absorb(outcome4);
    ASSERT_TRUE(outcome4.result.accepted) << "error=" << static_cast<int>(outcome4.result.error);
    ASSERT_TRUE(outcome4.result.success);
    ASSERT_EQ(outcome4.newRecords.size(), 2u);
    EXPECT_EQ(outcome4.newRecords[1].recordType, "book");
    const auto bookDefinition = mwmp::records::decodeDefinition(outcome4.newRecords[1].definition);
    const auto* book = std::get_if<mwmp::records::Book>(&bookDefinition.data);
    ASSERT_NE(book, nullptr);
    EXPECT_TRUE(book->isScroll);
    EXPECT_EQ(book->enchantment.value, outcome4.result.enchantmentRecordId);
}

TEST(EnchantingService, SoulValidationRejects)
{
    Fixture fixture;
    mwmp::EnchantingService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    // Empty soul.
    inventory[1].soul.clear();
    auto request = fixture.request("empty-soul");
    auto outcome = service.execute(request, fixture.hashOf(request), fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome.result.accepted);
    EXPECT_EQ(outcome.result.error, mwmp::records::EnchantingError::EmptySoul);

    // Soul that is not a known creature.
    inventory[1].soul = "not_a_creature";
    auto request2 = fixture.request("bad-soul");
    auto outcome2 = service.execute(request2, fixture.hashOf(request2), fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome2.result.accepted);
    EXPECT_EQ(outcome2.result.error, mwmp::records::EnchantingError::InvalidSoul);
}

TEST(EnchantingService, DuplicateInstanceRejects)
{
    Fixture fixture;
    mwmp::EnchantingService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    auto request = fixture.request("duplicate");
    request.soulGemInstanceId = 101;
    auto outcome = service.execute(request, fixture.hashOf(request), fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome.result.accepted);
    EXPECT_EQ(outcome.result.error, mwmp::records::EnchantingError::DuplicateSourceInstance);
}

TEST(EnchantingService, CapacityExceededRejects)
{
    Fixture fixture;
    mwmp::EnchantingService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    // A huge enchantment on a tiny capacity item.
    ESM::Weapon dagger;
    dagger.blank();
    dagger.mId = ESM::RefId::stringRefId("weak_dagger");
    dagger.mName = "Weak Dagger";
    dagger.mData.mType = ESM::Weapon::LongBladeOneHand;
    dagger.mData.mEnchant = 5; // tiny capacity
    fixture.content.store.insertStatic(dagger);
    inventory[0].refId = "weak_dagger";

    auto request = fixture.request("capacity");
    request.effects[0].magnitudeMin = 100;
    request.effects[0].magnitudeMax = 100;
    request.effects[0].duration = 100;
    auto outcome = service.execute(request, fixture.hashOf(request), fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome.result.accepted);
    EXPECT_EQ(outcome.result.error, mwmp::records::EnchantingError::CapacityExceeded);
}

TEST(EnchantingService, InvalidEffectsReject)
{
    Fixture fixture;
    mwmp::EnchantingService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    // Unknown effect id.
    auto request = fixture.request("unknown-effect");
    request.effects[0].effectId = "NotARealEffect";
    auto outcome = service.execute(request, fixture.hashOf(request), fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome.result.accepted);
    EXPECT_EQ(outcome.result.error, mwmp::records::EnchantingError::InvalidEffect);

    // Effect without the AllowEnchanting flag.
    auto request2 = fixture.request("not-allowed");
    request2.effects[0].effectId = "Soultrap";
    auto outcome2 = service.execute(request2, fixture.hashOf(request2), fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome2.result.accepted);
    EXPECT_EQ(outcome2.result.error, mwmp::records::EnchantingError::EffectNotAllowed);

    // Range the effect does not support (FireDamage has CastSelf but the
    // constant-effect mode restricts everything to Self).
    auto request3 = fixture.request("bad-range");
    request3.castStyle = ESM::Enchantment::ConstantEffect;
    request3.effects[0].range = ESM::RT_Touch;
    auto outcome3 = service.execute(request3, fixture.hashOf(request3), fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome3.result.accepted);
    EXPECT_EQ(outcome3.result.error, mwmp::records::EnchantingError::InvalidEffect);

    // min magnitude above max.
    auto request4 = fixture.request("bad-magnitude");
    request4.effects[0].magnitudeMin = 20;
    request4.effects[0].magnitudeMax = 10;
    auto outcome4 = service.execute(request4, fixture.hashOf(request4), fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome4.result.accepted);
    EXPECT_EQ(outcome4.result.error, mwmp::records::EnchantingError::InvalidMagnitude);

    // A TargetSkill effect without a skill target.
    auto request5 = fixture.request("missing-skill");
    request5.effects[0].effectId = "DrainSkill";
    request5.effects[0].skillId.clear();
    auto outcome5 = service.execute(request5, fixture.hashOf(request5), fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome5.result.accepted);
    EXPECT_EQ(outcome5.result.error, mwmp::records::EnchantingError::InvalidEffect);

    // A TargetSkill effect with an unknown skill.
    auto request6 = fixture.request("unknown-skill");
    request6.effects[0].effectId = "DrainSkill";
    request6.effects[0].skillId = "not_a_skill";
    auto outcome6 = service.execute(request6, fixture.hashOf(request6), fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome6.result.accepted);
    EXPECT_EQ(outcome6.result.error, mwmp::records::EnchantingError::InvalidEffect);

    // A valid TargetSkill effect is accepted and preserved.
    auto request7 = fixture.request("valid-skill");
    request7.effects[0].effectId = "DrainSkill";
    request7.effects[0].skillId = "longblade";
    request7.effects[0].range = ESM::RT_Self;
    auto outcome7 = service.execute(request7, fixture.hashOf(request7), fixture.context(player, inventory, 0));
    fixture.registry.absorb(outcome7);
    ASSERT_TRUE(outcome7.result.accepted) << "error=" << static_cast<int>(outcome7.result.error);
    const auto enchantment = asEnchantment(outcome7.newRecords, outcome7.result.enchantmentRecordId);
    ASSERT_TRUE(enchantment.has_value());
    ASSERT_EQ(enchantment->effects.size(), 1u);
    EXPECT_EQ(enchantment->effects[0].skillId, "longblade");
}

TEST(EnchantingService, InvalidCastStyleRejects)
{
    Fixture fixture;
    mwmp::EnchantingService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    // CastOnce is unreachable for weapons.
    auto request = fixture.request("cast-once-weapon");
    request.castStyle = ESM::Enchantment::CastOnce;
    auto outcome = service.execute(request, fixture.hashOf(request), fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome.result.accepted);
    EXPECT_EQ(outcome.result.error, mwmp::records::EnchantingError::InvalidCastStyle);

    // ConstantEffect requires a powerful soul (>= iSoulAmountForConstantEffect).
    auto request2 = fixture.request("weak-soul-constant");
    request2.castStyle = ESM::Enchantment::ConstantEffect;
    request2.effects[0].range = ESM::RT_Self;
    auto outcome2 = service.execute(request2, fixture.hashOf(request2), fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome2.result.accepted);
    EXPECT_EQ(outcome2.result.error, mwmp::records::EnchantingError::InvalidCastStyle);

    // A constant effect with a powerful soul is accepted.
    auto request3 = fixture.request("constant");
    request3.castStyle = ESM::Enchantment::ConstantEffect;
    request3.effects[0].range = ESM::RT_Self;
    inventory[1].soul = "soul_ancestorghost";
    auto outcome3 = service.execute(request3, fixture.hashOf(request3), fixture.context(player, inventory, 0));
    fixture.registry.absorb(outcome3);
    ASSERT_TRUE(outcome3.result.accepted) << "error=" << static_cast<int>(outcome3.result.error);
    ASSERT_TRUE(outcome3.result.success);
    const auto enchantment = asEnchantment(outcome3.newRecords, outcome3.result.enchantmentRecordId);
    ASSERT_TRUE(enchantment.has_value());
    EXPECT_EQ(enchantment->type, ESM::Enchantment::ConstantEffect);
    EXPECT_EQ(enchantment->cost, 0);
    EXPECT_EQ(enchantment->charge, 0);
}

TEST(EnchantingService, UnknownTargetRefIdIsContentMismatch)
{
    Fixture fixture;
    mwmp::EnchantingService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();
    inventory[0].refId = "totally_unknown_item";

    auto request = fixture.request("unknown-ref");
    auto context = fixture.context(player, inventory, 0);
    context.isContentIdAllowed = [](std::string_view) { return false; };
    auto outcome = service.execute(request, fixture.hashOf(request), context);
    EXPECT_FALSE(outcome.result.accepted);
    EXPECT_EQ(outcome.result.error, mwmp::records::EnchantingError::ContentMismatch);
}

TEST(EnchantingService, IdenticalRequestsDeduplicateBothRecords)
{
    Fixture fixture;
    mwmp::EnchantingService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    auto first = fixture.request("dedup-1");
    auto outcome1 = service.execute(first, fixture.hashOf(first), fixture.context(player, inventory, 0));
    fixture.registry.absorb(outcome1);
    ASSERT_TRUE(outcome1.result.accepted);
    const std::string enchantmentId = outcome1.result.enchantmentRecordId;
    const std::string itemId = outcome1.result.itemRecordId;
    EXPECT_EQ(enchantmentId, "$custom_enchantment_1");
    // Record ids are allocated per type; the weapon is the second record.
    EXPECT_EQ(itemId, "$custom_weapon_2");

    // A second identical request reuses both canonical records. The target
    // and gem are rebuilt because the first commit consumed them.
    auto second = fixture.request("dedup-2");
    second.inventoryRevision = 1;
    std::vector<mwmp::Item> inventory2 = makeInventory();
    inventory2[1].soul = "soul_rat";
    auto outcome2 = service.execute(second, fixture.hashOf(second), fixture.context(player, inventory2, 1));
    fixture.registry.absorb(outcome2);
    ASSERT_TRUE(outcome2.result.accepted);
    ASSERT_TRUE(outcome2.result.success);
    EXPECT_EQ(outcome2.result.enchantmentRecordId, enchantmentId);
    EXPECT_EQ(outcome2.result.itemRecordId, itemId);
    EXPECT_TRUE(outcome2.result.enchantmentReused);
    EXPECT_TRUE(outcome2.result.itemReused);
    EXPECT_TRUE(outcome2.newRecords.empty());
    EXPECT_EQ(fixture.database.loadDynamicRecords().size(), 2u);
}

TEST(EnchantingService, ItemIdentityDependsOnNameAndEnchantment)
{
    Fixture fixture;
    mwmp::EnchantingService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    auto first = fixture.request("distinct-1");
    auto outcome1 = service.execute(first, fixture.hashOf(first), fixture.context(player, inventory, 0));
    fixture.registry.absorb(outcome1);
    ASSERT_TRUE(outcome1.result.accepted);

    // A different custom name yields a different item record while the
    // enchantment itself is reused.
    auto second = fixture.request("distinct-2");
    second.inventoryRevision = 1;
    second.itemName = "Dagger of Other Testing";
    std::vector<mwmp::Item> inventory2 = makeInventory();
    inventory2[1].soul = "soul_rat";
    auto outcome2 = service.execute(second, fixture.hashOf(second), fixture.context(player, inventory2, 1));
    fixture.registry.absorb(outcome2);
    ASSERT_TRUE(outcome2.result.accepted);
    ASSERT_TRUE(outcome2.result.success);
    EXPECT_EQ(outcome2.result.enchantmentRecordId, outcome1.result.enchantmentRecordId);
    EXPECT_TRUE(outcome2.result.enchantmentReused);
    EXPECT_NE(outcome2.result.itemRecordId, outcome1.result.itemRecordId);
    EXPECT_FALSE(outcome2.result.itemReused);
    EXPECT_EQ(fixture.database.loadDynamicRecords().size(), 3u);
}

TEST(EnchantingService, RetryReplaysAcceptedRequestWithoutDoubleConsumption)
{
    Fixture fixture;
    mwmp::EnchantingService service(fixture.database);
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
    EXPECT_EQ(fixture.database.loadInventoryRevision(fixture.character), 1u);
    EXPECT_EQ(fixture.database.loadDynamicRecords().size(), 2u);
}

TEST(EnchantingService, RetryReplaysRejectedRequest)
{
    Fixture fixture;
    mwmp::EnchantingService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    auto request = fixture.request("retry-rejected");
    request.soulGemInstanceId = 999;
    const std::string hash = fixture.hashOf(request);
    auto first = service.execute(request, hash, fixture.context(player, inventory, 0));
    EXPECT_FALSE(first.result.accepted);
    EXPECT_EQ(first.result.error, mwmp::records::EnchantingError::SoulGemNotFound);

    auto retry = service.execute(request, hash, fixture.context(player, inventory, 0));
    EXPECT_FALSE(retry.result.accepted);
    EXPECT_TRUE(retry.replayed);
    EXPECT_EQ(retry.result, first.result);
}

TEST(EnchantingService, DuplicateRequestIdWithDifferentPayloadConflicts)
{
    Fixture fixture;
    mwmp::EnchantingService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    auto request = fixture.request("conflict");
    const std::string hash = fixture.hashOf(request);
    auto first = service.execute(request, hash, fixture.context(player, inventory, 0));
    fixture.registry.absorb(first);
    ASSERT_TRUE(first.result.accepted);

    auto different = fixture.request("conflict");
    different.itemName = "A Different Name";
    auto outcome = service.execute(different, fixture.hashOf(different), fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome.result.accepted);
    EXPECT_EQ(outcome.result.error, mwmp::records::EnchantingError::DuplicateRequestConflict);
}

TEST(EnchantingService, RestartReplaysTerminalResult)
{
    Fixture fixture;
    {
        mwmp::EnchantingService service(fixture.database);
        mwmp::BasePlayer player = makePlayer();
        std::vector<mwmp::Item> inventory = makeInventory();

        auto request = fixture.request("restart");
        const std::string hash = fixture.hashOf(request);
        auto first = service.execute(request, hash, fixture.context(player, inventory, 0));
        fixture.registry.absorb(first);
        ASSERT_TRUE(first.result.accepted);
    }

    // A fresh database handle (server restart) replays the exact result.
    mwmp::PlayerDatabase reopened(fixture.temporary.path.string());
    mwmp::EnchantingService service(reopened);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();
    auto request = fixture.request("restart");
    const std::string hash = fixture.hashOf(request);
    auto replay = service.execute(request, hash, fixture.context(player, inventory, 0));
    EXPECT_TRUE(replay.result.accepted);
    EXPECT_TRUE(replay.replayed);
    EXPECT_EQ(replay.result.inventoryRevision, 1u);
    EXPECT_FALSE(replay.committed);
    EXPECT_EQ(reopened.loadInventoryRevision(fixture.character), 1u);
    EXPECT_EQ(reopened.loadDynamicRecords().size(), 2u);
}

TEST(EnchantingService, QuotaExceededRejectsWithoutMutation)
{
    Fixture fixture;
    mwmp::EnchantingService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    auto request = fixture.request("quota");
    auto context = fixture.context(player, inventory, 0);
    context.maximumNewRecords = 0;
    auto outcome = service.execute(request, fixture.hashOf(request), context);

    EXPECT_FALSE(outcome.result.accepted);
    EXPECT_EQ(outcome.result.error, mwmp::records::EnchantingError::QuotaExceeded);
    EXPECT_FALSE(outcome.committed);
    // Nothing was consumed or persisted.
    EXPECT_TRUE(fixture.database.loadDynamicRecords().empty());
    EXPECT_EQ(fixture.database.loadInventoryRevision(fixture.character), 0u);
    EXPECT_EQ(fixture.database.loadCraftRequest(fixture.account, fixture.character, "quota")->status, "rejected");
}

TEST(EnchantingService, EnchantSkillLevelUpAdvancesBaseAndLevelProgress)
{
    Fixture fixture;
    mwmp::EnchantingService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    const int index = ESM::Skill::refIdToIndex(ESM::Skill::Enchant);
    player.skills[index].base = 10.f;
    player.skills[index].progress = 0.99f;
    player.attributes[ESM::Attribute::refIdToIndex(ESM::Attribute::Intelligence)].base = 500;
    player.attributes[ESM::Attribute::refIdToIndex(ESM::Attribute::Luck)].base = 500;
    std::vector<mwmp::Item> inventory = makeInventory();

    auto request = fixture.request("levelup");
    auto outcome = service.execute(request, fixture.hashOf(request), fixture.context(player, inventory, 0));
    fixture.registry.absorb(outcome);
    ASSERT_TRUE(outcome.result.accepted);
    ASSERT_TRUE(outcome.result.success);
    ASSERT_TRUE(outcome.resultingStats.has_value());
    EXPECT_EQ(outcome.resultingStats->skills[index].base, 11.f);
    EXPECT_FLOAT_EQ(outcome.resultingStats->skills[index].progress, 0.f);
    EXPECT_FLOAT_EQ(outcome.resultingStats->levelProgress, 2.f);
}

TEST(EnchantingService, MalformedEarlyRequestsRejectCleanly)
{
    Fixture fixture;
    mwmp::EnchantingService service(fixture.database);
    mwmp::BasePlayer player = makePlayer();
    std::vector<mwmp::Item> inventory = makeInventory();

    // Unsupported protocol version.
    auto request = fixture.request("bad-version");
    request.protocolVersion = 999;
    auto outcome = service.execute(request, fixture.hashOf(request), fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome.result.accepted);
    EXPECT_EQ(outcome.result.error, mwmp::records::EnchantingError::UnsupportedProtocol);

    // Empty request id.
    auto request2 = fixture.request("bad-id");
    request2.requestId.clear();
    auto outcome2 = service.execute(request2, fixture.hashOf(request2), fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome2.result.accepted);
    EXPECT_EQ(outcome2.result.error, mwmp::records::EnchantingError::InvalidRequest);

    // Empty name.
    auto request3 = fixture.request("no-name");
    request3.itemName.clear();
    auto outcome3 = service.execute(request3, fixture.hashOf(request3), fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome3.result.accepted);
    EXPECT_EQ(outcome3.result.error, mwmp::records::EnchantingError::InvalidRequest);

    // No effects.
    auto request4 = fixture.request("no-effects");
    request4.effects.clear();
    auto outcome4 = service.execute(request4, fixture.hashOf(request4), fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome4.result.accepted);
    EXPECT_EQ(outcome4.result.error, mwmp::records::EnchantingError::InvalidRequest);

    // Self-enchanting with an enchanter identity is contradictory.
    auto request5 = fixture.request("self-with-enchanter");
    request5.enchanterNetId = 7;
    auto outcome5 = service.execute(request5, fixture.hashOf(request5), fixture.context(player, inventory, 0));
    EXPECT_FALSE(outcome5.result.accepted);
    EXPECT_EQ(outcome5.result.error, mwmp::records::EnchantingError::InvalidRequest);
}
