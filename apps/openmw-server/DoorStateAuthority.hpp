#ifndef OPENMW_SERVER_DOORSTATEAUTHORITY_HPP
#define OPENMW_SERVER_DOORSTATEAUTHORITY_HPP

#include <array>
#include <optional>
#include <string>
#include <vector>

#include <components/openmw-mp/Packets/Object/PacketDoorState.hpp>

namespace mwmp
{
    enum class DoorStateProposalError : std::uint8_t
    {
        None,
        InvalidEntry,
        WrongCell,
        IrrelevantCell,
        UnsupportedDynamicDoor,
        UnknownDoor,
        WrongDoorIdentity,
        InvalidRevision,
        StaleRevision,
        InvalidTransition,
        LockStateMutation,
        InvalidDistance,
        TooFar,
    };

    struct DoorStateReference
    {
        std::string cellId;
        std::string refId;
        std::uint32_t refNum = 0;
        std::array<float, 3> position {};
    };

    struct DoorStateProposalContext
    {
        std::string packetCellId;
        std::vector<std::string> relevantCellIds;
        std::array<float, 3> playerPosition {};
        float maximumDistance = 0.f;
        std::optional<DoorEntry> current;
        std::optional<DoorStateReference> reference;
    };

    DoorStateProposalError validateDoorStateProposal(
        const DoorEntry& proposed, const DoorStateProposalContext& context);
    const char* doorStateProposalErrorName(DoorStateProposalError error);
}

#endif
