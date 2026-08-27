#ifndef OPENMW_MP_PACKETOBJECTCOUNT_HPP
#define OPENMW_MP_PACKETOBJECTCOUNT_HPP

#include <components/openmw-mp/Packets/BasePacket.hpp>
#include <components/openmw-mp/WorldItemTake.hpp>

namespace mwmp
{
    // Server -> client authoritative count mutation for an existing placed
    // world item. Count zero uses PacketObjectDelete instead.
    class PacketObjectCount : public BasePacket
    {
    public:
        PlacedObjectIdentity object;
        std::int32_t count = 0;

        PacketObjectCount() : BasePacket(PacketType::ObjectCount) {}

    protected:
        void pack(WriteStream& stream) override
        {
            stream.write(static_cast<std::uint8_t>(object.kind));
            stream.writeString(object.cellId);
            stream.writeString(object.refId);
            stream.write(object.refIndex);
            stream.write(object.refContentFile);
            stream.write(object.mpNum);
            stream.write(count);
        }

        void unpack(ReadStream& stream) override
        {
            std::uint8_t kind = 0;
            stream.read(kind);
            object.kind = static_cast<PlacedObjectKind>(kind);
            object.cellId = stream.readString();
            object.refId = stream.readString();
            stream.read(object.refIndex);
            stream.read(object.refContentFile);
            stream.read(object.mpNum);
            stream.read(count);
        }
    };
}

#endif
