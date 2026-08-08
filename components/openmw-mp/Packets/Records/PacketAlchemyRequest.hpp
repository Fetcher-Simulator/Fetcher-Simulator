#ifndef OPENMW_MP_PACKET_ALCHEMY_REQUEST_HPP
#define OPENMW_MP_PACKET_ALCHEMY_REQUEST_HPP

#include <components/openmw-mp/Packets/BasePacket.hpp>
#include <components/openmw-mp/Records/AlchemyProtocol.hpp>

namespace mwmp
{
    class PacketAlchemyRequest : public BasePacket
    {
    public:
        PacketAlchemyRequest()
            : BasePacket(PacketType::AlchemyRequest)
        {
        }

        records::AlchemyRequest request;

    protected:
        void pack(WriteStream& stream) override
        {
            stream.write(request.protocolVersion);
            stream.writeString(request.requestId);
            stream.write(request.inventoryRevision);
            stream.writeString(request.potionName);
            stream.write(request.count);
            stream.writeVector(request.ingredientInstanceIds);
            stream.writeVector(request.apparatusInstanceIds);
        }

        void unpack(ReadStream& stream) override
        {
            stream.read(request.protocolVersion);
            request.requestId = stream.readString();
            stream.read(request.inventoryRevision);
            request.potionName = stream.readString();
            stream.read(request.count);
            readInstanceIds(stream, request.ingredientInstanceIds, records::MaxAlchemyIngredients);
            readInstanceIds(stream, request.apparatusInstanceIds, records::MaxAlchemyApparatus);
            if (!stream.eof())
                throw std::runtime_error("Trailing bytes in alchemy request");
            if (request.potionName.size() > records::MaxAlchemyPotionNameLength)
                throw std::runtime_error("Alchemy potion name is too long");
        }

    private:
        static void readInstanceIds(
            ReadStream& stream, std::vector<std::uint32_t>& out, std::size_t maximum)
        {
            std::uint32_t count = 0;
            stream.read(count);
            if (count > maximum)
                throw std::runtime_error("Too many alchemy source instances");
            out.resize(count);
            for (std::uint32_t& id : out)
                stream.read(id);
        }
    };
}

#endif
