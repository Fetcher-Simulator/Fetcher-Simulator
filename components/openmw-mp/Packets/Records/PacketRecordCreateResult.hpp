#ifndef OPENMW_MP_PACKET_RECORD_CREATE_RESULT_HPP
#define OPENMW_MP_PACKET_RECORD_CREATE_RESULT_HPP

#include <components/openmw-mp/Packets/BasePacket.hpp>
#include <components/openmw-mp/Records/RecordCreateProtocol.hpp>
#include <components/openmw-mp/Records/DynamicRecordTypes.hpp>

namespace mwmp
{
    class PacketRecordCreateResult : public BasePacket
    {
    public:
        PacketRecordCreateResult()
            : BasePacket(PacketType::RecordCreateResult)
        {
        }

        records::RecordCreateResult result;

    protected:
        void pack(WriteStream& stream) override
        {
            stream.write(result.protocolVersion);
            stream.writeString(result.requestId);
            stream.write(result.accepted);
            stream.write(static_cast<std::uint16_t>(result.error));
            stream.write(result.inventoryRevision);
            stream.write(result.commitSequence);
            stream.write(static_cast<std::uint32_t>(result.records.size()));
            for (const records::CreatedRecord& record : result.records)
            {
                stream.writeString(record.temporaryKey);
                stream.writeString(record.recordId);
                stream.write(record.reused);
                stream.writeBytes(record.definition);
            }
        }

        void unpack(ReadStream& stream) override
        {
            stream.read(result.protocolVersion);
            result.requestId = stream.readString();
            stream.read(result.accepted);
            std::uint16_t error = 0;
            stream.read(error);
            result.error = static_cast<records::CreateError>(error);
            stream.read(result.inventoryRevision);
            stream.read(result.commitSequence);
            std::uint32_t count = 0;
            stream.read(count);
            if (count > 64)
                throw std::runtime_error("Too many records in record create result");
            result.records.resize(count);
            for (records::CreatedRecord& record : result.records)
            {
                record.temporaryKey = stream.readString();
                record.recordId = stream.readString();
                stream.read(record.reused);
                record.definition = stream.readBytes(records::MaximumDefinitionBytes);
            }
            if (!stream.eof())
                throw std::runtime_error("Trailing bytes in record create result");
        }
    };
}

#endif
