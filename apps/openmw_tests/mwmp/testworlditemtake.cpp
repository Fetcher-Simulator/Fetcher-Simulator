#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>

#include <apps/openmw-server/PlayerDatabase.hpp>
#include <apps/openmw/mwmp/sync/ActorSync.hpp>
#include <apps/openmw/mwmp/sync/WorldObjectSync.hpp>
#include <components/esm3/refnum.hpp>
#include <components/openmw-mp/Base/ActorSyncProtocol.hpp>
#include <components/openmw-mp/Sha256.hpp>

namespace
{
    struct TemporaryDatabase
    {
        std::filesystem::path path = std::filesystem::temp_directory_path()
            / ("openmw-world-item-take-test-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".db");
        ~TemporaryDatabase()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
            std::filesystem::remove(path.string() + "-wal", error);
            std::filesystem::remove(path.string() + "-shm", error);
        }
    };

    mwmp::WorldItemTakeCommit makeCommit(mwmp::PlayerDatabase& database,
        std::int64_t account, std::int64_t character, std::string requestId = "take-1")
    {
        mwmp::WorldItemTakeCommit commit;
        commit.accountId = account;
        commit.characterId = character;
        commit.requestId = std::move(requestId);
        commit.requestHash = mwmp::crypto::sha256hex(commit.requestId);
        commit.object.kind = mwmp::PlacedObjectKind::ContentReference;
        commit.object.cellId = "Balmora";
        commit.object.refId = "iron dagger";
        commit.object.refIndex = 77;
        commit.object.refContentFile = 0;
        commit.result.requestId = commit.requestId;
        commit.result.accepted = true;
        commit.result.object = commit.object;
        commit.result.itemRefId = commit.object.refId;
        commit.result.itemCount = 1;
        commit.result.crimeValue = 10;
        commit.result.theft = true;
        commit.expectedInventoryRevision = database.loadInventoryRevision(character);
        commit.resultingInventoryRevision = commit.expectedInventoryRevision + 1;
        commit.result.inventoryRevision = commit.resultingInventoryRevision;
        mwmp::Item item;
        item.instanceId = 900;
        item.refId = commit.object.refId;
        item.count = 1;
        commit.inventory.push_back(item);
        commit.stolenItemMutations.push_back({ item.refId, "dagger_owner", false, 1 });
        return commit;
    }

    mwmp::CrimeMutationCommit makeCrimeCommit(std::int64_t account, std::int64_t character,
        std::string requestId = "world-take-crime")
    {
        mwmp::CrimeMutationCommit crime;
        crime.service = "crime-event";
        crime.accountId = account;
        crime.characterId = character;
        crime.requestId = std::move(requestId);
        crime.requestHash = mwmp::crypto::sha256hex(crime.requestId);
        crime.resultPayload = "terminal-world-crime-result";
        crime.source = "world-item-take-test";
        crime.expectedRevision = 0;
        crime.resultingState.bounty = 10;
        crime.resultingState.currentCrimeId = 0;
        crime.resultingState.paidCrimeId = -1;
        crime.resultingState.revision = 1;
        return crime;
    }
}

TEST(WorldItemTakeClientPolicy, GenuineLooseWorldObjectRequiresAuthority)
{
    EXPECT_TRUE(mwmp::WorldObjectSync::requiresAuthoritativeWorldItemTake(
        true, true, true, false, false));
}

TEST(WorldItemTakeClientPolicy, DetachedLocalPlayerSplitMayRejoinInventory)
{
    EXPECT_FALSE(mwmp::WorldObjectSync::requiresAuthoritativeWorldItemTake(
        true, true, true, false, true));
}

TEST(WorldItemTakeClientPolicy, BarterAndNonPlayerDestinationsDoNotUseWorldTakeGate)
{
    EXPECT_FALSE(mwmp::WorldObjectSync::requiresAuthoritativeWorldItemTake(
        true, true, true, true, false));
    EXPECT_FALSE(mwmp::WorldObjectSync::requiresAuthoritativeWorldItemTake(
        true, true, false, false, false));
}

TEST(ContainerOpenClientPolicy, NonAuthorityStaticContainerRequestsBootstrap)
{
    EXPECT_TRUE(mwmp::WorldObjectSync::requiresContainerBootstrapOnOpen(
        false, false, false));
    EXPECT_FALSE(mwmp::WorldObjectSync::requiresContainerBootstrapOnOpen(
        false, false, true));
}

TEST(ContainerOpenClientPolicy, NonAuthorityActorCorpseRequestsBootstrap)
{
    EXPECT_TRUE(mwmp::WorldObjectSync::requiresContainerBootstrapOnOpen(
        true, false, true));
    EXPECT_FALSE(mwmp::WorldObjectSync::requiresContainerBootstrapOnOpen(
        true, true, false));
}

TEST(ContainerProjectileRecoveryPolicy, BootstrapsOnlyBeforeFirstAuthoritativeSnapshot)
{
    EXPECT_TRUE(mwmp::WorldObjectSync::requiresProjectileStoredActorBootstrap(false, false));
    EXPECT_FALSE(mwmp::WorldObjectSync::requiresProjectileStoredActorBootstrap(true, false));
    EXPECT_FALSE(mwmp::WorldObjectSync::requiresProjectileStoredActorBootstrap(false, true));
}

TEST(ActorDeathClientPolicy, RealtimeDeathBindingDoesNotReapplyBootstrapFinalPose)
{
    EXPECT_TRUE(mwmp::ActorSync::requiresBootstrapDeathPresentation(false, false));
    EXPECT_TRUE(mwmp::ActorSync::requiresBootstrapDeathPresentation(true, false));
    EXPECT_FALSE(mwmp::ActorSync::requiresBootstrapDeathPresentation(true, true));
}

TEST(ActorDeathClientPolicy, ReplaysCanonicalDeathWhenLocalPoseDiffers)
{
    EXPECT_TRUE(mwmp::ActorSync::requiresAuthoritativeDeathReplay(
        true, true, "death4", "death1"));
    EXPECT_FALSE(mwmp::ActorSync::requiresAuthoritativeDeathReplay(
        true, true, "death2", "death2"));
    EXPECT_FALSE(mwmp::ActorSync::requiresAuthoritativeDeathReplay(
        false, true, "", "death3"));
    EXPECT_FALSE(mwmp::ActorSync::requiresAuthoritativeDeathReplay(
        true, false, "death4", "death1"));
}

TEST(ActorIdentityProtocol, ContentQualifiedVanillaRefNumsDoNotCollide)
{
    const ESM::RefNum baseRef { 1, 0 };
    const ESM::RefNum modRef { 1, 37 };

    mwmp::BaseActor baseActor;
    baseActor.refNum = baseRef.toUint32();
    mwmp::BaseActor modActor;
    modActor.refNum = modRef.toUint32();

    EXPECT_EQ(baseActor.refNum, 1u);
    EXPECT_EQ(modActor.refNum, (37u << 24) | 1u);
    EXPECT_NE(baseActor.refNum, modActor.refNum);
    EXPECT_NE(mwmp::actorInstanceIdFromActor(baseActor), mwmp::actorInstanceIdFromActor(modActor));
    EXPECT_EQ(mwmp::ActorSyncProtocolVersionV2, 13u);
}

TEST(ActorDeathClientPolicy, KnownDeadIdentityRefreshPreservesCorpsePresentation)
{
    EXPECT_TRUE(mwmp::ActorSync::shouldPreserveDeadIdentityRefresh(
        true, true, true, false));
    EXPECT_FALSE(mwmp::ActorSync::shouldPreserveDeadIdentityRefresh(
        false, true, true, false));
    EXPECT_FALSE(mwmp::ActorSync::shouldPreserveDeadIdentityRefresh(
        true, false, true, false));
    EXPECT_FALSE(mwmp::ActorSync::shouldPreserveDeadIdentityRefresh(
        true, true, false, false));
    EXPECT_FALSE(mwmp::ActorSync::shouldPreserveDeadIdentityRefresh(
        true, true, true, true));
}

TEST(WorldItemTakePersistence, CommitsTombstoneInventoryAndReplayAtomically)
{
    TemporaryDatabase temporary;
    std::int64_t account = 0;
    std::int64_t character = 0;
    mwmp::WorldItemTakeCommit original;
    {
        mwmp::PlayerDatabase database(temporary.path.string());
        account = database.createAccount("take-account");
        character = database.createCharacter(account, "Take Tester").characterId;
        original = makeCommit(database, account, character);
        const auto committed = database.commitWorldItemTake(original);
        EXPECT_EQ(committed.status, mwmp::WorldItemTakeCommitStatus::Committed);
        EXPECT_EQ(database.loadInventoryRevision(character), 1u);
        ASSERT_EQ(database.loadCharacterInventory(character).size(), 1u);
        ASSERT_EQ(database.loadTakenWorldItemReferences().size(), 1u);
        ASSERT_EQ(database.loadCharacterStolenItems(character).size(), 1u);

        const auto replay = database.commitWorldItemTake(original);
        EXPECT_EQ(replay.status, mwmp::WorldItemTakeCommitStatus::DuplicateRequest);
        EXPECT_TRUE(replay.result.replayed);

        auto conflict = original;
        conflict.requestHash = mwmp::crypto::sha256hex("different");
        EXPECT_EQ(database.commitWorldItemTake(conflict).status,
            mwmp::WorldItemTakeCommitStatus::DuplicateRequestConflict);

        auto second = makeCommit(database, account, character, "take-2");
        second.object = original.object;
        second.result.object = second.object;
        EXPECT_EQ(database.commitWorldItemTake(second).status,
            mwmp::WorldItemTakeCommitStatus::ObjectAlreadyTaken);
    }

    mwmp::PlayerDatabase reopened(temporary.path.string());
    EXPECT_EQ(reopened.loadInventoryRevision(character), 1u);
    ASSERT_EQ(reopened.loadCharacterInventory(character).size(), 1u);
    ASSERT_EQ(reopened.loadTakenWorldItemReferences().size(), 1u);
    ASSERT_EQ(reopened.loadCharacterStolenItems(character).size(), 1u);
    EXPECT_EQ(reopened.loadCharacterStolenItems(character)[0].ownerId, "dagger_owner");
    EXPECT_EQ(reopened.loadTakenWorldItemReferences().front(), original.object);
}

TEST(WorldItemTakePersistence, CrimeResultCommitsAtomicallyWithTombstoneAndInventory)
{
    TemporaryDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const std::int64_t account = database.createAccount("world-crime-account");
    const std::int64_t character = database.createCharacter(account, "World Crime Tester").characterId;
    auto commit = makeCommit(database, account, character);
    commit.crimeMutation = makeCrimeCommit(account, character);

    EXPECT_EQ(database.commitWorldItemTake(commit).status, mwmp::WorldItemTakeCommitStatus::Committed);
    const mwmp::PlayerCrimeState state = database.loadPlayerCrimeState(character);
    EXPECT_EQ(state.bounty, 10);
    EXPECT_EQ(state.currentCrimeId, 0);
    EXPECT_EQ(state.revision, 1u);
    EXPECT_TRUE(database.loadSemanticRequest(
        "crime-event", account, character, commit.crimeMutation->requestId).has_value());
    EXPECT_EQ(database.loadTakenWorldItemReferences().size(), 1u);
    EXPECT_EQ(database.loadInventoryRevision(character), 1u);
    ASSERT_EQ(database.loadCharacterStolenItems(character).size(), 1u);
}

TEST(WorldItemTakePersistence, CrimeFailureRollsBackTombstoneInventoryAndSemanticJournal)
{
    TemporaryDatabase temporary;
    mwmp::PlayerDatabase database(temporary.path.string());
    const std::int64_t account = database.createAccount("world-rollback-account");
    const std::int64_t character = database.createCharacter(account, "World Rollback Tester").characterId;
    auto commit = makeCommit(database, account, character);
    commit.crimeMutation = makeCrimeCommit(account, character, "world-crime-failure");
    commit.crimeMutation->failurePoint = mwmp::CrimeCommitFailurePoint::AfterStateWrite;

    EXPECT_THROW(database.commitWorldItemTake(commit), std::runtime_error);
    EXPECT_TRUE(database.loadTakenWorldItemReferences().empty());
    EXPECT_TRUE(database.loadCharacterInventory(character).empty());
    EXPECT_EQ(database.loadInventoryRevision(character), 0u);
    EXPECT_TRUE(database.loadCharacterStolenItems(character).empty());
    EXPECT_FALSE(database.loadSemanticRequest(
        "crime-event", account, character, commit.crimeMutation->requestId).has_value());
    EXPECT_EQ(database.loadPlayerCrimeState(character), mwmp::PlayerCrimeState{});
}
