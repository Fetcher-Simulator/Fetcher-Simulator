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
            ws.write(static_cast<uint8_t>(mPlayer->vehicle.occupantRole));
            ws.write(mPlayer->vehicle.driverGuid);
            ws.write(mPlayer->vehicle.seatIndex);
        }

        void unpack(ReadStream& rs) override
        {
            rs.read(mPlayer->guid);
            rs.read(mPlayer->vehicle.active);
            rs.read(mPlayer->vehicle.revision);
            rs.read(mPlayer->vehicle.parkedObjectMpNum);
            mPlayer->vehicle.profileId = rs.readString();
            if (rs.remaining() >= sizeof(uint8_t) + sizeof(uint32_t) + sizeof(uint8_t))
            {
                uint8_t occupantRole = 0;
                rs.read(occupantRole);
                mPlayer->vehicle.occupantRole = static_cast<VehicleOccupantRole>(occupantRole);
                rs.read(mPlayer->vehicle.driverGuid);
                rs.read(mPlayer->vehicle.seatIndex);
            }
            else if (mPlayer->vehicle.active)
            {
                mPlayer->vehicle.occupantRole = VehicleOccupantRole::Driver;
                mPlayer->vehicle.driverGuid = mPlayer->guid;
                mPlayer->vehicle.seatIndex = 0;
            }
        }
    };
}

#endif // OPENMW_MP_PACKETPLAYERVEHICLESTATE_HPP
