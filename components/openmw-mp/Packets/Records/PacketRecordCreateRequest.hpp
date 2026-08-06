#ifndef OPENMW_MP_PACKET_RECORD_CREATE_REQUEST_HPP
#define OPENMW_MP_PACKET_RECORD_CREATE_REQUEST_HPP

#include <components/openmw-mp/Packets/BasePacket.hpp>
#include <components/openmw-mp/Records/DynamicRecordCodec.hpp>
#include <components/openmw-mp/Records/RecordCreateProtocol.hpp>

namespace mwmp
{
    class PacketRecordCreateRequest : public BasePacket
    {
    public:
        PacketRecordCreateRequest()
            : BasePacket(PacketType::RecordCreateRequest)
        {
        }

        records::RecordCreateRequest request;

    protected:
        void pack(WriteStream& stream) override
        {
            stream.write(request.protocolVersion);
            stream.writeString(request.requestId);
            stream.write(static_cast<std::uint8_t>(request.operation));
            stream.write(request.inventoryRevision);
            stream.writeString(request.scriptPackageId);
            stream.writeBytes(records::encodeBundle(request.bundle));
            stream.writeBytes(request.evidence);
        }

        void unpack(ReadStream& stream) override
        {
            stream.read(request.protocolVersion);
            request.requestId = stream.readString();
            std::uint8_t operation = 0;
            stream.read(operation);
            request.operation = static_cast<records::CreateOperation>(operation);
            stream.read(request.inventoryRevision);
            request.scriptPackageId = stream.readString();
            request.bundle = records::decodeBundle(stream.readBytes());
            request.evidence = stream.readBytes();
            if (!stream.eof())
                throw std::runtime_error("Trailing bytes in record create request");
        }
    };
}

#endif
