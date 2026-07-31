#ifndef OPENMW_MP_PACKETPLAYERPOSITION_HPP
#define OPENMW_MP_PACKETPLAYERPOSITION_HPP

#include "PlayerPacket.hpp"

namespace mwmp
{
    class PacketPlayerPosition : public PlayerPacket
    {
    public:
        PacketPlayerPosition() : PlayerPacket(PacketType::PlayerPosition) {}

    protected:
        void pack(WriteStream& ws) override
        {
            ws.write(mPlayer->guid);
            packPosition(ws, mPlayer->position);
            ws.write(mPlayer->position.isTeleporting);
            ws.write(mPlayer->velocity.linear[0]);
            ws.write(mPlayer->velocity.linear[1]);
            ws.write(mPlayer->velocity.linear[2]);
            ws.write(mPlayer->positionSampleTimeUs);
            ws.write(mPlayer->vehicle.hasRigidBodyPose);
            for (float compression : mPlayer->vehicle.suspensionCompression)
                ws.write(compression);
        }

        void unpack(ReadStream& rs) override
        {
            rs.read(mPlayer->guid);
            unpackPosition(rs, mPlayer->position);
            rs.read(mPlayer->position.isTeleporting);
            rs.read(mPlayer->velocity.linear[0]);
            rs.read(mPlayer->velocity.linear[1]);
            rs.read(mPlayer->velocity.linear[2]);
            // Keep decoding compatible with position packets produced before
            // timestamped snapshots were introduced.
            if (rs.remaining() >= sizeof(mPlayer->positionSampleTimeUs))
                rs.read(mPlayer->positionSampleTimeUs);
            else
                mPlayer->positionSampleTimeUs = 0;

            mPlayer->vehicle.hasRigidBodyPose = false;
            mPlayer->vehicle.suspensionCompression.fill(0.f);
            constexpr std::size_t rigidBodyPoseBytes = sizeof(bool) + sizeof(float) * 4;
            if (rs.remaining() >= rigidBodyPoseBytes)
            {
                rs.read(mPlayer->vehicle.hasRigidBodyPose);
                for (float& compression : mPlayer->vehicle.suspensionCompression)
                    rs.read(compression);
            }
        }
    };

} // namespace mwmp

#endif // OPENMW_MP_PACKETPLAYERPOSITION_HPP
