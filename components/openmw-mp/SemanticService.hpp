#ifndef OPENMW_MP_SEMANTICSERVICE_HPP
#define OPENMW_MP_SEMANTICSERVICE_HPP

#include <cstdint>
#include <optional>
#include <utility>

namespace mwmp
{
    /// Shared client-side ordering decision for revisioned authoritative state.
    enum class RevisionDecision : std::uint8_t
    {
        AcceptedNewer,
        IdenticalReplay,
        Stale,
        Conflict,
    };

    /// Minimal result/application barrier shared by typed semantic services.
    /// State must expose a `revision` member and equality. Receiving a state is
    /// separate from consuming it so packets may safely arrive before the
    /// corresponding engine object exists.
    template <class State>
    class RevisionedStateGate
    {
    public:
        void reset()
        {
            mLatest.reset();
            mPending = false;
        }

        RevisionDecision receive(State state)
        {
            if (!mLatest || state.revision > mLatest->revision)
            {
                mLatest = std::move(state);
                mPending = true;
                return RevisionDecision::AcceptedNewer;
            }
            if (state.revision < mLatest->revision)
                return RevisionDecision::Stale;
            if (state == *mLatest)
                return RevisionDecision::IdenticalReplay;
            return RevisionDecision::Conflict;
        }

        bool hasState() const { return mLatest.has_value(); }
        bool hasPending() const { return mPending; }
        const std::optional<State>& latest() const { return mLatest; }

        std::optional<State> takePending()
        {
            if (!mPending || !mLatest)
                return std::nullopt;
            mPending = false;
            return mLatest;
        }

    private:
        std::optional<State> mLatest;
        bool mPending = false;
    };
}

#endif
