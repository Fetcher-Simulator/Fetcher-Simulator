#ifndef OPENMW_MP_PACKETPLAYERVEHICLEREQUEST_HPP
#define OPENMW_MP_PACKETPLAYERVEHICLEREQUEST_HPP

#include <cstdint>

#include <components/openmw-mp/Packets/BasePacket.hpp>

namespace mwmp
{
    enum class VehicleRequestAction : uint8_t
    {
        Enter = 0,
        Exit = 1,
        EnterPassenger = 2,
    };

    inline constexpr uint8_t sAutomaticVehicleSeat = 0xff;

    // Client -> server request. Parked entry derives the profile from the
    // authoritative world object; passenger entry names an active driver and
    // lets the server validate and allocate the requested profile seat.
    class PacketPlayerVehicleRequest : public BasePacket
    {
    public:
        VehicleRequestAction action = VehicleRequestAction::Enter;
        uint32_t parkedObjectMpNum = 0;
        uint32_t driverGuid = 0;
        uint8_t seatIndex = sAutomaticVehicleSeat;

        PacketPlayerVehicleRequest()
            : BasePacket(PacketType::PlayerVehicleRequest)
        {
        }

    protected:
        void pack(WriteStream& ws) override
        {
            ws.write(static_cast<uint8_t>(action));
            ws.write(parkedObjectMpNum);
            ws.write(driverGuid);
            ws.write(seatIndex);
        }

        void unpack(ReadStream& rs) override
        {
            uint8_t rawAction = 0;
            rs.read(rawAction);
            action = static_cast<VehicleRequestAction>(rawAction);
            rs.read(parkedObjectMpNum);
            if (rs.remaining() >= sizeof(driverGuid) + sizeof(seatIndex))
            {
                rs.read(driverGuid);
                rs.read(seatIndex);
            }
        }
    };
}

#endif // OPENMW_MP_PACKETPLAYERVEHICLEREQUEST_HPP
