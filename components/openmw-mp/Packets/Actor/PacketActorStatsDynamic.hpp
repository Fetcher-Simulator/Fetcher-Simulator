#ifndef OPENMW_MP_PACKETACTORSTATSDYNAMIC_HPP
#define OPENMW_MP_PACKETACTORSTATSDYNAMIC_HPP

#include "ActorPacket.hpp"

namespace mwmp
{
    class PacketActorStatsDynamic : public ActorPacket
    {
    public:
        PacketActorStatsDynamic()
            : ActorPacket(PacketType::ActorStatsDynamic)
        {
        }

    protected:
        void pack(WriteStream& ws) override
        {
            packBatchHeader(ws);
            const auto count = static_cast<uint16_t>(mActorList->actors.size());
            ws.write(count);
            for (const auto& actor : mActorList->actors)
            {
                packActorIdentity(ws, actor);
                packDynamicStats(ws, actor.dynamicStats);
                ws.write(actor.isDead);
                ws.write(actor.hasTimedSpellSnapshot);
                if (actor.hasTimedSpellSnapshot)
                    packTimedSpells(ws, actor.timedSpells);
            }
        }

        void unpack(ReadStream& rs) override
        {
            unpackBatchHeader(rs);
            uint16_t count = 0;
            rs.read(count);
            mActorList->actors.resize(count);
            for (auto& actor : mActorList->actors)
            {
                unpackActorIdentity(rs, actor);
                unpackDynamicStats(rs, actor.dynamicStats);
                rs.read(actor.isDead);
                rs.read(actor.hasTimedSpellSnapshot);
                if (actor.hasTimedSpellSnapshot)
                    unpackTimedSpells(rs, actor.timedSpells);
            }
        }
    };
}

#endif // OPENMW_MP_PACKETACTORSTATSDYNAMIC_HPP
