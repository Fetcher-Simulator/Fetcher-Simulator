#ifndef OPENMW_MP_PACKETINVENTORYPUT_HPP
#define OPENMW_MP_PACKETINVENTORYPUT_HPP

#include <stdexcept>

#include <components/openmw-mp/InventoryPut.hpp>
#include <components/openmw-mp/Packets/BasePacket.hpp>
#include <components/openmw-mp/Packets/Object/PacketInventoryTake.hpp>

namespace mwmp
{
    class PacketInventoryPutRequest final : public BasePacket
    {
    public:
        InventoryPutRequest request;
        PacketInventoryPutRequest() : BasePacket(PacketType::InventoryPutRequest) {}

    protected:
        void pack(WriteStream& stream) override
        {
            stream.write(request.protocolVersion);
            stream.writeString(request.requestId);
            packInventorySource(stream, request.destination);
            stream.writeString(request.itemRefId);
            stream.write(request.itemInstanceId);
            stream.write(request.itemCharge);
            stream.write(request.requestedCount);
            stream.write(request.expectedInventoryRevision);
        }
        void unpack(ReadStream& stream) override
        {
            stream.read(request.protocolVersion);
            request.requestId = stream.readString();
            unpackInventorySource(stream, request.destination);
            request.itemRefId = stream.readString();
            stream.read(request.itemInstanceId);
            stream.read(request.itemCharge);
            stream.read(request.requestedCount);
            stream.read(request.expectedInventoryRevision);
            if (validateInventoryPutRequest(request) != InventoryPutError::None || !stream.eof()
                || mHeader.payloadSize + PacketHeader::WIRE_SIZE != stream.pos())
                throw std::runtime_error("PacketInventoryPutRequest: invalid payload");
        }
    };

    class PacketInventoryPutResult final : public BasePacket
    {
    public:
        InventoryPutResult result;
        PacketInventoryPutResult() : BasePacket(PacketType::InventoryPutResult) {}

    protected:
        void pack(WriteStream& stream) override
        {
            stream.write(result.protocolVersion);
            stream.writeString(result.requestId);
            stream.write(static_cast<std::uint8_t>(result.accepted));
            stream.write(static_cast<std::uint8_t>(result.replayed));
            stream.write(static_cast<std::uint16_t>(result.error));
            packInventorySource(stream, result.destination);
            stream.writeString(result.itemRefId);
            stream.write(result.itemInstanceId);
            stream.write(result.itemCharge);
            stream.write(result.itemCount);
            stream.write(result.inventoryRevision);
        }
        void unpack(ReadStream& stream) override
        {
            stream.read(result.protocolVersion);
            result.requestId = stream.readString();
            std::uint8_t accepted = 0;
            std::uint8_t replayed = 0;
            std::uint16_t error = 0;
            stream.read(accepted);
            stream.read(replayed);
            stream.read(error);
            if (accepted > 1 || replayed > 1)
                throw std::runtime_error("PacketInventoryPutResult: invalid flags");
            result.accepted = accepted != 0;
            result.replayed = replayed != 0;
            result.error = static_cast<InventoryPutError>(error);
            unpackInventorySource(stream, result.destination);
            result.itemRefId = stream.readString();
            stream.read(result.itemInstanceId);
            stream.read(result.itemCharge);
            stream.read(result.itemCount);
            stream.read(result.inventoryRevision);
            if (result.protocolVersion != InventoryPutProtocolVersion || result.requestId.empty()
                || result.requestId.size() > MaximumInventoryPutRequestIdLength
                || getInventoryPutErrorCode(result.error) == "unknown_error"
                || (result.accepted && result.error != InventoryPutError::None)
                || (!result.accepted && result.error == InventoryPutError::None)
                || !stream.eof() || mHeader.payloadSize + PacketHeader::WIRE_SIZE != stream.pos())
                throw std::runtime_error("PacketInventoryPutResult: invalid payload");
        }
    };
}

#endif
