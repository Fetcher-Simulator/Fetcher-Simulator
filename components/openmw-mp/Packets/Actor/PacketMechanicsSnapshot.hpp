#ifndef OPENMW_MP_PACKETMECHANICSSNAPSHOT_HPP
#define OPENMW_MP_PACKETMECHANICSSNAPSHOT_HPP

#include <stdexcept>
#include <utility>

#include <components/openmw-mp/Base/MechanicsSnapshot.hpp>
#include <components/openmw-mp/Packets/BasePacket.hpp>

namespace mwmp
{
    class PacketMechanicsSnapshot : public BasePacket
    {
    public:
        PacketMechanicsSnapshot()
            : BasePacket(PacketType::MechanicsSnapshot)
        {
        }

        void setBatch(MechanicsSnapshotBatch* batch) { mBatch = batch; }

    protected:
        void pack(WriteStream& stream) override
        {
            if (mBatch == nullptr || mBatch->snapshots.size() > MaximumMechanicsSnapshotsPerPacket)
                throw std::runtime_error("PacketMechanicsSnapshot: invalid outgoing batch");

            stream.write(mBatch->wireVersion);
            stream.write(static_cast<std::uint16_t>(mBatch->snapshots.size()));
            for (const MechanicsSnapshot& snapshot : mBatch->snapshots)
                packSnapshot(stream, snapshot);
        }

        void unpack(ReadStream& stream) override
        {
            if (mBatch == nullptr)
                throw std::runtime_error("PacketMechanicsSnapshot: missing batch");

            MechanicsSnapshotBatch decoded;
            stream.read(decoded.wireVersion);
            if (decoded.wireVersion != MechanicsSnapshotWireVersion)
                throw std::runtime_error("PacketMechanicsSnapshot: unsupported wire version");

            std::uint16_t count = 0;
            stream.read(count);
            if (count == 0 || count > MaximumMechanicsSnapshotsPerPacket)
                throw std::runtime_error("PacketMechanicsSnapshot: invalid snapshot count");

            decoded.snapshots.resize(count);
            for (MechanicsSnapshot& snapshot : decoded.snapshots)
            {
                unpackSnapshot(stream, snapshot);
                if (!validateMechanicsSnapshot(snapshot))
                    throw std::runtime_error("PacketMechanicsSnapshot: invalid snapshot");
            }

            if (!stream.eof() || mHeader.payloadSize + PacketHeader::WIRE_SIZE != stream.pos())
                throw std::runtime_error("PacketMechanicsSnapshot: malformed payload length or trailing bytes");
            *mBatch = std::move(decoded);
        }

    private:
        static void packSnapshot(WriteStream& stream, const MechanicsSnapshot& snapshot)
        {
            stream.write(static_cast<std::uint8_t>(snapshot.kind));
            stream.write(snapshot.playerGuid);
            stream.write(snapshot.actorInstanceId);
            stream.writeString(snapshot.cellId);
            for (float axis : snapshot.position.pos)
                stream.write(axis);
            for (float axis : snapshot.position.rot)
                stream.write(axis);
            stream.write(snapshot.stateFlags);
            stream.write(snapshot.sneakSkill);
            stream.write(snapshot.agility);
            stream.write(snapshot.luck);
            stream.write(snapshot.fatigueCurrent);
            stream.write(snapshot.fatigueMaximumModified);
            stream.write(snapshot.chameleon);
            stream.write(snapshot.invisibility);
            stream.write(snapshot.blind);
            stream.write(snapshot.witnessStateFlags);
            stream.write(snapshot.effectiveAlarm);
            stream.write(snapshot.effectiveFight);
            stream.write(static_cast<std::uint8_t>(snapshot.combatTargetKind));
            stream.write(snapshot.combatTargetPlayerGuid);
            stream.write(snapshot.combatTargetActorInstanceId);
            stream.write(snapshot.migrationGeneration);
            stream.write(snapshot.authorityGeneration);
            stream.write(snapshot.snapshotSequence);
        }

        static void unpackSnapshot(ReadStream& stream, MechanicsSnapshot& snapshot)
        {
            std::uint8_t kind = 0;
            stream.read(kind);
            snapshot.kind = static_cast<MechanicsSubjectKind>(kind);
            stream.read(snapshot.playerGuid);
            stream.read(snapshot.actorInstanceId);
            snapshot.cellId = stream.readString();
            for (float& axis : snapshot.position.pos)
                stream.read(axis);
            for (float& axis : snapshot.position.rot)
                stream.read(axis);
            stream.read(snapshot.stateFlags);
            stream.read(snapshot.sneakSkill);
            stream.read(snapshot.agility);
            stream.read(snapshot.luck);
            stream.read(snapshot.fatigueCurrent);
            stream.read(snapshot.fatigueMaximumModified);
            stream.read(snapshot.chameleon);
            stream.read(snapshot.invisibility);
            stream.read(snapshot.blind);
            stream.read(snapshot.witnessStateFlags);
            stream.read(snapshot.effectiveAlarm);
            stream.read(snapshot.effectiveFight);
            std::uint8_t combatTargetKind = 0;
            stream.read(combatTargetKind);
            snapshot.combatTargetKind = static_cast<MechanicsSubjectKind>(combatTargetKind);
            stream.read(snapshot.combatTargetPlayerGuid);
            stream.read(snapshot.combatTargetActorInstanceId);
            stream.read(snapshot.migrationGeneration);
            stream.read(snapshot.authorityGeneration);
            stream.read(snapshot.snapshotSequence);
        }

        MechanicsSnapshotBatch* mBatch = nullptr;
    };
}

#endif
