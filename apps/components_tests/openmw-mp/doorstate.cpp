#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>

#include <sqlite3.h>

#include <apps/openmw-server/PlayerDatabase.hpp>
#include <components/openmw-mp/Packets/Object/PacketDoorState.hpp>

namespace
{
    struct TemporaryDoorDatabase
    {
        std::filesystem::path path = std::filesystem::temp_directory_path()
            / ("openmw-door-state-test-"
                + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".db");

        ~TemporaryDoorDatabase()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
            std::filesystem::remove(path.string() + "-wal", error);
            std::filesystem::remove(path.string() + "-shm", error);
        }
    };

    mwmp::DoorEntry doorEntry(std::uint64_t revision = 7)
    {
        mwmp::DoorEntry entry;
        entry.cellId = "EXT:-3,-2";
        entry.refId = "Ex_De_SN_Gate";
        entry.refNum = 321262;
        entry.isOpen = true;
        entry.revision = revision;
        return entry;
    }
}

TEST(DoorStateProtocolTest, RoundTripsVersionedRevision)
{
    mwmp::PacketDoorState encoder;
    encoder.authorGuid = 42;
    encoder.cellId = "EXT:-3,-2";
    encoder.doors.push_back(doorEntry());
    const std::vector<std::uint8_t> bytes = encoder.encode(99);

    mwmp::PacketDoorState decoder;
    ASSERT_TRUE(decoder.decode(bytes));
    EXPECT_EQ(decoder.authorGuid, 42u);
    EXPECT_EQ(decoder.cellId, "EXT:-3,-2");
    EXPECT_EQ(decoder.doors, encoder.doors);
    EXPECT_EQ(decoder.getSequence(), 99u);
}

TEST(DoorStateProtocolTest, RejectsTruncationAndTrailingBytesAtomically)
{
    mwmp::PacketDoorState encoder;
    encoder.cellId = "EXT:-3,-2";
    encoder.doors.push_back(doorEntry());
    const std::vector<std::uint8_t> valid = encoder.encode();

    mwmp::PacketDoorState decoder;
    decoder.cellId = "unchanged";
    decoder.doors = { doorEntry(3) };
    auto truncated = valid;
    truncated.pop_back();
    EXPECT_FALSE(decoder.decode(truncated));
    EXPECT_EQ(decoder.cellId, "unchanged");
    EXPECT_EQ(decoder.doors, (std::vector<mwmp::DoorEntry>{ doorEntry(3) }));

    auto trailing = valid;
    trailing.push_back(0);
    EXPECT_FALSE(decoder.decode(trailing));
}

TEST(DoorStatePersistenceTest, RestartRestoresAuthoritativeRevision)
{
    TemporaryDoorDatabase temporary;
    {
        mwmp::PlayerDatabase database(temporary.path.string());
        database.upsertDoorState(doorEntry(12));
    }
    {
        mwmp::PlayerDatabase database(temporary.path.string());
        const std::vector<mwmp::DoorEntry> restored = database.loadDoorStates();
        ASSERT_EQ(restored.size(), 1u);
        EXPECT_EQ(restored.front(), doorEntry(12));
    }
}

TEST(DoorStatePersistenceTest, LegacyRowsMigrateToRevisionOne)
{
    TemporaryDoorDatabase temporary;
    sqlite3* raw = nullptr;
    ASSERT_EQ(sqlite3_open(temporary.path.string().c_str(), &raw), SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(raw,
        "CREATE TABLE world_doors(cell_id TEXT NOT NULL, ref_id TEXT NOT NULL, ref_num INTEGER NOT NULL DEFAULT 0,"
        " mp_num INTEGER NOT NULL DEFAULT 0, is_open INTEGER NOT NULL DEFAULT 0,"
        " is_locked INTEGER NOT NULL DEFAULT 0, lock_level INTEGER NOT NULL DEFAULT 0,"
        " PRIMARY KEY(cell_id, ref_id, ref_num));"
        " INSERT INTO world_doors VALUES('EXT:-3,-2','Ex_De_SN_Gate',321262,0,1,0,0);",
        nullptr, nullptr, nullptr), SQLITE_OK);
    sqlite3_close(raw);

    mwmp::PlayerDatabase database(temporary.path.string());
    const std::vector<mwmp::DoorEntry> restored = database.loadDoorStates();
    ASSERT_EQ(restored.size(), 1u);
    EXPECT_EQ(restored.front().revision, 1u);
    EXPECT_TRUE(restored.front().isOpen);
}
