#pragma once
#include <components/openmw-mp/Packets/BasePacket.hpp>
#include <components/openmw-mp/Records/SpellmakingProtocol.hpp>

namespace mwmp
{
    class PacketSpellmakingRequest : public BasePacket
    {
    public:
        PacketSpellmakingRequest()
            : BasePacket(PacketType::SpellmakingRequest)
        {
        }
        records::SpellmakingRequest request;

    protected:
        void pack(WriteStream& stream) override
        {
            stream.write(request.protocolVersion);
            stream.writeString(request.requestId);
            stream.write(request.inventoryRevision);
            stream.write(request.spellbookRevision);
            stream.write(request.actorNetId);
            stream.writeString(request.name);
            stream.write(request.maximumPrice);
            stream.write(static_cast<std::uint32_t>(request.effects.size()));
            for (const auto& effect : request.effects)
            {
                stream.writeString(effect.effectId);
                stream.writeString(effect.skillId);
                stream.writeString(effect.attributeId);
                stream.write(effect.range);
                stream.write(effect.area);
                stream.write(effect.duration);
                stream.write(effect.magnitudeMin);
                stream.write(effect.magnitudeMax);
            }
        }
        void unpack(ReadStream& stream) override
        {
            if (stream.remaining() > 4096)
                throw std::runtime_error("Oversized spellmaking payload");
            stream.read(request.protocolVersion);
            request.requestId = stream.readString();
            stream.read(request.inventoryRevision);
            stream.read(request.spellbookRevision);
            stream.read(request.actorNetId);
            request.name = stream.readString();
            stream.read(request.maximumPrice);
            std::uint32_t count = 0;
            stream.read(count);
            if (count > records::MaxSpellmakingEffects)
                throw std::runtime_error("Too many spellmaking effects");
            request.effects.resize(count);
            for (auto& effect : request.effects)
            {
                effect.effectId = stream.readString();
                effect.skillId = stream.readString();
                effect.attributeId = stream.readString();
                stream.read(effect.range);
                stream.read(effect.area);
                stream.read(effect.duration);
                stream.read(effect.magnitudeMin);
                stream.read(effect.magnitudeMax);
                if (effect.effectId.size() > 128 || effect.skillId.size() > 128 || effect.attributeId.size() > 128)
                    throw std::runtime_error("Spellmaking effect ID too long");
            }
            if (request.requestId.empty() || request.requestId.size() > 128 || !stream.eof())
                throw std::runtime_error("Invalid spellmaking payload");
            if (request.name.empty() || request.name.size() > 64)
                throw std::runtime_error("Invalid spell name");
        }
    };
}
