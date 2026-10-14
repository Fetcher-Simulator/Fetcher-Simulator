#ifndef OPENMW_MP_PACKETACTORAUTHORITYTRANSFER_HPP
#define OPENMW_MP_PACKETACTORAUTHORITYTRANSFER_HPP

#include <components/openmw-mp/Base/ActorAuthorityTransfer.hpp>

#include "ActorPacket.hpp"

namespace mwmp
{
    class PacketActorAuthorityTransfer : public ActorPacket
    {
    public:
        PacketActorAuthorityTransfer()
            : ActorPacket(PacketType::ActorAuthorityTransfer)
        {
        }

        void setTransfer(ActorAuthorityTransfer* transfer) { mTransfer = transfer; }

    protected:
        void pack(WriteStream& ws) override
        {
            ws.write(static_cast<std::uint8_t>(mTransfer->phase));
            ws.write(mTransfer->actorNetId);
            ws.write(mTransfer->transferId);
            ws.write(mTransfer->oldOwnerGuid);
            ws.write(mTransfer->newOwnerGuid);
            ws.write(mTransfer->authorityGeneration);
            ws.write(mTransfer->migrationGeneration);
            ws.writeString(mTransfer->cellId);
            if (mTransfer->phase != ActorAuthorityTransferPhase::Prepare
                && mTransfer->phase != ActorAuthorityTransferPhase::Commit)
                return;

            const BaseActor& actor = mTransfer->actor;
            packActorIdentity(ws, actor);
            packPosition(ws, actor.position);
            packVelocity(ws, actor.velocity);
            packActorPresentation(ws, actor);
            packDynamicStats(ws, actor.dynamicStats);
            packAnimFlags(ws, actor.animFlags);
            packAnimPlay(ws, actor.animPlay);
            packAttack(ws, actor.attack);
            packCast(ws, actor.cast);
            packEquipment(ws, actor.equipment);
            packAI(ws, actor.ai);
            ws.write(actor.deathState);
            ws.write(actor.deathEventId);
            ws.write(actor.deathCauseCombatEventId);
            ws.write(actor.isDead);
            ws.write(actor.isInstantDeath);
            ws.writeString(actor.deathAnimGroup);
            ws.write(actor.isFollowerCellChange);
            packTimedSpells(ws, mTransfer->timedSpells);
        }

        void unpack(ReadStream& rs) override
        {
            std::uint8_t phase = 0;
            rs.read(phase);
            mTransfer->phase = static_cast<ActorAuthorityTransferPhase>(phase);
            rs.read(mTransfer->actorNetId);
            rs.read(mTransfer->transferId);
            rs.read(mTransfer->oldOwnerGuid);
            rs.read(mTransfer->newOwnerGuid);
            rs.read(mTransfer->authorityGeneration);
            rs.read(mTransfer->migrationGeneration);
            mTransfer->cellId = rs.readString();
            if (mTransfer->phase != ActorAuthorityTransferPhase::Prepare
                && mTransfer->phase != ActorAuthorityTransferPhase::Commit)
                return;

            BaseActor& actor = mTransfer->actor;
            unpackActorIdentity(rs, actor);
            unpackPosition(rs, actor.position);
            unpackVelocity(rs, actor.velocity);
            unpackActorPresentation(rs, actor);
            unpackDynamicStats(rs, actor.dynamicStats);
            unpackAnimFlags(rs, actor.animFlags);
            unpackAnimPlay(rs, actor.animPlay);
            unpackAttack(rs, actor.attack);
            unpackCast(rs, actor.cast);
            unpackEquipment(rs, actor.equipment);
            unpackAI(rs, actor.ai);
            rs.read(actor.deathState);
            rs.read(actor.deathEventId);
            rs.read(actor.deathCauseCombatEventId);
            rs.read(actor.isDead);
            rs.read(actor.isInstantDeath);
            actor.deathAnimGroup = rs.readString();
            rs.read(actor.isFollowerCellChange);
            unpackTimedSpells(rs, mTransfer->timedSpells);
        }

    private:
        ActorAuthorityTransfer* mTransfer = nullptr;
    };
}

#endif
