#ifndef OPENMW_MP_PACKET_ALCHEMY_RESULT_HPP
#define OPENMW_MP_PACKET_ALCHEMY_RESULT_HPP

#include <components/openmw-mp/Packets/BasePacket.hpp>
#include <components/openmw-mp/Records/AlchemyProtocol.hpp>

namespace mwmp
{
    class PacketAlchemyResult : public BasePacket
    {
    public:
        PacketAlchemyResult()
            : BasePacket(PacketType::AlchemyResult)
        {
        }

        records::AlchemyResult result;

    protected:
        void pack(WriteStream& stream) override
        {
            stream.write(result.protocolVersion);
            stream.writeString(result.requestId);
            stream.write(result.accepted);
            stream.write(static_cast<std::uint16_t>(result.error));
            stream.write(result.inventoryRevision);
            stream.write(result.commitSequence);
            stream.write(static_cast<std::uint32_t>(result.attempts.size()));
            for (const records::AlchemyAttemptResult& attempt : result.attempts)
            {
                stream.write(attempt.success);
                stream.writeString(attempt.recordId);
                stream.write(attempt.reused);
            }
        }

        void unpack(ReadStream& stream) override
        {
            stream.read(result.protocolVersion);
            result.requestId = stream.readString();
            stream.read(result.accepted);
            std::uint16_t error = 0;
            stream.read(error);
            result.error = static_cast<records::AlchemyError>(error);
            stream.read(result.inventoryRevision);
            stream.read(result.commitSequence);
            std::uint32_t count = 0;
            stream.read(count);
            if (count > records::MaxAlchemyAttempts)
                throw std::runtime_error("Too many alchemy attempts in result");
            result.attempts.resize(count);
            for (records::AlchemyAttemptResult& attempt : result.attempts)
            {
                stream.read(attempt.success);
                attempt.recordId = stream.readString();
                stream.read(attempt.reused);
            }
            if (!stream.eof())
                throw std::runtime_error("Trailing bytes in alchemy result");
        }
    };
}

#endif
