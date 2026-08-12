#ifndef OPENMW_MP_PACKETPLAYERFACTION_HPP
#define OPENMW_MP_PACKETPLAYERFACTION_HPP

#include <stdexcept>

#include <components/openmw-mp/PlayerFactionState.hpp>

#include "PlayerPacket.hpp"

namespace mwmp
{
    class PacketPlayerFaction : public PlayerPacket
    {
    public:
        enum class Mode : std::uint8_t
        {
            Proposal = 0,
            Result = 1,
        };

        Mode mode = Mode::Result;
        FactionMutationRequest request;
        std::string resultRequestId;
        bool accepted = true;
        FactionError error = FactionError::None;

        PacketPlayerFaction()
            : PlayerPacket(PacketType::PlayerFaction)
        {
        }

    protected:
        void pack(WriteStream& stream) override
        {
            stream.write(mPlayer->guid);
            stream.write(static_cast<std::uint8_t>(mode));
            stream.write(FactionServiceProtocolVersion);
            if (mode == Mode::Proposal)
            {
                stream.writeString(request.requestId);
                stream.write(static_cast<std::uint16_t>(request.mutations.size()));
                for (const FactionMutation& mutation : request.mutations)
                {
                    stream.write(static_cast<std::uint8_t>(mutation.kind));
                    stream.writeString(mutation.factionId);
                    stream.write(mutation.value);
                }
                stream.write(static_cast<std::uint8_t>(request.expectedRevision.has_value() ? 1 : 0));
                if (request.expectedRevision)
                    stream.write(*request.expectedRevision);
                stream.writeString(request.source);
                return;
            }

            stream.writeString(resultRequestId);
            stream.write(static_cast<std::uint8_t>(accepted ? 1 : 0));
            stream.write(static_cast<std::uint16_t>(error));
            stream.write(mPlayer->factionState.schemaVersion);
            stream.write(mPlayer->factionState.revision);
            stream.write(static_cast<std::uint16_t>(mPlayer->factionState.factions.size()));
            for (const PlayerFactionEntry& entry : mPlayer->factionState.factions)
            {
                stream.writeString(entry.factionId);
                stream.write(entry.rank);
                stream.write(entry.reputation);
                stream.write(static_cast<std::uint8_t>(entry.expelled ? 1 : 0));
            }
        }

        void unpack(ReadStream& stream) override
        {
            stream.read(mPlayer->guid);
            std::uint8_t rawMode = 0;
            stream.read(rawMode);
            if (rawMode > static_cast<std::uint8_t>(Mode::Result))
                throw std::runtime_error("PacketPlayerFaction: invalid mode");
            mode = static_cast<Mode>(rawMode);

            std::uint16_t protocolVersion = 0;
            stream.read(protocolVersion);
            if (protocolVersion != FactionServiceProtocolVersion)
                throw std::runtime_error("PacketPlayerFaction: unsupported protocol version");

            if (mode == Mode::Proposal)
            {
                request.protocolVersion = protocolVersion;
                request.requestId = stream.readString();
                std::uint16_t count = 0;
                stream.read(count);
                if (count > MaximumFactionMutations)
                    throw std::runtime_error("PacketPlayerFaction: too many mutations");
                request.mutations.resize(count);
                for (FactionMutation& mutation : request.mutations)
                {
                    std::uint8_t kind = 0;
                    stream.read(kind);
                    mutation.kind = static_cast<FactionMutationKind>(kind);
                    mutation.factionId = stream.readString();
                    stream.read(mutation.value);
                }
                std::uint8_t hasRevision = 0;
                stream.read(hasRevision);
                if (hasRevision > 1)
                    throw std::runtime_error("PacketPlayerFaction: invalid revision flag");
                if (hasRevision)
                {
                    std::uint64_t revision = 0;
                    stream.read(revision);
                    request.expectedRevision = revision;
                }
                else
                    request.expectedRevision.reset();
                request.source = stream.readString();
                if (validateFactionMutationRequest(request) != FactionError::None)
                    throw std::runtime_error("PacketPlayerFaction: invalid proposal");
            }
            else
            {
                resultRequestId = stream.readString();
                std::uint8_t rawAccepted = 0;
                stream.read(rawAccepted);
                if (rawAccepted > 1)
                    throw std::runtime_error("PacketPlayerFaction: invalid acceptance flag");
                accepted = rawAccepted != 0;
                std::uint16_t rawError = 0;
                stream.read(rawError);
                error = static_cast<FactionError>(rawError);
                if (resultRequestId.size() > MaximumSemanticRequestIdLength
                    || getFactionErrorCode(error) == "faction_unknown_error"
                    || (accepted && error != FactionError::None) || (!accepted && error == FactionError::None))
                    throw std::runtime_error("PacketPlayerFaction: invalid result metadata");

                stream.read(mPlayer->factionState.schemaVersion);
                stream.read(mPlayer->factionState.revision);
                std::uint16_t count = 0;
                stream.read(count);
                if (count > MaximumPlayerFactions)
                    throw std::runtime_error("PacketPlayerFaction: too many factions");
                mPlayer->factionState.factions.resize(count);
                for (PlayerFactionEntry& entry : mPlayer->factionState.factions)
                {
                    entry.factionId = stream.readString();
                    stream.read(entry.rank);
                    stream.read(entry.reputation);
                    std::uint8_t expelled = 0;
                    stream.read(expelled);
                    if (expelled > 1)
                        throw std::runtime_error("PacketPlayerFaction: invalid expulsion flag");
                    entry.expelled = expelled != 0;
                }
                if (validatePlayerFactionState(mPlayer->factionState) != FactionError::None)
                    throw std::runtime_error("PacketPlayerFaction: invalid faction state");
            }

            if (!stream.eof() || mHeader.payloadSize + PacketHeader::WIRE_SIZE != stream.pos())
                throw std::runtime_error("PacketPlayerFaction: trailing bytes");
        }
    };
}

#endif
