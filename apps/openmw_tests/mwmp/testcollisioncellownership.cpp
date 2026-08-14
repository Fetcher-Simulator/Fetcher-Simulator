#include <gtest/gtest.h>

#include <components/misc/constants.hpp>

#include <apps/openmw-server/CollisionCellOwnership.hpp>

namespace
{
    mwmp::CellId exteriorCell(int x, int y)
    {
        mwmp::CellId result;
        result.isExterior = true;
        result.gridX = x;
        result.gridY = y;
        return result;
    }

    mwmp::Position position(float x, float y)
    {
        mwmp::Position result;
        result.pos[0] = x;
        result.pos[1] = y;
        return result;
    }
}

TEST(CollisionCellOwnershipTest, ComputesDeterministicSetDifferencesWithoutDuplicateAcquires)
{
    mwmp::CollisionCellOwnership ownership;
    const auto first = ownership.update("player:1", { "EXT:0,0", "EXT:1,0", "EXT:0,0" });
    EXPECT_EQ(first.acquire, (std::vector<std::string>{ "EXT:0,0", "EXT:1,0" }));
    EXPECT_TRUE(first.release.empty());

    const auto unchanged = ownership.update("player:1", { "EXT:1,0", "EXT:0,0" });
    EXPECT_TRUE(unchanged.acquire.empty());
    EXPECT_TRUE(unchanged.release.empty());

    const auto moved = ownership.update("player:1", { "EXT:1,0", "EXT:2,0" });
    EXPECT_EQ(moved.acquire, (std::vector<std::string>{ "EXT:2,0" }));
    EXPECT_EQ(moved.release, (std::vector<std::string>{ "EXT:0,0" }));

    const auto removed = ownership.remove("player:1");
    EXPECT_EQ(removed.release, (std::vector<std::string>{ "EXT:1,0", "EXT:2,0" }));
    EXPECT_EQ(ownership.ownerCount(), 0u);
}

TEST(CollisionCellOwnershipTest, InteriorOwnsOnlyAcceptedCurrentCell)
{
    mwmp::CellId cell;
    cell.cellName = "Balmora, Guild of Mages";
    EXPECT_EQ(mwmp::collisionCellsForPlayer(cell, {}, 2000.f),
        (std::vector<std::string>{ "Balmora, Guild of Mages" }));
}

TEST(CollisionCellOwnershipTest, ExteriorCenterDoesNotAcquirePermanentThreeByThreeGrid)
{
    const float cellSize = static_cast<float>(Constants::CellSizeInUnits);
    EXPECT_EQ(mwmp::collisionCellsForPlayer(exteriorCell(0, 0), position(cellSize / 2.f, cellSize / 2.f), 2000.f),
        (std::vector<std::string>{ "EXT:0,0" }));
}

TEST(CollisionCellOwnershipTest, ExteriorBorderAcquiresOnlyCircleIntersectedNeighbors)
{
    const float cellSize = static_cast<float>(Constants::CellSizeInUnits);
    EXPECT_EQ(mwmp::collisionCellsForPlayer(exteriorCell(0, 0), position(cellSize - 100.f, cellSize / 2.f), 2000.f),
        (std::vector<std::string>{ "EXT:0,0", "EXT:1,0" }));

    EXPECT_EQ(mwmp::collisionCellsForPlayer(exteriorCell(0, 0), position(cellSize - 100.f, cellSize - 100.f), 2000.f),
        (std::vector<std::string>{ "EXT:0,0", "EXT:0,1", "EXT:1,0", "EXT:1,1" }));
}

TEST(CollisionCellOwnershipTest, PositionOutsideAcceptedCellCannotSelectOtherCells)
{
    const float cellSize = static_cast<float>(Constants::CellSizeInUnits);
    EXPECT_EQ(mwmp::collisionCellsForPlayer(exteriorCell(0, 0), position(10.f * cellSize, 10.f * cellSize), 2000.f),
        (std::vector<std::string>{ "EXT:0,0" }));
}
