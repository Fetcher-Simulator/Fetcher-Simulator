#ifndef OPENMW_MP_PACKETPLAYERVEHICLESTATE_HPP
#define OPENMW_MP_PACKETPLAYERVEHICLESTATE_HPP

#include "PlayerPacket.hpp"

namespace mwmp
{
    class PacketPlayerVehicleState : public PlayerPacket
    {
    public:
        PacketPlayerVehicleState()
            : PlayerPacket(PacketType::PlayerVehicleState)
        {
        }

    protected:
        void pack(WriteStream& ws) override
        {
            ws.write(mPlayer->guid);
            ws.write(mPlayer->vehicle.active);
            ws.write(mPlayer->vehicle.revision);
            ws.write(mPlayer->vehicle.parkedObjectMpNum);
            ws.writeString(mPlayer->vehicle.profileId);
        }

        void unpack(ReadStream& rs) override
        {
            rs.read(mPlayer->guid);
            rs.read(mPlayer->vehicle.active);
            rs.read(mPlayer->vehicle.revision);
            rs.read(mPlayer->vehicle.parkedObjectMpNum);
            mPlayer->vehicle.profileId = rs.readString();
        }
    };
}

#endif // OPENMW_MP_PACKETPLAYERVEHICLESTATE_HPP
