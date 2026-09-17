#pragma once
#include <components/openmw-mp/Packets/BasePacket.hpp>
#include <components/openmw-mp/PersuasionProtocol.hpp>

namespace mwmp
{
    class PacketPersuasionRequest : public BasePacket
    {
    public:
        PacketPersuasionRequest() : BasePacket(PacketType::PersuasionRequest) {}
        PersuasionRequest request;
    protected:
        void pack(WriteStream& s) override
        {
            s.writeString(request.requestId); s.write(request.actorId); s.write(request.generation);
            s.write(request.inventoryRevision); s.write(request.relationshipRevision); s.write(static_cast<std::uint8_t>(request.action));
        }
        void unpack(ReadStream& s) override
        {
            if (s.remaining()>256) throw std::runtime_error("Oversized persuasion request");
            request.requestId=s.readString(); s.read(request.actorId); s.read(request.generation);
            s.read(request.inventoryRevision); s.read(request.relationshipRevision);
            std::uint8_t action; s.read(action); request.action=static_cast<PersuasionAction>(action);
            if (request.requestId.empty() || request.requestId.size()>128 || !request.actorId || !request.generation
                || action>static_cast<unsigned>(PersuasionAction::Close) || !s.eof()) throw std::runtime_error("Invalid persuasion request");
        }
    };
    class PacketPersuasionResult : public BasePacket
    {
    public:
        PacketPersuasionResult() : BasePacket(PacketType::PersuasionResult) {}
        PersuasionResult result;
    protected:
        void pack(WriteStream& s) override
        {
            s.writeString(result.requestId); s.write(result.actorId); s.write(result.inventoryRevision); s.write(result.relationshipRevision);
            s.write(static_cast<std::uint8_t>(result.error)); s.write(result.success);
            s.write(result.baseDisposition); s.write(result.currentDisposition); s.write(result.chargedGold); s.write(result.fight); s.write(result.flee);
        }
        void unpack(ReadStream& s) override
        {
            if (s.remaining()>256) throw std::runtime_error("Oversized persuasion result");
            result.requestId=s.readString(); s.read(result.actorId); s.read(result.inventoryRevision); s.read(result.relationshipRevision);
            std::uint8_t error; s.read(error); result.error=static_cast<PersuasionError>(error); s.read(result.success);
            s.read(result.baseDisposition); s.read(result.currentDisposition); s.read(result.chargedGold); s.read(result.fight); s.read(result.flee);
            if (result.requestId.size()>128 || !result.actorId || error>static_cast<unsigned>(PersuasionError::Pending) || !s.eof())
                throw std::runtime_error("Invalid persuasion result");
        }
    };
}
