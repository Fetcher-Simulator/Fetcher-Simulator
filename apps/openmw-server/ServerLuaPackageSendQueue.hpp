#ifndef OPENMW_SERVER_LUA_PACKAGE_SEND_QUEUE_HPP
#define OPENMW_SERVER_LUA_PACKAGE_SEND_QUEUE_HPP
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>
#include <utility>

namespace mwmp
{
    // Keep the completion marker and character list behind every source chunk.
    // A full SteamNetworkingSockets send buffer is backpressure, not delivery.
    class ServerLuaPackageSendQueue
    {
    public:
        void push(std::vector<std::uint8_t> packet) { mPackets.push_back(std::move(packet)); }
        template<class Send> bool pump(Send&& send, std::size_t budget = 128 * 1024)
        {
            unsigned count=0;
            while (!mPackets.empty() && budget && count++<4)
            {
                const auto& packet=mPackets.front();
                if (!send(packet)) return false;
                budget=packet.size()>=budget ? 0 : budget-packet.size();
                mPackets.pop_front();
            }
            return mPackets.empty();
        }
    private:
        std::deque<std::vector<std::uint8_t>> mPackets;
    };
}
#endif
