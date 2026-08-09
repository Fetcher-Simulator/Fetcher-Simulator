#ifndef OPENMW_MP_PACKETRECORDDYNAMIC_HPP
#define OPENMW_MP_PACKETRECORDDYNAMIC_HPP

#include <cstdint>
#include <string>
#include <vector>

#include <components/openmw-mp/Base/DynamicRecord.hpp>
#include <components/openmw-mp/Packets/BasePacket.hpp>
#include <components/openmw-mp/Records/DynamicRecordTypes.hpp>

namespace mwmp
{
    class PacketRecordDynamic : public BasePacket
    {
    public:
        static constexpr std::uint32_t MaxEntries = 4096;

        PacketRecordDynamic()
            : BasePacket(PacketType::RecordDynamic)
        {
        }

        DynamicRecordAction action = DynamicRecordAction::Upsert;
        std::string recordType;
        std::vector<DynamicRecordEntry> entries;

    protected:
        void pack(WriteStream& ws) override
        {
            const uint8_t wireAction = static_cast<uint8_t>(action);
            ws.write(wireAction);
            ws.writeString(recordType);

            const auto count = static_cast<uint32_t>(entries.size());
            ws.write(count);
            for (const auto& entry : entries)
            {
                ws.writeString(entry.recordId);
                ws.writeBytes(entry.data);
            }
        }

        void unpack(ReadStream& rs) override
        {
            uint8_t wireAction = 0;
            rs.read(wireAction);
            if (wireAction > static_cast<uint8_t>(DynamicRecordAction::Remove))
                throw std::runtime_error("PacketRecordDynamic: invalid action");
            action = static_cast<DynamicRecordAction>(wireAction);
            recordType = rs.readString();

            uint32_t count = 0;
            rs.read(count);
            if (count > MaxEntries)
                throw std::runtime_error("PacketRecordDynamic: too many entries");
            entries.clear();
            entries.reserve(count);
            for (uint32_t i = 0; i < count; ++i)
            {
                DynamicRecordEntry entry;
                entry.recordId = rs.readString();
                entry.data = rs.readBytes(records::MaximumDefinitionBytes);
                entries.push_back(std::move(entry));
            }
        }
    };
}

#endif // OPENMW_MP_PACKETRECORDDYNAMIC_HPP
