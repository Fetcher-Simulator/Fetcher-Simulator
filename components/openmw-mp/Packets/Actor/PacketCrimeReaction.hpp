#ifndef OPENMW_COMPONENTS_OPENMW_MP_PACKETS_ACTOR_PACKETCRIMEREACTION_HPP
#define OPENMW_COMPONENTS_OPENMW_MP_PACKETS_ACTOR_PACKETCRIMEREACTION_HPP

#include <stdexcept>

#include <components/openmw-mp/CrimeReaction.hpp>
#include <components/openmw-mp/Packets/BasePacket.hpp>

namespace mwmp
{
    class PacketCrimeReaction final : public BasePacket
    {
    public:
        CrimeReactionDirective directive;

        PacketCrimeReaction()
            : BasePacket(PacketType::CrimeReaction)
        {
        }

    protected:
        void pack(WriteStream& stream) override
        {
            stream.write(directive.protocolVersion);
            stream.writeString(directive.eventId);
            stream.writeString(directive.cellId);
            stream.write(directive.offenderGuid);
            stream.write(static_cast<std::uint16_t>(directive.actors.size()));
            for (const CrimeActorReaction& actor : directive.actors)
            {
                stream.write(actor.actorNetId);
                stream.write(actor.migrationGeneration);
                stream.write(static_cast<std::uint8_t>(actor.dialogue));
                stream.write(actor.flags);
            }
        }

        void unpack(ReadStream& stream) override
        {
            stream.read(directive.protocolVersion);
            directive.eventId = stream.readString();
            directive.cellId = stream.readString();
            stream.read(directive.offenderGuid);
            std::uint16_t count = 0;
            stream.read(count);
            if (count == 0 || count > MaximumCrimeReactionActors)
                throw std::runtime_error("PacketCrimeReaction: invalid actor count");
            directive.actors.resize(count);
            for (CrimeActorReaction& actor : directive.actors)
            {
                stream.read(actor.actorNetId);
                stream.read(actor.migrationGeneration);
                std::uint8_t dialogue = 0;
                stream.read(dialogue);
                actor.dialogue = static_cast<CrimeReactionDialogue>(dialogue);
                stream.read(actor.flags);
            }
            if (!validateCrimeReactionDirective(directive) || !stream.eof()
                || mHeader.payloadSize + PacketHeader::WIRE_SIZE != stream.pos())
                throw std::runtime_error("PacketCrimeReaction: invalid payload");
        }
    };
}

#endif
