#ifndef OPENMW_MP_PACKETCONTAINER_HPP
#define OPENMW_MP_PACKETCONTAINER_HPP

#include <algorithm>
#include <stdexcept>

#include <components/openmw-mp/Packets/BasePacket.hpp>
#include <components/openmw-mp/Base/BaseObject.hpp>

namespace mwmp
{
    inline constexpr std::uint16_t ContainerWireVersion = 2;
    // -----------------------------------------------------------------------
    // PacketContainer — shared container (chest/barrel/crate) sync.
    //
    // Flow:
    //   Player opens container → client sends action=Set with full contents.
    //   Server: if no authority record, store it and echo back to opener.
    //           if authority record exists, send it back as action=Set.
    //   Player takes/adds items → client sends action=Remove / Add deltas.
    //   Server: apply delta to record, relay to all clients in cell.
    //
    // Race condition (two players open same container simultaneously):
    //   Server serialises updates — first-writer wins.
    //   Slower client receives server's authoritative Set and UI refreshes.
    // -----------------------------------------------------------------------
    class PacketContainer : public BasePacket
    {
    public:
        ContainerRecord container;
        std::uint32_t authorityGeneration = 0;

        PacketContainer() : BasePacket(PacketType::Container) {}

    protected:
        void packItem(WriteStream& ws, const ContainerItem& item)
        {
            ws.writeString(item.refId);
            ws.write(item.count);
            ws.write(item.charge);
            ws.write(item.instanceId);
            ws.write(item.enchantmentCharge);
            ws.writeString(item.soul);
            ws.write(static_cast<std::uint8_t>(item.restocking));
        }

        void unpackItem(ReadStream& rs, ContainerItem& item)
        {
            item.refId = rs.readString();
            rs.read(item.count);
            rs.read(item.charge);
            rs.read(item.instanceId);
            rs.read(item.enchantmentCharge);
            item.soul = rs.readString();
            std::uint8_t restocking = 0;
            rs.read(restocking);
            if (restocking > 1)
                throw std::runtime_error("PacketContainer: invalid restocking flag");
            item.restocking = restocking != 0;
        }

        void pack(WriteStream& ws) override
        {
            ws.write(ContainerWireVersion);
            ws.write(authorityGeneration);
            ws.writeString(container.cellId);
            ws.writeString(container.refId);
            ws.write(container.refNum);
            ws.write(container.mpNum);
            ws.write(mAction);
            auto count = static_cast<uint16_t>(container.items.size());
            ws.write(count);
            for (const auto& item : container.items)
                packItem(ws, item);
        }

        void unpack(ReadStream& rs) override
        {
            std::uint16_t wireVersion = 0;
            rs.read(wireVersion);
            if (wireVersion != ContainerWireVersion)
                throw std::runtime_error("PacketContainer: unsupported wire version");
            rs.read(authorityGeneration);
            container.cellId = rs.readString();
            container.refId  = rs.readString();
            rs.read(container.refNum);
            rs.read(container.mpNum);
            rs.read(mAction);
            uint16_t count = 0;
            rs.read(count);
            container.items.resize(count);
            for (auto& item : container.items)
                unpackItem(rs, item);
            const auto action = static_cast<ContainerAction>(mAction);
            const bool cellReset = action == ContainerAction::Reset;
            if (container.cellId.empty()
                || (cellReset
                    ? (!container.refId.empty() || container.refNum != 0 || container.mpNum != 0
                        || !container.items.empty())
                    : container.refId.empty())
                || (action != ContainerAction::Set && action != ContainerAction::Add
                    && action != ContainerAction::Remove && action != ContainerAction::BootstrapRequest
                    && action != ContainerAction::Reset)
                || std::any_of(container.items.begin(), container.items.end(), [](const ContainerItem& item) {
                    return item.refId.empty() || item.count <= 0;
                })
                || !rs.eof() || mHeader.payloadSize + PacketHeader::WIRE_SIZE != rs.pos())
                throw std::runtime_error("PacketContainer: invalid payload");
        }

    public:
        // Explicit action field — set by caller before encode(), read after decode().
        uint8_t mAction = static_cast<uint8_t>(ContainerAction::Set);
    };

} // namespace mwmp

#endif // OPENMW_MP_PACKETCONTAINER_HPP
