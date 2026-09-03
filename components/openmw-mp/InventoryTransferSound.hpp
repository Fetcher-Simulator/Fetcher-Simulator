#ifndef OPENMW_MP_INVENTORYTRANSFERSOUND_HPP
#define OPENMW_MP_INVENTORYTRANSFERSOUND_HPP

#include <cstddef>
#include <cstdint>
#include <string>

namespace mwmp
{
    inline constexpr std::uint16_t InventoryTransferSoundProtocolVersion = 1;
    inline constexpr std::size_t MaximumInventoryTransferSoundEventIdLength = 192;
    inline constexpr std::size_t MaximumInventoryTransferSoundRefIdLength = 255;
    inline constexpr std::int32_t MaximumInventoryTransferSoundCount = 1000000;

    enum class InventoryTransferSoundDirection : std::uint8_t
    {
        Up = 1,
        Down = 2,
    };

    constexpr bool isInventoryTransferSoundDirection(InventoryTransferSoundDirection direction)
    {
        return direction == InventoryTransferSoundDirection::Up
            || direction == InventoryTransferSoundDirection::Down;
    }

    enum class InventoryTransferMutation : std::uint8_t
    {
        Added = 1,
        Removed = 2,
    };

    constexpr bool isInventoryTransferMutation(InventoryTransferMutation mutation)
    {
        return mutation == InventoryTransferMutation::Added
            || mutation == InventoryTransferMutation::Removed;
    }

    struct InventoryTransferSound
    {
        std::uint16_t protocolVersion = InventoryTransferSoundProtocolVersion;
        std::string eventId;
        std::uint32_t actorGuid = 0;
        std::string itemRefId;
        // Optional stable stack identity. Zero is valid for legacy stacks;
        // itemRefId still provides the authoritative sound lookup key.
        std::uint32_t itemInstanceId = 0;
        std::int32_t itemCount = 0;
        std::uint64_t inventoryRevision = 0;
        InventoryTransferMutation mutation = InventoryTransferMutation::Added;
        InventoryTransferSoundDirection direction = InventoryTransferSoundDirection::Up;

        bool operator==(const InventoryTransferSound&) const = default;
    };

    bool validateInventoryTransferSound(const InventoryTransferSound& event);
}

#endif
