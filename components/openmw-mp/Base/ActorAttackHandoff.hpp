#ifndef OPENMW_MP_ACTORATTACKHANDOFF_HPP
#define OPENMW_MP_ACTORATTACKHANDOFF_HPP
#include <cstdint>
#include <string>
namespace mwmp
{
    struct ActorAttackHandoff
    {
        std::string cellId;
        uint32_t guid = 0;
        uint32_t generation = 0;
        uint64_t time = 0;
        bool accepts(const std::string& packetCell, uint32_t sender, uint32_t packetGuid,
            uint32_t packetGeneration, uint64_t now) const
        {
            // Only the immediately previous actor owner. Client timestamps are
            // untrusted; a second canonical handoff replaces this grant.
            constexpr uint64_t graceMs = 500;
            return guid != 0 && generation != 0 && sender == guid && packetGuid == guid
                && packetGeneration == generation && packetCell == cellId
                && now >= time && now - time <= graceMs;
        }
    };
    struct ActorAttackStream
    {
        std::string cellId;
        uint32_t guid = 0;
        uint32_t generation = 0;
        uint32_t eventId = 0;
    };
    struct ActorAttackReplayGuard
    {
        ActorAttackStream current, previous;
        uint32_t relayEventId = 0;
        bool accept(const std::string& cellId, uint32_t guid, uint32_t generation, uint32_t eventId)
        {
            if (eventId == 0)
                return false;
            ActorAttackStream* stream = nullptr;
            if (current.cellId == cellId && current.guid == guid && current.generation == generation)
                stream = &current;
            else if (previous.cellId == cellId && previous.guid == guid && previous.generation == generation)
                stream = &previous;
            else
            {
                previous = current;
                current = { cellId, guid, generation, 0 };
                stream = &current;
            }
            if (stream->eventId != 0 && static_cast<int32_t>(eventId - stream->eventId) <= 0)
                return false;
            stream->eventId = eventId;
            if (++relayEventId == 0)
                ++relayEventId;
            return true;
        }
    };
}
#endif
