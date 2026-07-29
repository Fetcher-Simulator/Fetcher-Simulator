#ifndef OPENMW_MWMECHANICS_VEHICLECONTROLLER_H
#define OPENMW_MWMECHANICS_VEHICLECONTROLLER_H

namespace MWWorld
{
    class Ptr;
}

namespace MWMechanics
{
    void updateLocalVehicle(const MWWorld::Ptr& player, float duration);
}

#endif
