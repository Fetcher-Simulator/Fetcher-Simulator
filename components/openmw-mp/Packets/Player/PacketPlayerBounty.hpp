#ifndef OPENMW_MP_PACKETPLAYERBOUNTY_HPP
#define OPENMW_MP_PACKETPLAYERBOUNTY_HPP

#include <stdexcept>

#include <components/openmw-mp/PlayerCrimeState.hpp>

#include "PlayerPacket.hpp"

namespace mwmp
{
    /// Server-authored semantic player crime state. Installing this packet
    /// mirrors state; it never executes the script or gameplay cause that
    /// produced the transition.
    class PacketPlayerBounty : public PlayerPacket
    {
    public:
        PacketPlayerBounty()
            : PlayerPacket(PacketType::PlayerBounty)
        {
        }

    protected:
        void pack(WriteStream& stream) override
        {
            stream.write(mPlayer->guid);
            stream.write(mPlayer->crimeState.schemaVersion);
            stream.write(mPlayer->crimeState.revision);
            stream.write(mPlayer->crimeState.bounty);
            stream.write(mPlayer->crimeState.currentCrimeId);
            stream.write(mPlayer->crimeState.paidCrimeId);
        }

        void unpack(ReadStream& stream) override
        {
            stream.read(mPlayer->guid);
            stream.read(mPlayer->crimeState.schemaVersion);
            stream.read(mPlayer->crimeState.revision);
            stream.read(mPlayer->crimeState.bounty);
            stream.read(mPlayer->crimeState.currentCrimeId);
            stream.read(mPlayer->crimeState.paidCrimeId);
            if (validatePlayerCrimeState(mPlayer->crimeState) != CrimeError::None)
                throw std::runtime_error("PacketPlayerBounty: invalid crime state");
            if (!stream.eof()
                || mHeader.payloadSize + PacketHeader::WIRE_SIZE != stream.pos())
                throw std::runtime_error("PacketPlayerBounty: malformed payload length or trailing bytes");
        }
    };
}

#endif
