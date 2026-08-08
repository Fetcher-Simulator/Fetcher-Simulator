#ifndef OPENMW_MP_PACKET_ENCHANTING_REQUEST_HPP
#define OPENMW_MP_PACKET_ENCHANTING_REQUEST_HPP

#include <components/openmw-mp/Packets/BasePacket.hpp>
#include <components/openmw-mp/Records/EnchantingProtocol.hpp>

namespace mwmp
{
    class PacketEnchantingRequest : public BasePacket
    {
    public:
        PacketEnchantingRequest()
            : BasePacket(PacketType::EnchantingRequest)
        {
        }

        records::EnchantingRequest request;

    protected:
        void pack(WriteStream& stream) override
        {
            stream.write(request.protocolVersion);
            stream.writeString(request.requestId);
            stream.write(request.inventoryRevision);
            stream.write(request.targetInstanceId);
            stream.write(request.soulGemInstanceId);
            stream.write(request.castStyle);
            stream.writeString(request.itemName);
            stream.write(request.selfEnchanting);
            stream.write(request.enchanterNetId);
            stream.write(static_cast<std::uint32_t>(request.effects.size()));
            for (const records::EnchantingEffectChoice& effect : request.effects)
            {
                stream.writeString(effect.effectId);
                stream.writeString(effect.skillId);
                stream.writeString(effect.attributeId);
                stream.write(effect.range);
                stream.write(effect.magnitudeMin);
                stream.write(effect.magnitudeMax);
                stream.write(effect.duration);
                stream.write(effect.area);
            }
        }

        void unpack(ReadStream& stream) override
        {
            stream.read(request.protocolVersion);
            request.requestId = stream.readString();
            stream.read(request.inventoryRevision);
            stream.read(request.targetInstanceId);
            stream.read(request.soulGemInstanceId);
            stream.read(request.castStyle);
            request.itemName = stream.readString();
            stream.read(request.selfEnchanting);
            stream.read(request.enchanterNetId);
            if (request.castStyle < 0 || request.castStyle > 3)
                throw std::runtime_error("Enchanting cast style is out of range");
            if (request.itemName.size() > records::MaxEnchantingItemNameLength)
                throw std::runtime_error("Enchanting item name is too long");

            std::uint32_t effectCount = 0;
            stream.read(effectCount);
            if (effectCount > records::MaxEnchantingEffects)
                throw std::runtime_error("Too many enchanting effects");
            request.effects.resize(effectCount);
            for (records::EnchantingEffectChoice& effect : request.effects)
            {
                effect.effectId = stream.readString();
                effect.skillId = stream.readString();
                effect.attributeId = stream.readString();
                stream.read(effect.range);
                stream.read(effect.magnitudeMin);
                stream.read(effect.magnitudeMax);
                stream.read(effect.duration);
                stream.read(effect.area);
                if (effect.effectId.size() > records::MaxEnchantingEffectIdLength)
                    throw std::runtime_error("Enchanting effect id is too long");
                if (effect.skillId.size() > records::MaxEnchantingEffectIdLength
                    || effect.attributeId.size() > records::MaxEnchantingEffectIdLength)
                    throw std::runtime_error("Enchanting effect target id is too long");
                if (effect.range < 0 || effect.range > 2)
                    throw std::runtime_error("Enchanting effect range is out of range");
                if (effect.magnitudeMin < 0 || effect.magnitudeMin > records::MaxEnchantingMagnitude
                    || effect.magnitudeMax < 0 || effect.magnitudeMax > records::MaxEnchantingMagnitude)
                    throw std::runtime_error("Enchanting effect magnitude is out of range");
                if (effect.duration < 0 || effect.duration > records::MaxEnchantingDuration)
                    throw std::runtime_error("Enchanting effect duration is out of range");
                if (effect.area < 0 || effect.area > records::MaxEnchantingArea)
                    throw std::runtime_error("Enchanting effect area is out of range");
            }
            if (!stream.eof())
                throw std::runtime_error("Trailing bytes in enchanting request");
        }
    };
}

#endif
