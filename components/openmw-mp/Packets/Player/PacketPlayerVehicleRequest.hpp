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
    };

    // Client -> server request. The server derives and validates the vehicle
    // profile from the parked object's authoritative refId.
    class PacketPlayerVehicleRequest : public BasePacket
    {
    public:
        VehicleRequestAction action = VehicleRequestAction::Enter;
        uint32_t parkedObjectMpNum = 0;

        PacketPlayerVehicleRequest()
            : BasePacket(PacketType::PlayerVehicleRequest)
        {
        }

    protected:
        void pack(WriteStream& ws) override
        {
            ws.write(static_cast<uint8_t>(action));
            ws.write(parkedObjectMpNum);
        }

        void unpack(ReadStream& rs) override
        {
            uint8_t rawAction = 0;
            rs.read(rawAction);
            action = static_cast<VehicleRequestAction>(rawAction);
            rs.read(parkedObjectMpNum);
        }
    };
}

#endif // OPENMW_MP_PACKETPLAYERVEHICLEREQUEST_HPP
