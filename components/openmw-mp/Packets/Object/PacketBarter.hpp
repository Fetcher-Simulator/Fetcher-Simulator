#ifndef OPENMW_MP_PACKETBARTER_HPP
#define OPENMW_MP_PACKETBARTER_HPP

#include <stdexcept>

#include <components/openmw-mp/Barter.hpp>
#include <components/openmw-mp/Packets/BasePacket.hpp>
#include <components/openmw-mp/Packets/Object/PacketInventoryTake.hpp>
#include <components/openmw-mp/Packets/Object/PacketWorldItemTake.hpp>

namespace mwmp
{
    class PacketBarterRequest final : public BasePacket
    {
    public:
        BarterRequest request;
        PacketBarterRequest() : BasePacket(PacketType::BarterRequest) {}

    protected:
        void pack(WriteStream& stream) override
        {
            stream.write(request.protocolVersion);
            stream.writeString(request.requestId);
            packInventorySource(stream, request.merchant);
            stream.write(static_cast<std::uint16_t>(request.lines.size()));
            for (const BarterLine& line : request.lines)
            {
                stream.write(static_cast<std::uint8_t>(line.kind));
                packInventorySource(stream, line.source);
                writePlacedObjectIdentity(stream, line.worldObject);
                stream.writeString(line.itemRefId);
                stream.write(line.itemInstanceId);
                stream.write(line.itemCharge);
                stream.write(line.itemEnchantmentCharge);
                stream.writeString(line.itemSoul);
                stream.write(line.count);
            }
            stream.write(request.balance);
            stream.write(request.merchantGold);
            stream.write(request.expectedInventoryRevision);
        }

        void unpack(ReadStream& stream) override
        {
            stream.read(request.protocolVersion);
            request.requestId = stream.readString();
            unpackInventorySource(stream, request.merchant);
            std::uint16_t lineCount = 0;
            stream.read(lineCount);
            if (lineCount == 0 || lineCount > MaximumBarterLines)
                throw std::runtime_error("PacketBarterRequest: invalid line count");
            request.lines.clear();
            request.lines.reserve(lineCount);
            for (std::uint16_t i = 0; i < lineCount; ++i)
            {
                BarterLine line;
                std::uint8_t kind = 0;
                stream.read(kind);
                line.kind = static_cast<BarterLineKind>(kind);
                unpackInventorySource(stream, line.source);
                readPlacedObjectIdentity(stream, line.worldObject);
                line.itemRefId = stream.readString();
                stream.read(line.itemInstanceId);
                stream.read(line.itemCharge);
                stream.read(line.itemEnchantmentCharge);
                line.itemSoul = stream.readString();
                stream.read(line.count);
                request.lines.push_back(std::move(line));
            }
            stream.read(request.balance);
            stream.read(request.merchantGold);
            stream.read(request.expectedInventoryRevision);
            if (validateBarterRequest(request) != BarterError::None || !stream.eof()
                || mHeader.payloadSize + PacketHeader::WIRE_SIZE != stream.pos())
                throw std::runtime_error("PacketBarterRequest: invalid payload");
        }
    };

    class PacketBarterResult final : public BasePacket
    {
    public:
        BarterResult result;
        PacketBarterResult() : BasePacket(PacketType::BarterResult) {}

    protected:
        void pack(WriteStream& stream) override
        {
            stream.write(result.protocolVersion);
            stream.writeString(result.requestId);
            stream.write(static_cast<std::uint8_t>(result.accepted));
            stream.write(static_cast<std::uint8_t>(result.replayed));
            stream.write(static_cast<std::uint16_t>(result.error));
            stream.write(result.inventoryRevision);
            stream.write(result.balance);
            stream.write(result.merchantGold);
            stream.write(result.buyLines);
            stream.write(result.sellLines);
            stream.write(static_cast<std::uint16_t>(result.missingSources.size()));
            for (const InventorySourceIdentity& source : result.missingSources)
                packInventorySource(stream, source);
        }

        void unpack(ReadStream& stream) override
        {
            stream.read(result.protocolVersion);
            result.requestId = stream.readString();
            std::uint8_t accepted = 0;
            std::uint8_t replayed = 0;
            std::uint16_t error = 0;
            stream.read(accepted);
            stream.read(replayed);
            stream.read(error);
            stream.read(result.inventoryRevision);
            stream.read(result.balance);
            stream.read(result.merchantGold);
            stream.read(result.buyLines);
            stream.read(result.sellLines);
            std::uint16_t missingCount = 0;
            stream.read(missingCount);
            if (missingCount > MaximumBarterLines)
                throw std::runtime_error("PacketBarterResult: too many missing sources");
            result.missingSources.resize(missingCount);
            for (InventorySourceIdentity& source : result.missingSources)
            {
                unpackInventorySource(stream, source);
                if (!isCanonicalBarterSourceIdentity(source))
                    throw std::runtime_error("PacketBarterResult: malformed missing source");
            }
            result.accepted = accepted != 0;
            result.replayed = replayed != 0;
            result.error = static_cast<BarterError>(error);
            if (accepted > 1 || replayed > 1 || result.protocolVersion != BarterProtocolVersion
                || result.requestId.empty() || result.requestId.size() > MaximumBarterRequestIdLength
                || getBarterErrorCode(result.error) == "unknown_error"
                || (result.accepted && result.error != BarterError::None)
                || (!result.accepted && result.error == BarterError::None)
                || result.merchantGold < 0
                || (result.accepted && !result.missingSources.empty())
                || (result.error == BarterError::SourceUnavailable && result.missingSources.empty())
                || (result.error != BarterError::SourceUnavailable && !result.missingSources.empty())
                || !stream.eof() || mHeader.payloadSize + PacketHeader::WIRE_SIZE != stream.pos())
                throw std::runtime_error("PacketBarterResult: invalid payload");
        }
    };
}

#endif
