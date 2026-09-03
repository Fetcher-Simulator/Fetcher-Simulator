#ifndef OPENMW_MP_PACKETPLAYERINVENTORYTRANSFERSOUND_HPP
#define OPENMW_MP_PACKETPLAYERINVENTORYTRANSFERSOUND_HPP

#include <stdexcept>

#include <components/openmw-mp/InventoryTransferSound.hpp>
#include <components/openmw-mp/Packets/BasePacket.hpp>

namespace mwmp
{
    class PacketPlayerInventoryTransferSound final : public BasePacket
    {
    public:
        InventoryTransferSound event;

        PacketPlayerInventoryTransferSound()
            : BasePacket(PacketType::InventoryTransferSound)
        {
        }

    protected:
        void pack(WriteStream& stream) override
        {
            stream.write(event.protocolVersion);
            stream.writeString(event.eventId);
            stream.write(event.actorGuid);
            stream.writeString(event.itemRefId);
            stream.write(event.itemInstanceId);
            stream.write(event.itemCount);
            stream.write(event.inventoryRevision);
            stream.write(static_cast<std::uint8_t>(event.mutation));
            stream.write(static_cast<std::uint8_t>(event.direction));
        }

        void unpack(ReadStream& stream) override
        {
            stream.read(event.protocolVersion);
            event.eventId = stream.readString();
            stream.read(event.actorGuid);
            event.itemRefId = stream.readString();
            stream.read(event.itemInstanceId);
            stream.read(event.itemCount);
            stream.read(event.inventoryRevision);
            std::uint8_t mutation = 0;
            std::uint8_t direction = 0;
            stream.read(mutation);
            stream.read(direction);
            event.mutation = static_cast<InventoryTransferMutation>(mutation);
            event.direction = static_cast<InventoryTransferSoundDirection>(direction);
            if (!validateInventoryTransferSound(event) || !stream.eof()
                || mHeader.payloadSize + PacketHeader::WIRE_SIZE != stream.pos())
                throw std::runtime_error("PacketPlayerInventoryTransferSound: invalid payload");
        }
    };
}

#endif
