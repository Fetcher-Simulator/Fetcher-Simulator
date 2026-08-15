#ifndef OPENMW_MP_PACKETCRIMEINTERACTION_HPP
#define OPENMW_MP_PACKETCRIMEINTERACTION_HPP

#include <stdexcept>

#include <components/openmw-mp/CrimeInteraction.hpp>
#include <components/openmw-mp/Packets/BasePacket.hpp>

namespace mwmp
{
    class PacketCrimeInteraction final : public BasePacket
    {
    public:
        CrimeInteractionRequest request;

        PacketCrimeInteraction() : BasePacket(PacketType::CrimeInteractionRequest) {}

    protected:
        void pack(WriteStream& stream) override
        {
            stream.write(request.protocolVersion);
            stream.writeString(request.requestId);
            stream.write(static_cast<std::uint8_t>(request.kind));
            stream.writeString(request.cellId);
            stream.writeString(request.refId);
            stream.write(request.refNum);
            stream.write(request.refContentFile);
        }

        void unpack(ReadStream& stream) override
        {
            stream.read(request.protocolVersion);
            request.requestId = stream.readString();
            std::uint8_t kind = 0;
            stream.read(kind);
            request.kind = static_cast<CrimeInteractionKind>(kind);
            request.cellId = stream.readString();
            request.refId = stream.readString();
            stream.read(request.refNum);
            stream.read(request.refContentFile);
            if (!validateCrimeInteractionRequest(request) || !stream.eof()
                || mHeader.payloadSize + PacketHeader::WIRE_SIZE != stream.pos())
                throw std::runtime_error("PacketCrimeInteraction: invalid payload");
        }
    };
}

#endif
