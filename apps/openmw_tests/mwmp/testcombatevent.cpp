#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>

#include <apps/openmw-server/PlayerDatabase.hpp>
#include <components/openmw-mp/Base/ActorSyncProtocol.hpp>
#include <components/openmw-mp/CombatEvent.hpp>
#include <components/openmw-mp/Sha256.hpp>

namespace
{
    struct TemporaryDatabase
    {
        std::filesystem::path path = std::filesystem::temp_directory_path()
            / ("openmw-combat-event-test-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".db");
        ~TemporaryDatabase()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
            std::filesystem::remove(path.string() + "-wal", error);
            std::filesystem::remove(path.string() + "-shm", error);
        }
    };

    mwmp::CombatEventRecord proposal(std::int64_t account, std::int64_t character)
    {
        mwmp::CombatEventRecord event;
        event.accountId = account;
        event.characterId = character;
        event.attackerGuid = 11;
        event.victimActorInstanceId
            = mwmp::packActorInstanceKey({ mwmp::ActorKeyKind::VanillaRefNum, 77 });
        event.victimRefId = "guard";
        event.cellId = "Balmora";
        event.migrationGeneration = 3;
        event.authorityGeneration = 9;
        event.actorAuthorityGuid = 22;
        event.proposedDamage = 8.f;
        event.proposedHealthDamage = true;
        event.proposalHash = mwmp::crypto::sha256hex("proposal");
        event.createdAtMs = 5000;
        return event;
    }
}

TEST(CombatEventPersistence, AcceptedAttributionIsIdempotentAndRestartDurable)
{
    TemporaryDatabase temporary;
    std::uint64_t eventId = 0;
    std::int64_t character = 0;
    std::uint64_t victim = 0;
    {
        mwmp::PlayerDatabase database(temporary.path.string());
        const std::int64_t account = database.createAccount("combat-account");
        character = database.createCharacter(account, "Combat Tester").characterId;
        const auto original = proposal(account, character);
        victim = original.victimActorInstanceId;
        eventId = database.createCombatEvent(original);
        ASSERT_NE(eventId, 0u);
        EXPECT_EQ(database.acceptCombatEvent(eventId, 4, mwmp::CombatResultApplied, 8.f, true),
            mwmp::CombatEventCommitStatus::Committed);
        EXPECT_EQ(database.acceptCombatEvent(eventId, 4, mwmp::CombatResultApplied, 8.f, true),
            mwmp::CombatEventCommitStatus::IdenticalReplay);
        EXPECT_EQ(database.acceptCombatEvent(eventId, 5, mwmp::CombatResultApplied, 8.f, true),
            mwmp::CombatEventCommitStatus::ConflictingReplay);
        database.markCombatAssaultReported(eventId, true);
        EXPECT_TRUE(database.hasReportedCriminalAssault(character, victim, 3));
    }

    mwmp::PlayerDatabase reopened(temporary.path.string());
    const auto restored = reopened.loadCombatEvent(eventId);
    ASSERT_TRUE(restored.has_value());
    EXPECT_TRUE(restored->accepted);
    EXPECT_TRUE(restored->qualifyingCrime);
    EXPECT_TRUE(restored->assaultReported);
    EXPECT_TRUE(reopened.hasReportedCriminalAssault(character, victim, 3));
}

TEST(WerewolfStatePersistence, TransformationEdgesAreRestartDurableAndIdempotent)
{
    TemporaryDatabase temporary;
    std::int64_t character = 0;
    {
        mwmp::PlayerDatabase database(temporary.path.string());
        const std::int64_t account = database.createAccount("werewolf-account");
        character = database.createCharacter(account, "Werewolf Tester").characterId;

        const auto normal = database.updateWerewolfState(character, false);
        EXPECT_FALSE(normal.changed);
        EXPECT_EQ(normal.transition, 0u);

        const auto transformed = database.updateWerewolfState(character, true);
        EXPECT_TRUE(transformed.changed);
        EXPECT_TRUE(transformed.transformed);
        EXPECT_EQ(transformed.transition, 1u);

        const auto duplicate = database.updateWerewolfState(character, true);
        EXPECT_FALSE(duplicate.changed);
        EXPECT_EQ(duplicate.transition, 1u);
    }

    mwmp::PlayerDatabase reopened(temporary.path.string());
    const auto duplicateAfterRestart = reopened.updateWerewolfState(character, true);
    EXPECT_FALSE(duplicateAfterRestart.changed);
    EXPECT_EQ(duplicateAfterRestart.transition, 1u);
    const auto reverted = reopened.updateWerewolfState(character, false);
    EXPECT_TRUE(reverted.changed);
    EXPECT_FALSE(reverted.transformed);
    EXPECT_EQ(reverted.transition, 2u);
}
