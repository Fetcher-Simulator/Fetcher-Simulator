#include "CollisionCellOwnership.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include <components/misc/constants.hpp>

namespace
{
    constexpr std::size_t MaximumPlayerCollisionCells = 25;

    std::string exteriorCellKey(int x, int y)
    {
        return "EXT:" + std::to_string(x) + "," + std::to_string(y);
    }

    float distanceToInterval(float value, float minimum, float maximum)
    {
        if (value < minimum)
            return minimum - value;
        if (value > maximum)
            return value - maximum;
        return 0.f;
    }
}

mwmp::CollisionCellOwnership::Transition mwmp::CollisionCellOwnership::update(
    std::string_view ownerId, std::vector<std::string> desiredCells)
{
    if (ownerId.empty())
        throw std::invalid_argument("collision owner identity must not be empty");

    desiredCells.erase(std::remove(desiredCells.begin(), desiredCells.end(), std::string()), desiredCells.end());
    std::sort(desiredCells.begin(), desiredCells.end());
    desiredCells.erase(std::unique(desiredCells.begin(), desiredCells.end()), desiredCells.end());

    std::unordered_set<std::string> desired(desiredCells.begin(), desiredCells.end());
    auto it = mOwners.try_emplace(std::string(ownerId)).first;
    std::unordered_set<std::string>& current = it->second;

    Transition result;
    for (const std::string& cellId : desiredCells)
    {
        if (current.find(cellId) == current.end())
            result.acquire.push_back(cellId);
    }
    for (const std::string& cellId : current)
    {
        if (desired.find(cellId) == desired.end())
            result.release.push_back(cellId);
    }
    std::sort(result.release.begin(), result.release.end());

    if (desired.empty())
        mOwners.erase(it);
    else
        current = std::move(desired);
    return result;
}

mwmp::CollisionCellOwnership::Transition mwmp::CollisionCellOwnership::remove(std::string_view ownerId)
{
    const auto it = mOwners.find(std::string(ownerId));
    if (it == mOwners.end())
        return {};

    Transition result;
    result.release.assign(it->second.begin(), it->second.end());
    std::sort(result.release.begin(), result.release.end());
    mOwners.erase(it);
    return result;
}

std::vector<std::string> mwmp::CollisionCellOwnership::cells(std::string_view ownerId) const
{
    const auto it = mOwners.find(std::string(ownerId));
    if (it == mOwners.end())
        return {};
    std::vector<std::string> result(it->second.begin(), it->second.end());
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::string> mwmp::collisionCellsForPlayer(
    const CellId& cell, const Position& position, float observationRadius)
{
    if (!cell.isExterior)
        return cell.cellName.empty() ? std::vector<std::string>() : std::vector<std::string>{ cell.cellName };

    std::vector<std::string> result{ exteriorCellKey(cell.gridX, cell.gridY) };
    if (!std::isfinite(observationRadius) || observationRadius <= 0.f
        || !std::isfinite(position.pos[0]) || !std::isfinite(position.pos[1]))
        return result;

    const float cellSize = static_cast<float>(Constants::CellSizeInUnits);
    const float currentMinX = static_cast<float>(cell.gridX) * cellSize;
    const float currentMinY = static_cast<float>(cell.gridY) * cellSize;
    const float currentMaxX = currentMinX + cellSize;
    const float currentMaxY = currentMinY + cellSize;

    // A position outside the accepted cell cannot be used to select arbitrary
    // collision cells. Cell-change acceptance will establish a new anchor.
    if (position.pos[0] < currentMinX || position.pos[0] > currentMaxX
        || position.pos[1] < currentMinY || position.pos[1] > currentMaxY)
        return result;

    const float boundedRadius = std::min(observationRadius, 2.f * cellSize);
    const int cellRadius = std::min(2, static_cast<int>(std::ceil(boundedRadius / cellSize)) + 1);
    const float radiusSquared = boundedRadius * boundedRadius;
    for (int dy = -cellRadius; dy <= cellRadius; ++dy)
    {
        for (int dx = -cellRadius; dx <= cellRadius; ++dx)
        {
            if (dx == 0 && dy == 0)
                continue;
            const int candidateX = cell.gridX + dx;
            const int candidateY = cell.gridY + dy;
            const float minimumX = static_cast<float>(candidateX) * cellSize;
            const float minimumY = static_cast<float>(candidateY) * cellSize;
            const float distanceX = distanceToInterval(position.pos[0], minimumX, minimumX + cellSize);
            const float distanceY = distanceToInterval(position.pos[1], minimumY, minimumY + cellSize);
            if (distanceX * distanceX + distanceY * distanceY <= radiusSquared)
                result.push_back(exteriorCellKey(candidateX, candidateY));
        }
    }

    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    if (result.size() > MaximumPlayerCollisionCells)
        result.resize(MaximumPlayerCollisionCells);
    return result;
}
