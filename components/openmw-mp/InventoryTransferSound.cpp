#include "InventoryTransferSound.hpp"

#include <string_view>

namespace
{
    bool validString(std::string_view value, std::size_t maximum)
    {
        if (value.empty() || value.size() > maximum)
            return false;
        for (unsigned char c : value)
        {
            if (c == 0 || c < 0x20 || c == 0x7f)
                return false;
        }
        return true;
    }
}

bool mwmp::validateInventoryTransferSound(const InventoryTransferSound& event)
{
    return event.protocolVersion == InventoryTransferSoundProtocolVersion
        && validString(event.eventId, MaximumInventoryTransferSoundEventIdLength)
        && event.actorGuid != 0
        && validString(event.itemRefId, MaximumInventoryTransferSoundRefIdLength)
        && event.itemCount > 0
        && event.itemCount <= MaximumInventoryTransferSoundCount
        && event.inventoryRevision != 0
        && isInventoryTransferMutation(event.mutation)
        && isInventoryTransferSoundDirection(event.direction);
}
