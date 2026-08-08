#ifndef OPENMW_MP_PACKET_ENCHANTING_RESULT_HPP
#define OPENMW_MP_PACKET_ENCHANTING_RESULT_HPP

#include <components/openmw-mp/Packets/BasePacket.hpp>
#include <components/openmw-mp/Records/EnchantingProtocol.hpp>

namespace mwmp
{
    class PacketEnchantingResult : public BasePacket
    {
    public:
        PacketEnchantingResult()
            : BasePacket(PacketType::EnchantingResult)
        {
        }

        records::EnchantingResult result;

    protected:
        void pack(WriteStream& stream) override
        {
            stream.write(result.protocolVersion);
            stream.writeString(result.requestId);
            stream.write(result.accepted);
            stream.write(static_cast<std::uint16_t>(result.error));
            stream.write(result.success);
            stream.write(result.inventoryRevision);
            stream.write(result.commitSequence);
            stream.writeString(result.enchantmentRecordId);
            stream.writeString(result.itemRecordId);
            stream.write(result.enchantmentReused);
            stream.write(result.itemReused);
        }

        void unpack(ReadStream& stream) override
        {
            stream.read(result.protocolVersion);
            result.requestId = stream.readString();
            stream.read(result.accepted);
            std::uint16_t error = 0;
            stream.read(error);
            result.error = static_cast<records::EnchantingError>(error);
            stream.read(result.success);
            stream.read(result.inventoryRevision);
            stream.read(result.commitSequence);
            result.enchantmentRecordId = stream.readString();
            result.itemRecordId = stream.readString();
            stream.read(result.enchantmentReused);
            stream.read(result.itemReused);
            if (!stream.eof())
                throw std::runtime_error("Trailing bytes in enchanting result");
            if (result.requestId.size() > 128)
                throw std::runtime_error("Enchanting result request id is too long");
            if (result.enchantmentRecordId.size() > records::MaxEnchantingEffectIdLength)
                throw std::runtime_error("Enchanting result record id is too long");
            if (result.itemRecordId.size() > records::MaxEnchantingEffectIdLength)
                throw std::runtime_error("Enchanting result record id is too long");
        }
    };
}

#endif
