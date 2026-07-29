#ifndef OPENMW_COMPONENTS_OPENMW_MP_PACKETS_ACTOR_PACKETACTORSPEECH_HPP
#define OPENMW_COMPONENTS_OPENMW_MP_PACKETS_ACTOR_PACKETACTORSPEECH_HPP

#include "ActorPacket.hpp"

namespace mwmp
{
    class PacketActorSpeech : public ActorPacket
    {
    public:
        PacketActorSpeech()
            : ActorPacket(PacketType::ActorSpeech)
        {
        }

        void setSpeechList(ActorSpeechList* speechList)
        {
            mSpeechList = speechList;
        }

    protected:
        void pack(WriteStream& ws) override
        {
            ws.write(mSpeechList->protocolVersion);
            ws.writeString(mSpeechList->cellId);
            ws.write(mSpeechList->authorityGuid);
            ws.write(mSpeechList->authorityGeneration);
            ws.write(mSpeechList->sequence);
            ws.write(mSpeechList->serverTimestamp);

            const auto count = static_cast<uint16_t>(mSpeechList->events.size());
            ws.write(count);
            for (const ActorSpeechEvent& event : mSpeechList->events)
            {
                ws.write(event.actorNetId);
                ws.write(event.eventId);
                ws.writeString(event.sound);
            }
        }

        void unpack(ReadStream& rs) override
        {
            rs.read(mSpeechList->protocolVersion);
            mSpeechList->cellId = rs.readString();
            rs.read(mSpeechList->authorityGuid);
            rs.read(mSpeechList->authorityGeneration);
            rs.read(mSpeechList->sequence);
            rs.read(mSpeechList->serverTimestamp);

            uint16_t count = 0;
            rs.read(count);
            mSpeechList->events.resize(count);
            for (ActorSpeechEvent& event : mSpeechList->events)
            {
                rs.read(event.actorNetId);
                rs.read(event.eventId);
                event.sound = rs.readString();
            }
        }

    private:
        ActorSpeechList* mSpeechList = nullptr;
    };
}

#endif
