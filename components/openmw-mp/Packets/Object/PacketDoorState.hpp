#ifndef OPENMW_MP_PACKETDOORSTATE_HPP
#define OPENMW_MP_PACKETDOORSTATE_HPP

#include <components/openmw-mp/Packets/BasePacket.hpp>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mwmp
{
    inline constexpr std::uint16_t DoorStateWireVersion = 1;
    inline constexpr std::size_t MaximumDoorStatesPerPacket = 32;
    inline constexpr std::size_t MaximumDoorCellIdLength = 255;
    inline constexpr std::size_t MaximumDoorRefIdLength = 255;

    struct DoorEntry
    {
        std::string cellId;
        std::string refId;
        uint32_t    refNum    = 0;
        uint32_t    mpNum     = 0;
        bool        isOpen    = false;
        bool        isLocked  = false;
        int         lockLevel = 0;
        std::uint64_t revision = 0;

        bool operator==(const DoorEntry&) const = default;
    };

    inline bool validateDoorEntry(const DoorEntry& entry)
    {
        const auto hasControl = [](const std::string& value) {
            return std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; });
        };
        return !entry.cellId.empty() && entry.cellId.size() <= MaximumDoorCellIdLength
            && !hasControl(entry.cellId) && !entry.refId.empty()
            && entry.refId.size() <= MaximumDoorRefIdLength && !hasControl(entry.refId)
            && (entry.refNum != 0 || entry.mpNum != 0) && !(entry.refNum != 0 && entry.mpNum != 0)
            && entry.lockLevel >= -32768 && entry.lockLevel <= 32767
            && entry.revision <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    }

    class PacketDoorState : public BasePacket
    {
    public:
        uint32_t              authorGuid = 0;
        std::string           cellId;
        std::vector<DoorEntry> doors;

        PacketDoorState() : BasePacket(PacketType::DoorState) {}

    protected:
        void pack(WriteStream& ws) override
        {
            if (doors.empty() || doors.size() > MaximumDoorStatesPerPacket
                || cellId.empty() || cellId.size() > MaximumDoorCellIdLength)
                throw std::runtime_error("PacketDoorState: invalid outgoing batch");
            ws.write(DoorStateWireVersion);
            ws.write(authorGuid);
            ws.writeString(cellId);
            auto count = static_cast<std::uint16_t>(doors.size());
            ws.write(count);
            for (const auto& d : doors)
            {
                if (!validateDoorEntry(d) || d.cellId != cellId)
                    throw std::runtime_error("PacketDoorState: invalid outgoing entry");
                ws.writeString(d.cellId);
                ws.writeString(d.refId);
                ws.write(d.refNum);
                ws.write(d.mpNum);
                ws.write(d.isOpen);
                ws.write(d.isLocked);
                ws.write(d.lockLevel);
                ws.write(d.revision);
            }
        }
        void unpack(ReadStream& rs) override
        {
            std::uint16_t wireVersion = 0;
            rs.read(wireVersion);
            if (wireVersion != DoorStateWireVersion)
                throw std::runtime_error("PacketDoorState: unsupported wire version");

            std::uint32_t decodedAuthorGuid = 0;
            rs.read(decodedAuthorGuid);
            std::string decodedCellId = rs.readString();
            if (decodedCellId.empty() || decodedCellId.size() > MaximumDoorCellIdLength)
                throw std::runtime_error("PacketDoorState: invalid cell identity");
            std::uint16_t count = 0;
            rs.read(count);
            if (count == 0 || count > MaximumDoorStatesPerPacket)
                throw std::runtime_error("PacketDoorState: invalid entry count");

            std::vector<DoorEntry> decodedDoors(count);
            for (auto& d : decodedDoors)
            {
                d.cellId   = rs.readString();
                d.refId    = rs.readString();
                rs.read(d.refNum);
                rs.read(d.mpNum);
                rs.read(d.isOpen);
                rs.read(d.isLocked);
                rs.read(d.lockLevel);
                rs.read(d.revision);
                if (!validateDoorEntry(d) || d.cellId != decodedCellId)
                    throw std::runtime_error("PacketDoorState: invalid entry");
            }
            if (!rs.eof() || mHeader.payloadSize + PacketHeader::WIRE_SIZE != rs.pos())
                throw std::runtime_error("PacketDoorState: malformed payload length or trailing bytes");

            authorGuid = decodedAuthorGuid;
            cellId = std::move(decodedCellId);
            doors = std::move(decodedDoors);
        }
    };

} // namespace mwmp

#endif // OPENMW_MP_PACKETDOORSTATE_HPP
