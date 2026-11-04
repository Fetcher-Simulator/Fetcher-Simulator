#ifndef OPENMW_MWSOUND_PROCEDURALMASTER_H
#define OPENMW_MWSOUND_PROCEDURALMASTER_H
#include <array>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace MWSound
{
    // A linked multi-output detector, scoped to one Studio performance. EQ is
    // identical on each spatial stem; compression uses the energy sum of their
    // attack/release envelopes. Independent OpenAL queues publish snapshots at
    // 1.5 kHz; this is a linked spatial master, not a post-listener mono sum.
    // Generation-tagged publications prevent a retired output corrupting a
    // replacement's detector. No allocation, locks or ownership on audio paths.
    class ProceduralMaster
    {
        std::array<std::atomic<std::uint64_t>,33> mSlots{};
        static std::uint64_t pack(std::uint32_t token,float value)
        {std::uint32_t bits;std::memcpy(&bits,&value,4);return (std::uint64_t(token)<<32)|bits;}
    public:
        unsigned claim(unsigned slot)
        {
            if(slot>=mSlots.size())return 0;
            auto old=mSlots[slot].load();
            for(;;)
            {
                const auto token=std::uint32_t(old>>32);
                if(token&1)return 0;
                const auto next=token==UINT32_MAX-1?1:token+1;
                if(mSlots[slot].compare_exchange_weak(old,pack(next,0)))return next;
            }
        }
        void publish(unsigned slot,unsigned token,float envelope)
        {
            if(slot>=mSlots.size() || !(token&1) || !std::isfinite(envelope))return;
            auto old=mSlots[slot].load(std::memory_order_relaxed);
            while(std::uint32_t(old>>32)==token)
                if(mSlots[slot].compare_exchange_weak(old,pack(token,std::clamp(envelope,0.f,4096.f)),std::memory_order_relaxed))break;
        }
        void release(unsigned slot,unsigned token)
        {
            if(slot>=mSlots.size() || !(token&1))return;
            auto old=mSlots[slot].load();
            while(std::uint32_t(old>>32)==token)
                if(mSlots[slot].compare_exchange_weak(old,pack(token+1,0)))break;
        }
        double envelope() const
        {
            double power=0;
            for(const auto& slot:mSlots)
            {
                const auto bits=slot.load(std::memory_order_relaxed);
                if(!(std::uint32_t(bits>>32)&1))continue;
                const auto low=std::uint32_t(bits);float value;std::memcpy(&value,&low,4);
                power+=double(value)*value;
            }
            return std::sqrt(power);
        }
    };
}
#endif
