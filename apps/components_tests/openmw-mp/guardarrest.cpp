#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <algorithm>
#include <stdexcept>
#include <vector>

#include <components/openmw-mp/GuardArrest.hpp>
#include <components/openmw-mp/Packets/Player/PacketGuardArrest.hpp>

#include "../../openmw-server/PlayerDatabase.hpp"
#include "../../openmw-server/JailSentenceService.hpp"

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

TEST(JailSentenceService, NoStolenItemsLeavesInventoryEquipmentAndEvidenceUntouched)
{
    mwmp::Item item { 10, "iron longsword", 1, 80, 15.f, "" };
    mwmp::EquipmentItem equipped { 4, item };
    mwmp::ContainerRecord evidence;
    evidence.cellId = "Balmora, Fort Moonmoth";
    evidence.refId = "stolen_goods";
    evidence.refNum = 42;
    evidence.hasAuthority = true;

    const auto plan = mwmp::JailSentenceService::planConfiscation(
        { item }, { equipped }, {}, evidence, [] { return 100u; });
    EXPECT_EQ(plan.error, mwmp::JailSentencePlanError::None);
    EXPECT_FALSE(plan.inventoryChanged());
    EXPECT_EQ(plan.inventory, std::vector<mwmp::Item>({ item }));
    ASSERT_EQ(plan.equipment.size(), 1u);
    EXPECT_EQ(plan.equipment[0].item.instanceId, 10u);
    EXPECT_TRUE(plan.evidence.items.empty());
    EXPECT_TRUE(plan.stolenItemMutations.empty());
}

TEST(JailSentenceService, UnresolvedEvidenceDestinationFailsClosed)
{
    mwmp::Item stolen { 9, "glass dagger", 1, 50, -1.f, "" };
    mwmp::ContainerRecord unresolved;
    const auto plan = mwmp::JailSentenceService::planConfiscation({ stolen }, {},
        { { stolen.refId, "owner", false, 1 } }, unresolved, [] { return 10u; });
    EXPECT_EQ(plan.error, mwmp::JailSentencePlanError::EvidenceUnavailable);
    EXPECT_EQ(plan.inventory, std::vector<mwmp::Item>({ stolen }));
    EXPECT_TRUE(plan.confiscatedItems.empty());
}

TEST(JailSentenceService, ConfiscatesOnlyAggregateStolenCountInInventoryOrder)
{
    mwmp::Item first { 10, "exclusive_potion", 2, -1, 37.5f, "golden saint" };
    mwmp::Item second { 11, "exclusive_potion", 3, -1, 12.f, "winged twilight" };
    mwmp::ContainerRecord evidence;
    evidence.cellId = "Vivec, Ministry of Truth";
    evidence.refId = "stolen_goods";
    evidence.refNum = 77;
    evidence.hasAuthority = true;
    const std::vector<mwmp::StolenItemRecord> stolen {
        { "EXCLUSIVE_POTION", "hlaalu", true, 1 },
        { "exclusive_potion", "owner_npc", false, 2 },
    };
    std::uint32_t nextId = 100;

    const auto plan = mwmp::JailSentenceService::planConfiscation(
        { first, second }, {}, stolen, evidence, [&] { return nextId++; });
    ASSERT_EQ(plan.error, mwmp::JailSentencePlanError::None);
    ASSERT_EQ(plan.inventory.size(), 1u);
    EXPECT_EQ(plan.inventory[0].instanceId, 11u);
    EXPECT_EQ(plan.inventory[0].count, 2); // two legitimate copies remain
    ASSERT_EQ(plan.confiscatedItems.size(), 2u);
    EXPECT_EQ(plan.confiscatedItems[0], first); // full first stack follows vanilla inventory order
    EXPECT_EQ(plan.confiscatedItems[1].instanceId, 100u);
    EXPECT_EQ(plan.confiscatedItems[1].count, 1);
    EXPECT_FLOAT_EQ(plan.confiscatedItems[1].enchantmentCharge, 12.f);
    EXPECT_EQ(plan.confiscatedItems[1].soul, "winged twilight");
    ASSERT_EQ(plan.evidence.items.size(), 2u);
    EXPECT_EQ(plan.evidence.items[0].instanceId, 10u);
    EXPECT_EQ(plan.evidence.items[1].instanceId, 100u);
    ASSERT_EQ(plan.stolenItemMutations.size(), 3u);
    EXPECT_EQ(plan.stolenItemMutations[0].countDelta, -1);
    EXPECT_EQ(plan.stolenItemMutations[1].countDelta, -1);
    EXPECT_EQ(plan.stolenItemMutations[2].countDelta, -1);
}

TEST(JailSentenceService, FullyConfiscatedEquippedInstanceIsUnequipped)
{
    mwmp::Item stolen { 55, "daedric dagger", 1, 321, 99.f, "" };
    mwmp::Item legitimate { 56, "common shirt_01", 1, -1, -1.f, "" };
    mwmp::ContainerRecord evidence { "Seyda Neen, Census and Excise Office", "stolen_goods", 5 };
    evidence.hasAuthority = true;
    const auto plan = mwmp::JailSentenceService::planConfiscation(
        { stolen, legitimate }, { { 5, stolen }, { 2, legitimate } },
        { { stolen.refId, "guard_owner", false, 1 } }, evidence, [] { return 200u; });
    ASSERT_EQ(plan.error, mwmp::JailSentencePlanError::None);
    ASSERT_EQ(plan.inventory.size(), 1u);
    EXPECT_EQ(plan.inventory[0], legitimate);
    ASSERT_EQ(plan.equipment.size(), 1u);
    EXPECT_EQ(plan.equipment[0].item, legitimate);
    ASSERT_EQ(plan.evidence.items.size(), 1u);
    EXPECT_EQ(plan.evidence.items[0].charge, 321);
    EXPECT_FLOAT_EQ(plan.evidence.items[0].enchantmentCharge, 99.f);
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

    void seedStolenLedger(mwmp::PlayerDatabase& database, std::int64_t accountId,
        std::int64_t characterId, const std::vector<mwmp::Item>& inventory,
        const mwmp::StolenItemMutation& mutation, std::uint64_t expectedRevision)
    {
        mwmp::InventoryTakeCommit seed;
        seed.accountId = accountId;
        seed.characterId = characterId;
        seed.requestId = "seed-stolen-ledger";
        seed.requestHash = "seed-stolen-ledger-hash";
        seed.result.requestId = seed.requestId;
        seed.result.accepted = true;
        seed.result.kind = mwmp::InventoryTakeKind::Container;
        seed.result.itemRefId = mutation.refId;
        seed.result.itemCount = static_cast<int>(mutation.countDelta);
        seed.result.inventoryRevision = expectedRevision + 1;
        seed.expectedInventoryRevision = expectedRevision;
        seed.resultingInventoryRevision = expectedRevision + 1;
        seed.inventory = inventory;
        seed.stolenItemMutations.push_back(mutation);
        ASSERT_EQ(database.commitInventoryTake(seed).status, mwmp::InventoryTakeCommitStatus::Committed);
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

TEST(GuardArrestDatabase, SentenceAtomicallyMovesStolenPhysicalItemAndUnequipsIt)
{
    TemporaryGuardArrestDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const std::int64_t accountId = database.createAccount("guard-arrest-confiscation");
    const auto character = database.createCharacter(accountId, "Evidence Prisoner");

    mwmp::Item stolen { 3001, "daedric dagger", 1, 222, 61.25f, "golden saint" };
    mwmp::Item legitimate { 3002, "common pants_01", 1, -1, -1.f, "" };
    database.saveCharacterInventory(character.characterId, { stolen, legitimate }, false, 1);
    database.saveCharacterEquipment(character.characterId, { { 5, stolen } }, false);
    seedStolenLedger(database, accountId, character.characterId, { stolen, legitimate },
        { stolen.refId, "owner_npc", false, 1 }, 1);

    mwmp::ContainerRecord evidence;
    evidence.cellId = "Balmora, Fort Moonmoth";
    evidence.refId = "stolen_goods";
    evidence.refNum = 91;
    evidence.hasAuthority = true;
    evidence.items.push_back({ "restore fatigue standard", 2, -1, 4001, -1.f, "" });
    database.upsertContainerRecord(evidence);

    const mwmp::PlayerCrimeState emptyCrime = database.loadPlayerCrimeState(character.characterId);
    mwmp::PlayerCrimeState wanted = emptyCrime;
    wanted.bounty = 100;
    ++wanted.revision;
    ASSERT_EQ(database.commitPlayerCrimeMutation(makeCrimeCommit(accountId, character.characterId,
        "seed-confiscation-crime", "crime", emptyCrime, wanted, "seed")).status,
        mwmp::CrimeCommitStatus::Committed);

    const auto plan = mwmp::JailSentenceService::planConfiscation({ stolen, legitimate }, { { 5, stolen } },
        database.loadCharacterStolenItems(character.characterId), evidence, [] { return 5001u; });
    ASSERT_EQ(plan.error, mwmp::JailSentencePlanError::None);
    mwmp::GuardArrestResult result;
    result.requestId = "sentence-confiscation";
    result.action = mwmp::GuardArrestAction::Surrender;
    result.accepted = true;
    result.crimeState = wanted;
    result.crimeState.bounty = 0;
    result.crimeState.paidCrimeId = result.crimeState.currentCrimeId;
    ++result.crimeState.revision;
    result.inventoryRevision = 3;
    result.sentenceDays = 1;

    mwmp::GuardArrestCommit commit;
    commit.crimeMutation = makeCrimeCommit(accountId, character.characterId, result.requestId,
        "guard-arrest", wanted, result.crimeState, mwmp::encodeGuardArrestResult(result));
    commit.inventoryChanged = true;
    commit.expectedInventoryRevision = 2;
    commit.resultingInventoryRevision = 3;
    commit.inventory = plan.inventory;
    commit.equipment = plan.equipment;
    commit.equipmentChanged = plan.equipmentChanged;
    commit.evidenceChanged = true;
    commit.evidenceWasPersisted = true;
    commit.expectedEvidence = evidence;
    commit.resultingEvidence = plan.evidence;
    commit.stolenItemMutations = plan.stolenItemMutations;

    auto staleEvidenceCommit = commit;
    mwmp::GuardArrestResult staleResult = result;
    staleResult.requestId = "sentence-stale-evidence";
    staleEvidenceCommit.crimeMutation = makeCrimeCommit(accountId, character.characterId,
        staleResult.requestId, "guard-arrest", wanted, staleResult.crimeState,
        mwmp::encodeGuardArrestResult(staleResult));
    staleEvidenceCommit.expectedEvidence.items.push_back(
        { "nonexistent concurrent item", 1, -1, 0, -1.f, "" });
    EXPECT_EQ(database.commitGuardArrest(staleEvidenceCommit).status,
        mwmp::GuardArrestCommitStatus::StaleEvidence);
    EXPECT_EQ(database.loadPlayerCrimeState(character.characterId), wanted);
    EXPECT_EQ(database.loadCharacterInventory(character.characterId),
        std::vector<mwmp::Item>({ stolen, legitimate }));
    EXPECT_FALSE(database.loadSemanticRequest("guard-arrest", accountId,
        character.characterId, staleResult.requestId).has_value());

    ASSERT_EQ(database.commitGuardArrest(commit).status, mwmp::GuardArrestCommitStatus::Committed);
    EXPECT_EQ(database.loadPlayerCrimeState(character.characterId), result.crimeState);
    EXPECT_EQ(database.loadCharacterInventory(character.characterId), plan.inventory);
    EXPECT_TRUE(database.loadCharacterEquipment(character.characterId).empty());
    EXPECT_TRUE(database.loadCharacterStolenItems(character.characterId).empty());
    const auto containers = database.loadContainerRecords();
    const auto storedEvidence = std::find_if(containers.begin(), containers.end(), [&](const auto& value) {
        return value.cellId == evidence.cellId && value.refId == evidence.refId
            && value.refNum == evidence.refNum;
    });
    ASSERT_NE(storedEvidence, containers.end());
    ASSERT_EQ(storedEvidence->items.size(), 2u);
    EXPECT_EQ(storedEvidence->items[1].instanceId, stolen.instanceId);
    EXPECT_EQ(storedEvidence->items[1].charge, stolen.charge);
    EXPECT_FLOAT_EQ(storedEvidence->items[1].enchantmentCharge, stolen.enchantmentCharge);
    EXPECT_EQ(storedEvidence->items[1].soul, stolen.soul);

    EXPECT_EQ(database.commitGuardArrest(commit).status,
        mwmp::GuardArrestCommitStatus::DuplicateRequest);
    auto conflict = commit;
    conflict.crimeMutation.requestHash = "different-hash";
    EXPECT_EQ(database.commitGuardArrest(conflict).status,
        mwmp::GuardArrestCommitStatus::DuplicateRequestConflict);
    const auto afterConflict = database.loadContainerRecords();
    ASSERT_EQ(afterConflict.size(), containers.size());
    ASSERT_EQ(afterConflict[0].items.size(), 2u);
    EXPECT_EQ(afterConflict[0].items[1], storedEvidence->items[1]);

    // A fresh connection models restart/reconnect restoration from durable state.
    mwmp::PlayerDatabase reopened(temporary.path.string());
    EXPECT_EQ(reopened.loadCharacterInventory(character.characterId), plan.inventory);
    EXPECT_TRUE(reopened.loadCharacterEquipment(character.characterId).empty());
    EXPECT_TRUE(reopened.loadCharacterStolenItems(character.characterId).empty());
    ASSERT_EQ(reopened.loadContainerRecords().size(), 1u);
    ASSERT_EQ(reopened.loadContainerRecords()[0].items.size(), 2u);
    EXPECT_EQ(reopened.loadContainerRecords()[0].items[1], storedEvidence->items[1]);
}

TEST(GuardArrestDatabase, InjectedFailuresRollBackCrimeInventoryEvidenceEquipmentAndLedger)
{
    for (const auto failurePoint : { mwmp::GuardArrestCommitFailurePoint::AfterInventoryWrite,
             mwmp::GuardArrestCommitFailurePoint::AfterEvidenceWrite })
    {
        TemporaryGuardArrestDatabase temporary;
        mwmp::PlayerDatabase database(temporary.path.string());
        const std::int64_t accountId = database.createAccount(
            failurePoint == mwmp::GuardArrestCommitFailurePoint::AfterInventoryWrite
                ? "rollback-after-inventory" : "rollback-after-evidence");
        const auto character = database.createCharacter(accountId, "Rollback Prisoner");
        mwmp::Item stolen { 6001, "glass dagger", 1, 88, 7.5f, "" };
        database.saveCharacterInventory(character.characterId, { stolen }, false, 1);
        database.saveCharacterEquipment(character.characterId, { { 5, stolen } }, false);
        seedStolenLedger(database, accountId, character.characterId, { stolen },
            { stolen.refId, "owner", false, 1 }, 1);
        mwmp::ContainerRecord evidence { "Pelagiad, Fort Pelagiad", "stolen_goods", 17 };
        evidence.hasAuthority = true;
        database.upsertContainerRecord(evidence);

        const mwmp::PlayerCrimeState emptyCrime = database.loadPlayerCrimeState(character.characterId);
        mwmp::PlayerCrimeState wanted = emptyCrime;
        wanted.bounty = 40;
        ++wanted.revision;
        ASSERT_EQ(database.commitPlayerCrimeMutation(makeCrimeCommit(accountId, character.characterId,
            "seed-rollback-crime", "crime", emptyCrime, wanted, "seed")).status,
            mwmp::CrimeCommitStatus::Committed);
        const auto plan = mwmp::JailSentenceService::planConfiscation({ stolen }, { { 5, stolen } },
            database.loadCharacterStolenItems(character.characterId), evidence, [] { return 7001u; });
        ASSERT_EQ(plan.error, mwmp::JailSentencePlanError::None);

        mwmp::GuardArrestResult result;
        result.requestId = "rollback-sentence";
        result.action = mwmp::GuardArrestAction::Surrender;
        result.accepted = true;
        result.crimeState = wanted;
        result.crimeState.bounty = 0;
        ++result.crimeState.revision;
        result.inventoryRevision = 3;
        result.sentenceDays = 1;
        mwmp::GuardArrestCommit commit;
        commit.crimeMutation = makeCrimeCommit(accountId, character.characterId, result.requestId,
            "guard-arrest", wanted, result.crimeState, mwmp::encodeGuardArrestResult(result));
        commit.inventoryChanged = true;
        commit.expectedInventoryRevision = 2;
        commit.resultingInventoryRevision = 3;
        commit.inventory = plan.inventory;
        commit.equipment = plan.equipment;
        commit.equipmentChanged = plan.equipmentChanged;
        commit.evidenceChanged = true;
        commit.evidenceWasPersisted = true;
        commit.expectedEvidence = evidence;
        commit.resultingEvidence = plan.evidence;
        commit.stolenItemMutations = plan.stolenItemMutations;
        commit.failurePoint = failurePoint;

        EXPECT_THROW(database.commitGuardArrest(commit), std::runtime_error);
        EXPECT_EQ(database.loadPlayerCrimeState(character.characterId), wanted);
        EXPECT_EQ(database.loadInventoryRevision(character.characterId), 2u);
        EXPECT_EQ(database.loadCharacterInventory(character.characterId), std::vector<mwmp::Item>({ stolen }));
        ASSERT_EQ(database.loadCharacterEquipment(character.characterId).size(), 1u);
        EXPECT_EQ(database.loadCharacterEquipment(character.characterId)[0].item, stolen);
        ASSERT_EQ(database.loadCharacterStolenItems(character.characterId).size(), 1u);
        EXPECT_EQ(database.loadCharacterStolenItems(character.characterId)[0].count, 1);
        ASSERT_EQ(database.loadContainerRecords().size(), 1u);
        EXPECT_TRUE(database.loadContainerRecords()[0].items.empty());
        EXPECT_FALSE(database.loadSemanticRequest("guard-arrest", accountId,
            character.characterId, result.requestId).has_value());
    }
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
