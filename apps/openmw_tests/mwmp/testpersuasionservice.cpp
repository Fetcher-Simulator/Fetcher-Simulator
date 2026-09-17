#include <apps/openmw-server/PersuasionService.hpp>
#include <components/openmw-mp/Packets/Player/PacketPersuasion.hpp>
#include <chrono>
#include <filesystem>
#include <gtest/gtest.h>

namespace
{
    class PersuasionServiceTest : public testing::Test
    {
    protected:
        struct TemporaryDatabase
        {
            std::filesystem::path path = std::filesystem::temp_directory_path()
                / ("persuasion-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".db");
            ~TemporaryDatabase()
            {
                std::error_code error;
                std::filesystem::remove(path, error);
                std::filesystem::remove(path.string() + "-wal", error);
                std::filesystem::remove(path.string() + "-shm", error);
            }
        } temporary;

        mwmp::PlayerDatabase db{ temporary.path.string() };
        mwmp::BasePlayer player;
        mwmp::PersuasionService::Context context;
        mwmp::PersuasionRequest request;
        std::int64_t secondCharacterId = 0;
        int roll = 0;

        PersuasionServiceTest()
        {
            context.accountId = db.createAccount("persuasion-test");
            context.characterId = db.createCharacter(context.accountId, "Mario").characterId;
            secondCharacterId = db.createCharacter(context.accountId, "Sith Soldier").characterId;
            context.inventoryRevision = 0;
            context.player = &player;
            context.actorAvailable = true;
            context.generation = 1;
            context.relationship = { 123, 50, 0 };
            context.currentBase = 50;
            context.derivedOffset = 0;
            context.mechanics.disposition = 50;
            context.mechanics.fight = 30;
            context.mechanics.flee = 20;
            context.mechanics.player = { 50, 50, 0, 1, 1, 50, 50 };
            context.mechanics.npc = { 50, 50, 0, 1, 1, 50, 50 };
            context.gmst = [](std::string_view id) {
                if (id == "fPersonalityMod" || id == "fLuckMod") return 10.f;
                if (id == "fReputationMod" || id == "fLevelMod") return 1.f;
                if (id == "fBribe10Mod" || id == "fBribe100Mod" || id == "fBribe1000Mod") return 0.f;
                if (id == "iPerMinChance") return 0.f;
                if (id == "iPerMinChange") return 1.f;
                if (id == "fPerDieRollMult" || id == "fPerTempMult") return 1.f;
                return 0.f;
            };
            context.roll = [&] { return roll; };

            mwmp::Item gold;
            gold.refId = "gold_001";
            gold.count = 100;
            gold.instanceId = 10;
            player.inventoryChanges.items = { gold };
            db.saveCharacterInventory(context.characterId, player.inventoryChanges.items, false, 0);

            mwmp::ContainerRecord container;
            container.cellId = "Balmora";
            container.refId = "llathyno hlaalu";
            container.refNum = 77;
            container.hasAuthority = true;
            mwmp::ContainerItem existingGold;
            existingGold.refId = "gold_001";
            existingGold.count = 5;
            container.items = { existingGold };
            context.container = container;

            request.requestId = "persuasion-1";
            request.actorId = 123;
            request.generation = 1;
            request.inventoryRevision = 0;
            request.relationshipRevision = 0;
            request.action = mwmp::PersuasionAction::Bribe10;
        }

        mwmp::PersuasionService::Outcome execute(std::string_view hash = "hash-1")
        {
            mwmp::PersuasionService service(db);
            return service.execute(request, hash, context);
        }
    };

    TEST_F(PersuasionServiceTest, SuccessfulBribeAtomicallyChargesAndPersistsRelationshipAndNpcGold)
    {
        roll = 0;
        const auto result = execute();
        ASSERT_TRUE(result.committed);
        EXPECT_TRUE(result.result.success);
        EXPECT_EQ(result.result.chargedGold, 10);
        ASSERT_EQ(result.inventory.size(), 1u);
        EXPECT_EQ(result.inventory.front().count, 90);
        ASSERT_TRUE(result.container);
        ASSERT_EQ(result.container->items.size(), 1u);
        EXPECT_EQ(result.container->items.front().count, 15);

        const auto relationship = db.loadRelationship(context.characterId, 123);
        ASSERT_TRUE(relationship);
        EXPECT_GT(relationship->baseDisposition, 50);
        EXPECT_EQ(relationship->revision, 1u);
        EXPECT_FALSE(db.loadRelationship(secondCharacterId, 123).has_value());

        const auto inventory = db.loadCharacterInventory(context.characterId);
        ASSERT_EQ(inventory.size(), 1u);
        EXPECT_EQ(inventory.front().count, 90);
        const auto containers = db.loadContainerRecords();
        const auto container = std::find_if(containers.begin(), containers.end(), [](const auto& value) {
            return value.refId == "llathyno hlaalu" && value.refNum == 77;
        });
        ASSERT_NE(container, containers.end());
        ASSERT_EQ(container->items.size(), 1u);
        EXPECT_EQ(container->items.front().count, 15);
    }

    TEST_F(PersuasionServiceTest, FailedBribeUsesVanillaDispositionLossWithoutCharging)
    {
        roll = 99;
        const auto result = execute();
        ASSERT_TRUE(result.committed);
        EXPECT_FALSE(result.result.success);
        EXPECT_EQ(result.result.chargedGold, 0);
        EXPECT_LT(result.result.baseDisposition, 50);
        EXPECT_EQ(db.loadCharacterInventory(context.characterId).front().count, 100);
        const auto relationship = db.loadRelationship(context.characterId, 123);
        ASSERT_TRUE(relationship);
        EXPECT_LT(relationship->baseDisposition, 50);
    }

    TEST_F(PersuasionServiceTest, ReplayReturnsOriginalResultAndCannotChargeTwice)
    {
        roll = 0;
        const auto first = execute();
        ASSERT_TRUE(first.committed);
        const auto replay = execute();
        EXPECT_TRUE(replay.replayed);
        EXPECT_FALSE(replay.committed);
        EXPECT_EQ(replay.result.chargedGold, 10);
        EXPECT_EQ(db.loadCharacterInventory(context.characterId).front().count, 90);
        const auto containers = db.loadContainerRecords();
        const auto container = std::find_if(containers.begin(), containers.end(), [](const auto& value) {
            return value.refId == "llathyno hlaalu" && value.refNum == 77;
        });
        ASSERT_NE(container, containers.end());
        EXPECT_EQ(container->items.front().count, 15);
    }

    TEST_F(PersuasionServiceTest, ConflictingRequestIdCannotMutateAgain)
    {
        roll = 0;
        ASSERT_TRUE(execute("hash-a").committed);
        request.action = mwmp::PersuasionAction::Bribe100;
        const auto conflict = execute("hash-b");
        EXPECT_EQ(conflict.result.error, mwmp::PersuasionError::Conflict);
        EXPECT_EQ(db.loadCharacterInventory(context.characterId).front().count, 90);
    }

    TEST_F(PersuasionServiceTest, StaleRelationshipRevisionRejectsWithoutCharge)
    {
        roll = 0;
        ASSERT_TRUE(execute().committed);
        request.requestId = "persuasion-2";
        request.inventoryRevision = 1;
        request.relationshipRevision = 0;
        context.inventoryRevision = 1;
        context.relationship = *db.loadRelationship(context.characterId, 123);
        player.inventoryChanges.items = db.loadCharacterInventory(context.characterId);
        const auto stale = execute("hash-2");
        EXPECT_EQ(stale.result.error, mwmp::PersuasionError::Stale);
        EXPECT_EQ(db.loadCharacterInventory(context.characterId).front().count, 90);
    }

    TEST_F(PersuasionServiceTest, RelationshipSurvivesDatabaseReopen)
    {
        roll = 0;
        const auto first = execute();
        ASSERT_TRUE(first.committed);
        mwmp::PlayerDatabase reopened(temporary.path.string());
        const auto relationship = reopened.loadRelationship(context.characterId, 123);
        ASSERT_TRUE(relationship);
        EXPECT_EQ(relationship->baseDisposition, first.result.baseDisposition);
        EXPECT_EQ(relationship->revision, first.result.relationshipRevision);
    }

    TEST_F(PersuasionServiceTest, InsufficientGoldAndUnavailableActorCannotMutate)
    {
        request.action = mwmp::PersuasionAction::Bribe1000;
        auto insufficient = execute("poor");
        EXPECT_EQ(insufficient.result.error, mwmp::PersuasionError::InsufficientGold);
        EXPECT_FALSE(db.loadRelationship(context.characterId, 123).has_value());
        EXPECT_EQ(db.loadCharacterInventory(context.characterId).front().count, 100);

        request.requestId = "persuasion-unavailable";
        request.action = mwmp::PersuasionAction::Admire;
        context.actorAvailable = false;
        auto unavailable = execute("unavailable");
        EXPECT_EQ(unavailable.result.error, mwmp::PersuasionError::Unavailable);
        EXPECT_FALSE(db.loadRelationship(context.characterId, 123).has_value());
    }

    TEST(PersuasionProtocol, PacketsRoundTripAndMalformedRequestsFailClosed)
    {
        mwmp::PacketPersuasionRequest packet;
        packet.request.requestId = "persuasion-packet";
        packet.request.actorId = 123;
        packet.request.generation = 2;
        packet.request.inventoryRevision = 4;
        packet.request.relationshipRevision = 7;
        packet.request.action = mwmp::PersuasionAction::Bribe100;
        const auto bytes = packet.encode();

        mwmp::PacketPersuasionRequest decoded;
        ASSERT_TRUE(decoded.decode(bytes));
        EXPECT_EQ(decoded.request.requestId, packet.request.requestId);
        EXPECT_EQ(decoded.request.actorId, packet.request.actorId);
        EXPECT_EQ(decoded.request.generation, packet.request.generation);
        EXPECT_EQ(decoded.request.inventoryRevision, packet.request.inventoryRevision);
        EXPECT_EQ(decoded.request.relationshipRevision, packet.request.relationshipRevision);
        EXPECT_EQ(decoded.request.action, packet.request.action);

        auto truncated = bytes;
        truncated.pop_back();
        EXPECT_FALSE(decoded.decode(truncated));

        packet.request.action = static_cast<mwmp::PersuasionAction>(255);
        EXPECT_FALSE(decoded.decode(packet.encode()));
        packet.request.action = mwmp::PersuasionAction::Admire;
        packet.request.requestId.assign(129, 'x');
        EXPECT_FALSE(decoded.decode(packet.encode()));
    }

    TEST(PersuasionMechanics, FailedBribeMatchesNativeNegativeDispositionChange)
    {
        mwmp::PersuasionInput input;
        input.disposition = 50;
        input.player = { 50, 50, 0, 1, 1, 50, 50 };
        input.npc = { 50, 50, 0, 1, 1, 50, 50 };
        const auto gmst = [](std::string_view id) {
            if (id == "fPersonalityMod" || id == "fLuckMod") return 10.f;
            if (id == "fReputationMod" || id == "fLevelMod") return 1.f;
            if (id.starts_with("fBribe")) return 0.f;
            if (id == "iPerMinChance") return 0.f;
            if (id == "iPerMinChange" || id == "fPerDieRollMult" || id == "fPerTempMult") return 1.f;
            return 0.f;
        };
        const auto outcome = mwmp::resolvePersuasion(input, mwmp::PT_Bribe10, 99, gmst);
        EXPECT_FALSE(outcome.success);
        EXPECT_LT(outcome.temporary, 0);
        EXPECT_LT(outcome.permanent, 0);
    }
}
