#ifndef OPENMW_MP_PACKETPLAYERTOPIC_HPP
#define OPENMW_MP_PACKETPLAYERTOPIC_HPP

#include <stdexcept>

#include <components/openmw-mp/PlayerTopicState.hpp>

#include "PlayerPacket.hpp"

namespace mwmp
{
    class PacketPlayerTopic : public PlayerPacket
    {
    public:
        enum class Action : std::uint8_t
        {
            Add = 0,
            Set = 1,
        };

        Action action = Action::Set;

        PacketPlayerTopic()
            : PlayerPacket(PacketType::PlayerTopic)
        {
        }

    protected:
        void pack(WriteStream& stream) override
        {
            stream.write(mPlayer->guid);
            stream.write(static_cast<std::uint8_t>(action));
            stream.write(mPlayer->topicState.schemaVersion);
            stream.write(mPlayer->topicState.revision);
            stream.write(static_cast<std::uint16_t>(mPlayer->topicState.knownTopicIds.size()));
            for (const std::string& id : mPlayer->topicState.knownTopicIds)
                stream.writeString(id);
        }

        void unpack(ReadStream& stream) override
        {
            stream.read(mPlayer->guid);
            std::uint8_t rawAction = 0;
            stream.read(rawAction);
            if (rawAction > static_cast<std::uint8_t>(Action::Set))
                throw std::runtime_error("PacketPlayerTopic: invalid action");
            action = static_cast<Action>(rawAction);
            stream.read(mPlayer->topicState.schemaVersion);
            stream.read(mPlayer->topicState.revision);
            std::uint16_t count = 0;
            stream.read(count);
            if (count > MaximumKnownTopics)
                throw std::runtime_error("PacketPlayerTopic: too many topics");
            mPlayer->topicState.knownTopicIds.resize(count);
            for (std::string& id : mPlayer->topicState.knownTopicIds)
                id = stream.readString();
            if (validatePlayerTopicState(mPlayer->topicState) != TopicStateError::None)
                throw std::runtime_error("PacketPlayerTopic: invalid topic state");
            if (!stream.eof() || mHeader.payloadSize + PacketHeader::WIRE_SIZE != stream.pos())
                throw std::runtime_error("PacketPlayerTopic: trailing bytes");
        }
    };
}

#endif
