#pragma once
#include <components/openmw-mp/Packets/BasePacket.hpp>
#include <components/openmw-mp/Records/SpellmakingProtocol.hpp>

namespace mwmp
{
    class PacketSpellmakingResult : public BasePacket
    {
    public:
        PacketSpellmakingResult()
            : BasePacket(PacketType::SpellmakingResult)
        {
        }
        records::SpellmakingResult result;

    protected:
        void pack(WriteStream& stream) override
        {
            stream.write(result.protocolVersion);
            stream.writeString(result.requestId);
            stream.write(result.accepted);
            stream.write(static_cast<std::uint16_t>(result.error));
            stream.write(result.inventoryRevision);
            stream.write(result.spellbookRevision);
            stream.write(result.commitSequence);
            stream.writeString(result.recordId);
            stream.write(result.price);
        }
        void unpack(ReadStream& stream) override
        {
            if (stream.remaining() > 512)
                throw std::runtime_error("Oversized spellmaking payload");
            stream.read(result.protocolVersion);
            result.requestId = stream.readString();
            stream.read(result.accepted);
            std::uint16_t error = 0;
            stream.read(error);
            result.error = static_cast<records::SpellmakingError>(error);
            stream.read(result.inventoryRevision);
            stream.read(result.spellbookRevision);
            stream.read(result.commitSequence);
            result.recordId = stream.readString();
            stream.read(result.price);
            if (result.requestId.empty() || result.requestId.size() > 128 || !stream.eof())
                throw std::runtime_error("Invalid spellmaking payload");
            if (result.protocolVersion != records::SpellmakingProtocolVersion || result.recordId.size() > 128
                || error > static_cast<std::uint16_t>(records::SpellmakingError::ServerError))
                throw std::runtime_error("Invalid spellmaking result");
        }
    };
}
