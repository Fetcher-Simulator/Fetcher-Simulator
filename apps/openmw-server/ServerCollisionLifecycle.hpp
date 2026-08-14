#ifndef OPENMW_SERVER_SERVERCOLLISIONLIFECYCLE_HPP
#define OPENMW_SERVER_SERVERCOLLISIONLIFECYCLE_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mwmp
{
    class ServerCollisionLifecycle
    {
    public:
        struct State
        {
            std::size_t refCount = 0;
            std::uint64_t generation = 0;
        };

        struct Transition
        {
            bool accepted = false;
            bool load = false;
            bool unload = false;
            State state;
        };

        Transition acquire(std::string_view cellId)
        {
            if (cellId.empty())
                throw std::invalid_argument("collision cell identity must not be empty");

            State& state = mStates[std::string(cellId)];
            Transition result;
            result.accepted = true;
            result.load = state.refCount == 0;
            if (result.load)
                state.generation = nextGeneration(state.generation);
            ++state.refCount;
            result.state = state;
            return result;
        }

        Transition release(std::string_view cellId)
        {
            const auto it = mStates.find(std::string(cellId));
            if (it == mStates.end() || it->second.refCount == 0)
                return {};

            Transition result;
            result.accepted = true;
            --it->second.refCount;
            result.unload = it->second.refCount == 0;
            if (result.unload)
                it->second.generation = nextGeneration(it->second.generation);
            result.state = it->second;
            return result;
        }

        State touch(std::string_view cellId)
        {
            const auto it = mStates.find(std::string(cellId));
            if (it == mStates.end() || it->second.refCount == 0)
                return {};
            it->second.generation = nextGeneration(it->second.generation);
            return it->second;
        }

        std::vector<std::string> clear()
        {
            std::vector<std::string> unloaded;
            for (auto& [cellId, state] : mStates)
            {
                if (state.refCount == 0)
                    continue;
                state.refCount = 0;
                state.generation = nextGeneration(state.generation);
                unloaded.push_back(cellId);
            }
            std::sort(unloaded.begin(), unloaded.end());
            return unloaded;
        }

        State state(std::string_view cellId) const
        {
            const auto it = mStates.find(std::string(cellId));
            return it != mStates.end() ? it->second : State{};
        }

    private:
        static std::uint64_t nextGeneration(std::uint64_t value)
        {
            ++value;
            return value == 0 ? 1 : value;
        }

        std::unordered_map<std::string, State> mStates;
    };
}

#endif
