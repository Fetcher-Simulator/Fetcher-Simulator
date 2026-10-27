#include "surfphysics.hpp"

#include "../mwmechanics/actorutil.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/ptr.hpp"

namespace MWPhysics
{
    SurfPhysicsSettings getPlayerSurfPhysicsSettings(const MWWorld::ConstPtr& ptr)
    {
        if (ptr != MWMechanics::getPlayer() || !ptr.isInCell())
            return {};
        const auto settings = getSurfPhysicsSettings();
        const auto* cell = ptr.getCell()->getCell();
        const std::string cellId = cell->isExterior()
            ? "EXT:" + std::to_string(cell->getGridX()) + ',' + std::to_string(cell->getGridY())
            : std::string(cell->getNameId());
        if (settings.cellId != cellId)
            return {};
        return settings;
    }
}
