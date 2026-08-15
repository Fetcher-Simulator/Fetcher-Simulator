#ifndef OPENMW_MP_PACKETACTORCOMBATRESULT_HPP
#define OPENMW_MP_PACKETACTORCOMBATRESULT_HPP

#include <components/openmw-mp/CombatEvent.hpp>

#include "ActorPacket.hpp"

namespace mwmp
{
    // A delegated result emitted only by the current authority for the victim.
    // The server accepts it only when it matches a live server-issued proposal.
    class PacketActorCombatResult : public ActorPacket
    {
    public:
        PacketActorCombatResult()
            : ActorPacket(PacketType::ActorCombatResult)
        {
        }

    protected:
        void pack(WriteStream& stream) override
        {
            packBatchHeader(stream);
            stream.write(CombatEventWireVersion);
            stream.write(mActorList->combatEventId);
            stream.write(mActorList->combatVictimActorInstanceId);
            stream.write(mActorList->combatVictimMigrationGeneration);
            stream.write(mActorList->combatVictimAuthorityGeneration);
            stream.write(mActorList->combatResultSequence);
            stream.write(mActorList->combatResultFlags);
            stream.write(mActorList->combatAppliedDamage);
            stream.write(static_cast<std::uint16_t>(mActorList->actors.size()));
            for (const BaseActor& actor : mActorList->actors)
                packActorIdentity(stream, actor);
        }

        void unpack(ReadStream& stream) override
        {
            unpackBatchHeader(stream);
            std::uint16_t version = 0;
            stream.read(version);
            if (version != CombatEventWireVersion)
                throw std::runtime_error("PacketActorCombatResult: unsupported version");
            stream.read(mActorList->combatEventId);
            stream.read(mActorList->combatVictimActorInstanceId);
            stream.read(mActorList->combatVictimMigrationGeneration);
            stream.read(mActorList->combatVictimAuthorityGeneration);
            stream.read(mActorList->combatResultSequence);
            stream.read(mActorList->combatResultFlags);
            stream.read(mActorList->combatAppliedDamage);
            std::uint16_t count = 0;
            stream.read(count);
            if (count != 1)
                throw std::runtime_error("PacketActorCombatResult: expected one victim");
            mActorList->actors.resize(count);
            unpackActorIdentity(stream, mActorList->actors.front());
            if (!validateCombatResultFields(mActorList->combatEventId,
                    mActorList->combatVictimActorInstanceId,
                    mActorList->combatVictimMigrationGeneration,
                    mActorList->combatVictimAuthorityGeneration,
                    mActorList->combatResultSequence, mActorList->combatResultFlags,
                    mActorList->combatAppliedDamage))
                throw std::runtime_error("PacketActorCombatResult: invalid result");
            if (!stream.eof() || mHeader.payloadSize + PacketHeader::WIRE_SIZE != stream.pos())
                throw std::runtime_error("PacketActorCombatResult: malformed payload length or trailing bytes");
        }
    };
}

#endif
