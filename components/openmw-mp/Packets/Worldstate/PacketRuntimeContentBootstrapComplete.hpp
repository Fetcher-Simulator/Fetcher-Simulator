#ifndef OPENMW_MP_PACKETRUNTIMECONTENTBOOTSTRAPCOMPLETE_HPP
#define OPENMW_MP_PACKETRUNTIMECONTENTBOOTSTRAPCOMPLETE_HPP

#include <components/openmw-mp/Packets/BasePacket.hpp>

namespace mwmp
{
    /// Ordered marker sent after the server's RecordDynamic bootstrap stream.
    class PacketRuntimeContentBootstrapComplete : public BasePacket
    {
    public:
        PacketRuntimeContentBootstrapComplete()
            : BasePacket(PacketType::RuntimeContentBootstrapComplete)
        {
        }

    protected:
        void pack(WriteStream&) override {}
        void unpack(ReadStream& stream) override
        {
            if (!stream.eof())
                throw std::runtime_error("PacketRuntimeContentBootstrapComplete: unexpected payload");
        }
    };
}

#endif
