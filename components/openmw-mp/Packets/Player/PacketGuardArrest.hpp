#ifndef OPENMW_MP_PACKETGUARDARREST_HPP
#define OPENMW_MP_PACKETGUARDARREST_HPP

#include <stdexcept>

#include <components/openmw-mp/GuardArrest.hpp>
#include <components/openmw-mp/Packets/BasePacket.hpp>

namespace mwmp
{
    class PacketGuardArrest final : public BasePacket
    {
    public:
        enum class Mode : std::uint8_t
        {
            Request = 0,
            Result = 1,
            Reach = 2,
            Prompt = 3,
        };

        Mode mode = Mode::Request;
        GuardArrestRequest request;
        GuardArrestResult result;
        GuardArrestReach reach;

        PacketGuardArrest()
            : BasePacket(PacketType::GuardArrest)
        {
        }

    protected:
        void pack(WriteStream& stream) override
        {
            stream.write(static_cast<std::uint8_t>(mode));
            if (mode == Mode::Request)
            {
                stream.write(request.protocolVersion);
                stream.writeString(request.requestId);
                stream.write(static_cast<std::uint8_t>(request.action));
                stream.writeString(request.cellId);
                stream.write(request.actorNetId);
                stream.write(request.migrationGeneration);
                stream.write(request.expectedCrimeRevision);
                stream.write(request.expectedInventoryRevision);
                return;
            }
            if (mode == Mode::Result)
            {
                stream.write(result.protocolVersion);
                stream.writeString(result.requestId);
                stream.write(static_cast<std::uint8_t>(result.action));
                stream.write(static_cast<std::uint8_t>(result.accepted ? 1 : 0));
                stream.write(static_cast<std::uint16_t>(result.error));
                stream.write(result.crimeState.schemaVersion);
                stream.write(result.crimeState.bounty);
                stream.write(result.crimeState.currentCrimeId);
                stream.write(result.crimeState.paidCrimeId);
                stream.write(result.crimeState.revision);
                stream.write(result.inventoryRevision);
                stream.write(result.goldPaid);
                stream.write(result.sentenceDays);
                return;
            }

            stream.write(reach.protocolVersion);
            stream.writeString(reach.cellId);
            stream.write(reach.actorNetId);
            stream.write(reach.migrationGeneration);
            stream.write(reach.offenderGuid);
        }

        void unpack(ReadStream& stream) override
        {
            std::uint8_t rawMode = 0;
            stream.read(rawMode);
            if (rawMode > static_cast<std::uint8_t>(Mode::Prompt))
                throw std::runtime_error("PacketGuardArrest: invalid mode");
            mode = static_cast<Mode>(rawMode);

            if (mode == Mode::Request)
            {
                stream.read(request.protocolVersion);
                request.requestId = stream.readString();
                std::uint8_t rawAction = 0;
                stream.read(rawAction);
                request.action = static_cast<GuardArrestAction>(rawAction);
                request.cellId = stream.readString();
                stream.read(request.actorNetId);
                stream.read(request.migrationGeneration);
                stream.read(request.expectedCrimeRevision);
                stream.read(request.expectedInventoryRevision);
                if (!validateGuardArrestRequest(request))
                    throw std::runtime_error("PacketGuardArrest: invalid request");
            }
            else if (mode == Mode::Result)
            {
                stream.read(result.protocolVersion);
                result.requestId = stream.readString();
                std::uint8_t rawAction = 0;
                stream.read(rawAction);
                result.action = static_cast<GuardArrestAction>(rawAction);
                std::uint8_t accepted = 0;
                stream.read(accepted);
                if (accepted > 1)
                    throw std::runtime_error("PacketGuardArrest: invalid acceptance flag");
                result.accepted = accepted != 0;
                std::uint16_t rawError = 0;
                stream.read(rawError);
                result.error = static_cast<GuardArrestError>(rawError);
                stream.read(result.crimeState.schemaVersion);
                stream.read(result.crimeState.bounty);
                stream.read(result.crimeState.currentCrimeId);
                stream.read(result.crimeState.paidCrimeId);
                stream.read(result.crimeState.revision);
                stream.read(result.inventoryRevision);
                stream.read(result.goldPaid);
                stream.read(result.sentenceDays);
                if (!validateGuardArrestResult(result))
                    throw std::runtime_error("PacketGuardArrest: invalid result");
            }
            else
            {
                stream.read(reach.protocolVersion);
                reach.cellId = stream.readString();
                stream.read(reach.actorNetId);
                stream.read(reach.migrationGeneration);
                stream.read(reach.offenderGuid);
                if (!validateGuardArrestReach(reach))
                    throw std::runtime_error("PacketGuardArrest: invalid reach/prompt");
            }

            if (!stream.eof() || mHeader.payloadSize + PacketHeader::WIRE_SIZE != stream.pos())
                throw std::runtime_error("PacketGuardArrest: trailing payload");
        }
    };
}

#endif
