#ifndef OPENMW_SERVER_INVENTORYPROFILE_HPP
#define OPENMW_SERVER_INVENTORYPROFILE_HPP

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <string_view>

#include <components/debug/debuglog.hpp>

namespace mwmp
{
    // Opt-in, per-client aggregates. No per-packet output even when profiling.
    struct InventoryProfile
    {
        using Clock = std::chrono::steady_clock;
        enum Stage { Decode, Validation, Apply, Reconciliation, Persistence, Lua, Gc, Encode, Send, Count };
        std::array<double, Count> totalMs{};
        std::array<double, Count> maxMs{};
        Clock::time_point windowStart = Clock::now();
        std::size_t packets = 0;
        std::size_t accepted = 0;
        std::size_t chargeOnly = 0;
        std::size_t bytes = 0;
        std::size_t stacks = 0;
    };

    class InventoryProfileSample
    {
        static bool enabled()
        {
            static const bool value = [] {
                const char* env = std::getenv("OPENMW_PROFILE_PLAYER_INVENTORY");
                return env && std::string_view(env) == "1";
            }();
            return value;
        }

    public:
        InventoryProfileSample(InventoryProfile& profile, std::string_view player, std::size_t bytes)
            : mProfile(enabled() ? &profile : nullptr), mPlayer(player)
        {
            if (mProfile)
            {
                mLast = InventoryProfile::Clock::now();
                ++mProfile->packets;
                mProfile->bytes += bytes;
            }
        }

        void stage(InventoryProfile::Stage next)
        {
            if (!mProfile)
                return;
            const auto now = InventoryProfile::Clock::now();
            const double ms = std::chrono::duration<double, std::milli>(now - mLast).count();
            mStageMs[mStage] += ms;
            mStage = next;
            mLast = now;
        }

        bool active() const { return mProfile != nullptr; }

        void accepted(std::size_t stacks, bool chargeOnly)
        {
            if (!mProfile)
                return;
            ++mProfile->accepted;
            mProfile->stacks += stacks;
            mProfile->chargeOnly += chargeOnly;
        }

        ~InventoryProfileSample()
        {
            if (!mProfile)
                return;
            stage(InventoryProfile::Decode);
            for (std::size_t i = 0; i < mStageMs.size(); ++i)
            {
                mProfile->totalMs[i] += mStageMs[i];
                mProfile->maxMs[i] = std::max(mProfile->maxMs[i], mStageMs[i]);
            }
            const double seconds = std::chrono::duration<double>(mLast - mProfile->windowStart).count();
            if (seconds < 30.)
                return;
            auto log = Log(Debug::Info);
            log << "[MPDIAG] PlayerInventory profile player=" << mPlayer
                << " seconds=" << seconds << " packets=" << mProfile->packets
                << " accepted=" << mProfile->accepted << " chargeOnly=" << mProfile->chargeOnly
                << " bytes=" << mProfile->bytes << " stacks=" << mProfile->stacks;
            constexpr std::array names{ "decode", "validation", "apply", "reconciliation", "persistence",
                "lua", "gc", "encode", "send" };
            for (std::size_t i = 0; i < names.size(); ++i)
                log << " " << names[i] << "AvgMs=" << mProfile->totalMs[i] / mProfile->packets
                    << " " << names[i] << "MaxMs=" << mProfile->maxMs[i];
            *mProfile = {};
        }

    private:
        InventoryProfile* mProfile;
        std::string_view mPlayer;
        InventoryProfile::Clock::time_point mLast;
        InventoryProfile::Stage mStage = InventoryProfile::Decode;
        std::array<double, InventoryProfile::Count> mStageMs{};
    };
}

#endif
