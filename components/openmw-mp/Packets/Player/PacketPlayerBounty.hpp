#ifndef OPENMW_MP_PACKETPLAYERBOUNTY_HPP
#define OPENMW_MP_PACKETPLAYERBOUNTY_HPP

#include <stdexcept>

#include <components/openmw-mp/PlayerCrimeState.hpp>

#include "PlayerPacket.hpp"

namespace mwmp
{
    /// Server-authored semantic player crime state. Installing this packet
    /// mirrors state; it never executes the script or gameplay cause that
    /// produced the transition.
    class PacketPlayerBounty : public PlayerPacket
    {
    public:
        enum class Mode : std::uint8_t
        {
            Proposal = 0,
            Result = 1,
        };

        Mode mode = Mode::Result;
        CrimeMutationRequest request;
        std::string resultRequestId;
        bool accepted = true;
        CrimeError error = CrimeError::None;

        PacketPlayerBounty()
            : PlayerPacket(PacketType::PlayerBounty)
        {
        }

    protected:
        void pack(WriteStream& stream) override
        {
            stream.write(mPlayer->guid);
            stream.write(static_cast<std::uint8_t>(mode));
            stream.write(CrimeServiceProtocolVersion);
            if (mode == Mode::Proposal)
            {
                stream.writeString(request.requestId);
                stream.write(static_cast<std::uint8_t>(request.kind));
                stream.write(request.value);
                stream.write(static_cast<std::uint8_t>(request.expectedRevision.has_value() ? 1 : 0));
                if (request.expectedRevision)
                    stream.write(*request.expectedRevision);
                stream.writeString(request.source);
                return;
            }

            stream.writeString(resultRequestId);
            stream.write(static_cast<std::uint8_t>(accepted ? 1 : 0));
            stream.write(static_cast<std::uint16_t>(error));
            stream.write(mPlayer->crimeState.schemaVersion);
            stream.write(mPlayer->crimeState.revision);
            stream.write(mPlayer->crimeState.bounty);
            stream.write(mPlayer->crimeState.currentCrimeId);
            stream.write(mPlayer->crimeState.paidCrimeId);
        }

        void unpack(ReadStream& stream) override
        {
            stream.read(mPlayer->guid);
            std::uint8_t rawMode = 0;
            stream.read(rawMode);
            if (rawMode > static_cast<std::uint8_t>(Mode::Result))
                throw std::runtime_error("PacketPlayerBounty: invalid mode");
            mode = static_cast<Mode>(rawMode);

            std::uint16_t protocolVersion = 0;
            stream.read(protocolVersion);
            if (protocolVersion != CrimeServiceProtocolVersion)
                throw std::runtime_error("PacketPlayerBounty: unsupported protocol version");

            if (mode == Mode::Proposal)
            {
                request.protocolVersion = protocolVersion;
                request.requestId = stream.readString();
                std::uint8_t rawKind = 0;
                stream.read(rawKind);
                request.kind = static_cast<CrimeMutationKind>(rawKind);
                stream.read(request.value);
                std::uint8_t hasRevision = 0;
                stream.read(hasRevision);
                if (hasRevision > 1)
                    throw std::runtime_error("PacketPlayerBounty: invalid revision flag");
                if (hasRevision)
                {
                    std::uint64_t revision = 0;
                    stream.read(revision);
                    request.expectedRevision = revision;
                }
                else
                    request.expectedRevision.reset();
                request.source = stream.readString();
                if (validateCrimeMutationRequest(request) != CrimeError::None)
                    throw std::runtime_error("PacketPlayerBounty: invalid proposal");
            }
            else
            {
                resultRequestId = stream.readString();
                std::uint8_t rawAccepted = 0;
                stream.read(rawAccepted);
                if (rawAccepted > 1)
                    throw std::runtime_error("PacketPlayerBounty: invalid acceptance flag");
                accepted = rawAccepted != 0;
                std::uint16_t rawError = 0;
                stream.read(rawError);
                error = static_cast<CrimeError>(rawError);
                if (resultRequestId.size() > MaximumSemanticRequestIdLength
                    || getCrimeErrorCode(error) == "crime_unknown_error"
                    || (accepted && error != CrimeError::None) || (!accepted && error == CrimeError::None))
                    throw std::runtime_error("PacketPlayerBounty: invalid result metadata");

                stream.read(mPlayer->crimeState.schemaVersion);
                stream.read(mPlayer->crimeState.revision);
                stream.read(mPlayer->crimeState.bounty);
                stream.read(mPlayer->crimeState.currentCrimeId);
                stream.read(mPlayer->crimeState.paidCrimeId);
                if (validatePlayerCrimeState(mPlayer->crimeState) != CrimeError::None)
                    throw std::runtime_error("PacketPlayerBounty: invalid crime state");
            }
            if (!stream.eof()
                || mHeader.payloadSize + PacketHeader::WIRE_SIZE != stream.pos())
                throw std::runtime_error("PacketPlayerBounty: malformed payload length or trailing bytes");
        }
    };
}

#endif
