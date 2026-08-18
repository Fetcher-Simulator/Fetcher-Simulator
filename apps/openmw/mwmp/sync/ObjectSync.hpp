#ifndef OPENMW_MWMP_SYNC_OBJECTSYNC_HPP
#define OPENMW_MWMP_SYNC_OBJECTSYNC_HPP

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <components/openmw-mp/Packets/Object/PacketDoorState.hpp>

namespace mwmp { class NetworkClient; }

namespace mwmp
{
    class ObjectSync
    {
    public:
        explicit ObjectSync(NetworkClient& client);

        // Called by the worldimp.cpp hook when the local player activates a door.
        // Sends a PacketDoorState intent to the server.
        void onDoorStateChanged(const std::string& cellId, const std::string& refId,
                                uint32_t refNum, bool isOpen, bool isLocked, int lockLevel);

        // Called by the protocol handler when the server broadcasts a door state.
        // Attempts to apply immediately; queues for retry if cells aren't loaded yet.
        void onServerDoorState(const std::string& cellId, const std::string& refId,
                               uint32_t refNum, bool isOpen, bool isLocked, int lockLevel,
                               std::uint64_t revision);

        void resetSessionState();

        // Called each frame — retries any pending door states that failed to apply.
        void update(float dt);

    private:
        struct OutgoingDoor
        {
            std::string cellId;
            DoorEntry entry;
        };

        // Try to find and activate a door across all active cells.
        // Returns true if the door was found and activated.
        bool tryApplyDoorState(
            const std::string& refId, uint32_t refNum, bool isOpen, bool isLocked, int lockLevel);
        void flushOutgoingDoorStates();

        NetworkClient& mClient;
        std::vector<OutgoingDoor> mOutgoingDoors;
        std::unordered_map<std::string, std::uint64_t> mDoorRevisions;

        struct PendingDoor
        {
            std::string cellId;
            std::string refId;
            uint32_t    refNum;
            bool        isOpen;
            bool        isLocked;
            int         lockLevel;
            float       retryTimer = 0.f;
        };
        std::vector<PendingDoor> mPendingDoors;
    };

} // namespace mwmp

#endif // OPENMW_MWMP_SYNC_OBJECTSYNC_HPP
