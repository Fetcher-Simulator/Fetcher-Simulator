#ifndef OPENMW_MP_PACKETWORLDITEMTAKE_HPP
#define OPENMW_MP_PACKETWORLDITEMTAKE_HPP

#include <stdexcept>

#include <components/openmw-mp/Packets/BasePacket.hpp>
#include <components/openmw-mp/WorldItemTake.hpp>

namespace mwmp
{
    inline void writePlacedObjectIdentity(WriteStream& stream, const PlacedObjectIdentity& identity)
    {
        stream.write(static_cast<std::uint8_t>(identity.kind));
        stream.writeString(identity.cellId);
        stream.writeString(identity.refId);
        stream.write(identity.refIndex);
        stream.write(identity.refContentFile);
        stream.write(identity.mpNum);
    }

    inline void readPlacedObjectIdentity(ReadStream& stream, PlacedObjectIdentity& identity)
    {
        std::uint8_t kind = 0;
        stream.read(kind);
        identity.kind = static_cast<PlacedObjectKind>(kind);
        identity.cellId = stream.readString();
        identity.refId = stream.readString();
        stream.read(identity.refIndex);
        stream.read(identity.refContentFile);
        stream.read(identity.mpNum);
    }

    class PacketWorldItemTakeRequest final : public BasePacket
    {
    public:
        WorldItemTakeRequest request;
        PacketWorldItemTakeRequest() : BasePacket(PacketType::WorldItemTakeRequest) {}

    protected:
        void pack(WriteStream& stream) override
        {
            stream.write(request.protocolVersion);
            stream.writeString(request.requestId);
            writePlacedObjectIdentity(stream, request.object);
            stream.write(request.requestedCount);
            stream.write(request.expectedInventoryRevision);
            stream.write(static_cast<std::uint8_t>(request.soundDirection));
        }
        void unpack(ReadStream& stream) override
        {
            stream.read(request.protocolVersion);
            request.requestId = stream.readString();
            readPlacedObjectIdentity(stream, request.object);
            stream.read(request.requestedCount);
            stream.read(request.expectedInventoryRevision);
            std::uint8_t soundDirection = 0;
            stream.read(soundDirection);
            request.soundDirection = static_cast<InventoryTransferSoundDirection>(soundDirection);
            if (validateWorldItemTakeRequest(request) != WorldItemTakeError::None || !stream.eof()
                || mHeader.payloadSize + PacketHeader::WIRE_SIZE != stream.pos())
                throw std::runtime_error("PacketWorldItemTakeRequest: invalid payload");
        }
    };

    class PacketWorldItemTakeResult final : public BasePacket
    {
    public:
        WorldItemTakeResult result;
        PacketWorldItemTakeResult() : BasePacket(PacketType::WorldItemTakeResult) {}

    protected:
        void pack(WriteStream& stream) override
        {
            stream.write(result.protocolVersion);
            stream.writeString(result.requestId);
            stream.write(result.accepted);
            stream.write(result.replayed);
            stream.write(static_cast<std::uint16_t>(result.error));
            writePlacedObjectIdentity(stream, result.object);
            stream.writeString(result.itemRefId);
            stream.write(result.itemCount);
            stream.write(result.crimeValue);
            stream.write(result.theft);
            stream.write(result.inventoryRevision);
        }
        void unpack(ReadStream& stream) override
        {
            stream.read(result.protocolVersion);
            result.requestId = stream.readString();
            stream.read(result.accepted);
            stream.read(result.replayed);
            std::uint16_t error = 0;
            stream.read(error);
            result.error = static_cast<WorldItemTakeError>(error);
            readPlacedObjectIdentity(stream, result.object);
            result.itemRefId = stream.readString();
            stream.read(result.itemCount);
            stream.read(result.crimeValue);
            stream.read(result.theft);
            stream.read(result.inventoryRevision);
        }
    };
}

#endif
