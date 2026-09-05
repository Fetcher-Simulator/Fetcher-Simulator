#ifndef OPENMW_MP_PACKETINVENTORYTAKE_HPP
#define OPENMW_MP_PACKETINVENTORYTAKE_HPP

#include <stdexcept>

#include <components/openmw-mp/InventoryTake.hpp>
#include <components/openmw-mp/Packets/BasePacket.hpp>

namespace mwmp
{
    inline void packInventorySource(WriteStream& stream, const InventorySourceIdentity& source)
    {
        stream.writeString(source.cellId);
        stream.writeString(source.refId);
        stream.write(source.refNum);
        stream.write(source.mpNum);
        stream.write(source.actorInstanceId);
        stream.write(source.migrationGeneration);
        stream.write(source.authorityGeneration);
    }

    inline void unpackInventorySource(ReadStream& stream, InventorySourceIdentity& source)
    {
        source.cellId = stream.readString();
        source.refId = stream.readString();
        stream.read(source.refNum);
        stream.read(source.mpNum);
        stream.read(source.actorInstanceId);
        stream.read(source.migrationGeneration);
        stream.read(source.authorityGeneration);
    }

    class PacketInventoryTakeRequest final : public BasePacket
    {
    public:
        InventoryTakeRequest request;
        PacketInventoryTakeRequest() : BasePacket(PacketType::InventoryTakeRequest) {}

    protected:
        void pack(WriteStream& stream) override
        {
            stream.write(request.protocolVersion);
            stream.writeString(request.requestId);
            stream.write(static_cast<std::uint8_t>(request.kind));
            packInventorySource(stream, request.source);
            packInventorySource(stream, request.merchant);
            stream.writeString(request.itemRefId);
            stream.write(request.itemInstanceId);
            stream.write(request.itemCharge);
            stream.write(request.itemEnchantmentCharge);
            stream.writeString(request.itemSoul);
            stream.write(request.requestedCount);
            stream.write(request.barterPrice);
            stream.write(request.expectedInventoryRevision);
            stream.write(static_cast<std::uint8_t>(request.soundDirection));
        }
        void unpack(ReadStream& stream) override
        {
            stream.read(request.protocolVersion);
            request.requestId = stream.readString();
            std::uint8_t kind = 0;
            stream.read(kind);
            request.kind = static_cast<InventoryTakeKind>(kind);
            unpackInventorySource(stream, request.source);
            unpackInventorySource(stream, request.merchant);
            request.itemRefId = stream.readString();
            stream.read(request.itemInstanceId);
            stream.read(request.itemCharge);
            stream.read(request.itemEnchantmentCharge);
            request.itemSoul = stream.readString();
            stream.read(request.requestedCount);
            stream.read(request.barterPrice);
            stream.read(request.expectedInventoryRevision);
            std::uint8_t soundDirection = 0;
            stream.read(soundDirection);
            request.soundDirection = static_cast<InventoryTransferSoundDirection>(soundDirection);
            if (validateInventoryTakeRequest(request) != InventoryTakeError::None || !stream.eof()
                || mHeader.payloadSize + PacketHeader::WIRE_SIZE != stream.pos())
                throw std::runtime_error("PacketInventoryTakeRequest: invalid payload");
        }
    };

    class PacketInventoryTakeResult final : public BasePacket
    {
    public:
        InventoryTakeResult result;
        PacketInventoryTakeResult() : BasePacket(PacketType::InventoryTakeResult) {}

    protected:
        void pack(WriteStream& stream) override
        {
            stream.write(result.protocolVersion);
            stream.writeString(result.requestId);
            stream.write(static_cast<std::uint8_t>(result.accepted));
            stream.write(static_cast<std::uint8_t>(result.replayed));
            stream.write(static_cast<std::uint16_t>(result.error));
            stream.write(static_cast<std::uint8_t>(result.kind));
            packInventorySource(stream, result.source);
            stream.writeString(result.itemRefId);
            stream.write(result.itemCharge);
            stream.write(result.itemCount);
            stream.write(result.inventoryRevision);
            stream.write(static_cast<std::uint8_t>(result.detected));
            stream.write(result.detectionRoll);
            stream.write(static_cast<std::uint8_t>(result.theft));
            stream.write(result.crimeValue);
        }
        void unpack(ReadStream& stream) override
        {
            stream.read(result.protocolVersion);
            result.requestId = stream.readString();
            std::uint8_t accepted = 0, replayed = 0, kind = 0, detected = 0, theft = 0;
            std::uint16_t error = 0;
            stream.read(accepted); stream.read(replayed); stream.read(error); stream.read(kind);
            if (accepted > 1 || replayed > 1)
                throw std::runtime_error("PacketInventoryTakeResult: invalid flags");
            result.accepted = accepted != 0;
            result.replayed = replayed != 0;
            result.error = static_cast<InventoryTakeError>(error);
            result.kind = static_cast<InventoryTakeKind>(kind);
            unpackInventorySource(stream, result.source);
            result.itemRefId = stream.readString();
            stream.read(result.itemCharge);
            stream.read(result.itemCount);
            stream.read(result.inventoryRevision);
            stream.read(detected); stream.read(result.detectionRoll); stream.read(theft); stream.read(result.crimeValue);
            if (detected > 1 || theft > 1 || result.protocolVersion != InventoryTakeProtocolVersion
                || result.requestId.empty() || result.requestId.size() > MaximumInventoryTakeRequestIdLength
                || getInventoryTakeErrorCode(result.error) == "unknown_error"
                || (result.accepted && result.error != InventoryTakeError::None)
                || (!result.accepted && result.error == InventoryTakeError::None)
                || !stream.eof() || mHeader.payloadSize + PacketHeader::WIRE_SIZE != stream.pos())
                throw std::runtime_error("PacketInventoryTakeResult: invalid payload");
            result.detected = detected != 0;
            result.theft = theft != 0;
        }
    };

    class PacketInventoryTakeBatchRequest final : public BasePacket
    {
    public:
        InventoryTakeBatchRequest request;
        PacketInventoryTakeBatchRequest() : BasePacket(PacketType::InventoryTakeBatchRequest) {}

    protected:
        void pack(WriteStream& stream) override
        {
            stream.write(request.protocolVersion);
            stream.writeString(request.requestId);
            stream.write(static_cast<std::uint8_t>(request.kind));
            packInventorySource(stream, request.source);
            stream.write(request.expectedInventoryRevision);
            stream.write(static_cast<std::uint8_t>(request.soundDirection));
            stream.write(static_cast<std::uint32_t>(request.items.size()));
            for (const InventoryTakeBatchLine& line : request.items)
            {
                stream.writeString(line.itemRefId);
                stream.write(line.itemInstanceId);
                stream.write(line.itemCharge);
                stream.write(line.itemEnchantmentCharge);
                stream.writeString(line.itemSoul);
                stream.write(line.requestedCount);
            }
        }

        void unpack(ReadStream& stream) override
        {
            stream.read(request.protocolVersion);
            request.requestId = stream.readString();
            std::uint8_t kind = 0;
            stream.read(kind);
            request.kind = static_cast<InventoryTakeKind>(kind);
            unpackInventorySource(stream, request.source);
            stream.read(request.expectedInventoryRevision);
            std::uint8_t soundDirection = 0;
            stream.read(soundDirection);
            request.soundDirection = static_cast<InventoryTransferSoundDirection>(soundDirection);
            std::uint32_t lineCount = 0;
            stream.read(lineCount);
            if (lineCount == 0 || lineCount > MaximumInventoryTakeBatchLines)
                throw std::runtime_error("PacketInventoryTakeBatchRequest: invalid line count");
            request.items.clear();
            request.items.reserve(lineCount);
            for (std::uint32_t i = 0; i < lineCount; ++i)
            {
                InventoryTakeBatchLine line;
                line.itemRefId = stream.readString();
                stream.read(line.itemInstanceId);
                stream.read(line.itemCharge);
                stream.read(line.itemEnchantmentCharge);
                line.itemSoul = stream.readString();
                stream.read(line.requestedCount);
                request.items.push_back(std::move(line));
            }
            if (validateInventoryTakeBatchRequest(request) != InventoryTakeError::None || !stream.eof()
                || mHeader.payloadSize + PacketHeader::WIRE_SIZE != stream.pos())
                throw std::runtime_error("PacketInventoryTakeBatchRequest: invalid payload");
        }
    };

    class PacketInventoryTakeBatchResult final : public BasePacket
    {
    public:
        InventoryTakeBatchResult result;
        PacketInventoryTakeBatchResult() : BasePacket(PacketType::InventoryTakeBatchResult) {}

    protected:
        void pack(WriteStream& stream) override
        {
            stream.write(result.protocolVersion);
            stream.writeString(result.requestId);
            stream.write(static_cast<std::uint8_t>(result.accepted));
            stream.write(static_cast<std::uint8_t>(result.replayed));
            stream.write(static_cast<std::uint16_t>(result.error));
            stream.write(static_cast<std::uint8_t>(result.kind));
            packInventorySource(stream, result.source);
            stream.write(result.lineCount);
            stream.write(result.itemCount);
            stream.write(result.inventoryRevision);
            stream.write(static_cast<std::uint8_t>(result.theft));
            stream.write(result.crimeValue);
        }

        void unpack(ReadStream& stream) override
        {
            stream.read(result.protocolVersion);
            result.requestId = stream.readString();
            std::uint8_t accepted = 0, replayed = 0, kind = 0, theft = 0;
            std::uint16_t error = 0;
            stream.read(accepted);
            stream.read(replayed);
            stream.read(error);
            stream.read(kind);
            if (accepted > 1 || replayed > 1)
                throw std::runtime_error("PacketInventoryTakeBatchResult: invalid flags");
            result.accepted = accepted != 0;
            result.replayed = replayed != 0;
            result.error = static_cast<InventoryTakeError>(error);
            result.kind = static_cast<InventoryTakeKind>(kind);
            unpackInventorySource(stream, result.source);
            stream.read(result.lineCount);
            stream.read(result.itemCount);
            stream.read(result.inventoryRevision);
            stream.read(theft);
            stream.read(result.crimeValue);
            if (theft > 1 || result.protocolVersion != InventoryTakeProtocolVersion
                || result.requestId.empty() || result.requestId.size() > MaximumInventoryTakeRequestIdLength
                || result.lineCount > MaximumInventoryTakeBatchLines
                || result.itemCount < 0
                || getInventoryTakeErrorCode(result.error) == "unknown_error"
                || (result.accepted && result.error != InventoryTakeError::None)
                || (!result.accepted && result.error == InventoryTakeError::None)
                || !stream.eof() || mHeader.payloadSize + PacketHeader::WIRE_SIZE != stream.pos())
                throw std::runtime_error("PacketInventoryTakeBatchResult: invalid payload");
            result.theft = theft != 0;
        }
    };
}

#endif
