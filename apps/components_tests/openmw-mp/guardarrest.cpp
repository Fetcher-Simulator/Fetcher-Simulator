#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>

#include <components/openmw-mp/GuardArrest.hpp>
#include <components/openmw-mp/Packets/Player/PacketGuardArrest.hpp>

#include "../../openmw-server/PlayerDatabase.hpp"

TEST(GuardArrestProtocol, RequestRoundTripsAndBindsAuthoritativeState)
{
    mwmp::PacketGuardArrest outgoing;
    outgoing.mode = mwmp::PacketGuardArrest::Mode::Request;
    outgoing.request.requestId = "guard-arrest-1";
    outgoing.request.action = mwmp::GuardArrestAction::PayFine;
    outgoing.request.cellId = "Caldera, Irgola: Pawnbroker";
    outgoing.request.actorNetId = mwmp::packActorInstanceKey({ mwmp::ActorKeyKind::VanillaRefNum, 273861 });
    outgoing.request.migrationGeneration = 4;
    outgoing.request.expectedCrimeRevision = 12;
    outgoing.request.expectedInventoryRevision = 9;

    const auto encoded = outgoing.encode();
    mwmp::PacketGuardArrest incoming;
    ASSERT_TRUE(incoming.decode(encoded));
    EXPECT_EQ(incoming.mode, outgoing.mode);
    EXPECT_EQ(incoming.request, outgoing.request);
    EXPECT_EQ(mwmp::canonicalGuardArrestRequest(incoming.request),
        mwmp::canonicalGuardArrestRequest(outgoing.request));

    auto changed = outgoing.request;
    changed.expectedInventoryRevision++;
    EXPECT_NE(mwmp::canonicalGuardArrestRequest(changed),
        mwmp::canonicalGuardArrestRequest(outgoing.request));
    changed = outgoing.request;
    changed.action = mwmp::GuardArrestAction::Resist;
    EXPECT_NE(mwmp::canonicalGuardArrestRequest(changed),
        mwmp::canonicalGuardArrestRequest(outgoing.request));
}

TEST(GuardArrestProtocol, ResultRoundTripsAndHasDurableEncoding)
{
    mwmp::GuardArrestResult result;
    result.requestId = "guard-arrest-2";
    result.action = mwmp::GuardArrestAction::PayFine;
    result.accepted = true;
    result.crimeState.bounty = 0;
    result.crimeState.currentCrimeId = 7;
    result.crimeState.paidCrimeId = 7;
    result.crimeState.revision = 13;
    result.inventoryRevision = 10;
    result.goldPaid = 125;

    const std::string durable = mwmp::encodeGuardArrestResult(result);
    EXPECT_EQ(mwmp::decodeGuardArrestResult(durable), result);

    mwmp::PacketGuardArrest outgoing;
    outgoing.mode = mwmp::PacketGuardArrest::Mode::Result;
    outgoing.result = result;
    mwmp::PacketGuardArrest incoming;
    ASSERT_TRUE(incoming.decode(outgoing.encode()));
    EXPECT_EQ(incoming.mode, outgoing.mode);
    EXPECT_EQ(incoming.result, result);
}

TEST(GuardArrestProtocol, ReachAndPromptRoundTrip)
{
    mwmp::GuardArrestReach reach;
    reach.cellId = "Caldera, Irgola: Pawnbroker";
    reach.actorNetId = mwmp::packActorInstanceKey({ mwmp::ActorKeyKind::VanillaRefNum, 273861 });
    reach.migrationGeneration = 3;
    reach.offenderGuid = 77;
    ASSERT_TRUE(mwmp::validateGuardArrestReach(reach));

    for (const auto mode : { mwmp::PacketGuardArrest::Mode::Reach, mwmp::PacketGuardArrest::Mode::Prompt })
    {
        mwmp::PacketGuardArrest outgoing;
        outgoing.mode = mode;
        outgoing.reach = reach;
        mwmp::PacketGuardArrest incoming;
        ASSERT_TRUE(incoming.decode(outgoing.encode()));
        EXPECT_EQ(incoming.mode, mode);
        EXPECT_EQ(incoming.reach, reach);
    }

    reach.offenderGuid = 0;
    EXPECT_FALSE(mwmp::validateGuardArrestReach(reach));
}

TEST(GuardArrestProtocol, EnforcesActionSpecificResultSemantics)
{
    mwmp::GuardArrestResult result;
    result.requestId = "guard-arrest-3";
    result.accepted = true;
    result.action = mwmp::GuardArrestAction::Surrender;
    result.crimeState.bounty = 0;
    result.sentenceDays = 1;
    EXPECT_TRUE(mwmp::validateGuardArrestResult(result));

    result.sentenceDays = 0;
    EXPECT_FALSE(mwmp::validateGuardArrestResult(result));

    result.action = mwmp::GuardArrestAction::Resist;
    result.crimeState.bounty = 5;
    EXPECT_TRUE(mwmp::validateGuardArrestResult(result));
    result.goldPaid = 5;
    EXPECT_FALSE(mwmp::validateGuardArrestResult(result));
}

namespace
{
    struct TemporaryGuardArrestDatabase
    {
        std::filesystem::path path = std::filesystem::temp_directory_path()
            / ("openmw-guard-arrest-test-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".db");

        ~TemporaryGuardArrestDatabase()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
            std::filesystem::remove(path.string() + "-wal", error);
            std::filesystem::remove(path.string() + "-shm", error);
        }
    };

    mwmp::CrimeMutationCommit makeCrimeCommit(std::int64_t accountId, std::int64_t characterId,
        std::string requestId, std::string service, const mwmp::PlayerCrimeState& before,
        const mwmp::PlayerCrimeState& after, std::string resultPayload)
    {
        mwmp::CrimeMutationCommit commit;
        commit.service = std::move(service);
        commit.accountId = accountId;
        commit.characterId = characterId;
        commit.requestId = std::move(requestId);
        commit.requestHash = "hash:" + commit.requestId;
        commit.resultPayload = std::move(resultPayload);
        commit.source = "test";
        commit.expectedRevision = before.revision;
        commit.resultingState = after;
        return commit;
    }
}

TEST(GuardArrestDatabase, PayFineAtomicallyRemovesGoldAndClearsCrime)
{
    TemporaryGuardArrestDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const std::int64_t accountId = database.createAccount("guard-arrest-test");
    const auto character = database.createCharacter(accountId, "Fine Payer");

    mwmp::Item gold;
    gold.instanceId = 1001;
    gold.refId = "gold_001";
    gold.count = 100;
    mwmp::Item sword;
    sword.instanceId = 1002;
    sword.refId = "iron longsword";
    sword.count = 1;
    database.saveCharacterInventory(character.characterId, { gold, sword }, false, 7);

    const mwmp::PlayerCrimeState emptyCrime = database.loadPlayerCrimeState(character.characterId);
    mwmp::PlayerCrimeState wanted = emptyCrime;
    wanted.bounty = 50;
    ++wanted.revision;
    ASSERT_EQ(database.commitPlayerCrimeMutation(makeCrimeCommit(accountId, character.characterId,
        "seed-crime", "crime", emptyCrime, wanted, "seed" )).status, mwmp::CrimeCommitStatus::Committed);

    mwmp::GuardArrestResult result;
    result.requestId = "pay-fine-1";
    result.action = mwmp::GuardArrestAction::PayFine;
    result.accepted = true;
    result.crimeState = wanted;
    result.crimeState.bounty = 0;
    result.crimeState.paidCrimeId = result.crimeState.currentCrimeId;
    ++result.crimeState.revision;
    result.inventoryRevision = 8;
    result.goldPaid = 50;

    gold.count = 50;
    mwmp::GuardArrestCommit commit;
    commit.crimeMutation = makeCrimeCommit(accountId, character.characterId,
        result.requestId, "guard-arrest", wanted, result.crimeState, mwmp::encodeGuardArrestResult(result));
    commit.inventoryChanged = true;
    commit.expectedInventoryRevision = 7;
    commit.resultingInventoryRevision = 8;
    commit.inventory = { gold, sword };

    const auto committed = database.commitGuardArrest(commit);
    ASSERT_EQ(committed.status, mwmp::GuardArrestCommitStatus::Committed);
    EXPECT_EQ(database.loadPlayerCrimeState(character.characterId), result.crimeState);
    EXPECT_EQ(database.loadInventoryRevision(character.characterId), 8u);
    const auto inventory = database.loadCharacterInventory(character.characterId);
    ASSERT_EQ(inventory.size(), 2u);
    EXPECT_EQ(inventory[0].refId, "gold_001");
    EXPECT_EQ(inventory[0].count, 50);
    EXPECT_EQ(inventory[1].refId, "iron longsword");

    const auto replay = database.commitGuardArrest(commit);
    EXPECT_EQ(replay.status, mwmp::GuardArrestCommitStatus::DuplicateRequest);
    EXPECT_EQ(mwmp::decodeGuardArrestResult(replay.storedResultPayload), result);
    EXPECT_EQ(database.loadCharacterInventory(character.characterId)[0].count, 50);
}

TEST(GuardArrestDatabase, StaleInventoryRollsBackCrimeClear)
{
    TemporaryGuardArrestDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const std::int64_t accountId = database.createAccount("guard-arrest-stale-test");
    const auto character = database.createCharacter(accountId, "Stale Fine Payer");

    mwmp::Item gold;
    gold.instanceId = 2001;
    gold.refId = "gold_001";
    gold.count = 100;
    database.saveCharacterInventory(character.characterId, { gold }, false, 8);

    const mwmp::PlayerCrimeState emptyCrime = database.loadPlayerCrimeState(character.characterId);
    mwmp::PlayerCrimeState wanted = emptyCrime;
    wanted.bounty = 25;
    ++wanted.revision;
    ASSERT_EQ(database.commitPlayerCrimeMutation(makeCrimeCommit(accountId, character.characterId,
        "seed-stale-crime", "crime", emptyCrime, wanted, "seed" )).status, mwmp::CrimeCommitStatus::Committed);

    mwmp::GuardArrestResult result;
    result.requestId = "pay-fine-stale";
    result.action = mwmp::GuardArrestAction::PayFine;
    result.accepted = true;
    result.crimeState = wanted;
    result.crimeState.bounty = 0;
    result.crimeState.paidCrimeId = result.crimeState.currentCrimeId;
    ++result.crimeState.revision;
    result.inventoryRevision = 8;
    result.goldPaid = 25;

    gold.count = 75;
    mwmp::GuardArrestCommit commit;
    commit.crimeMutation = makeCrimeCommit(accountId, character.characterId,
        result.requestId, "guard-arrest", wanted, result.crimeState, mwmp::encodeGuardArrestResult(result));
    commit.inventoryChanged = true;
    commit.expectedInventoryRevision = 7;
    commit.resultingInventoryRevision = 8;
    commit.inventory = { gold };

    const auto committed = database.commitGuardArrest(commit);
    EXPECT_EQ(committed.status, mwmp::GuardArrestCommitStatus::StaleInventoryRevision);
    EXPECT_EQ(database.loadPlayerCrimeState(character.characterId), wanted);
    EXPECT_EQ(database.loadInventoryRevision(character.characterId), 8u);
    ASSERT_EQ(database.loadCharacterInventory(character.characterId).size(), 1u);
    EXPECT_EQ(database.loadCharacterInventory(character.characterId)[0].count, 100);
    EXPECT_FALSE(database.loadSemanticRequest("guard-arrest", accountId,
        character.characterId, result.requestId).has_value());
}

TEST(GuardArrestProtocol, RejectsInvalidGuardIdentityAndTrailingPayload)
{
    mwmp::GuardArrestRequest request;
    request.requestId = "guard-arrest-4";
    request.cellId = "Balmora";
    request.migrationGeneration = 1;
    EXPECT_FALSE(mwmp::validateGuardArrestRequest(request));

    request.actorNetId = mwmp::packActorInstanceKey({ mwmp::ActorKeyKind::VanillaRefNum, 42 });
    EXPECT_TRUE(mwmp::validateGuardArrestRequest(request));

    mwmp::PacketGuardArrest packet;
    packet.request = request;
    auto encoded = packet.encode();
    encoded.push_back(0);
    EXPECT_FALSE(packet.decode(encoded));
}
