#ifndef OPENMW_MP_RUNTIMECONTENTBOOTSTRAPGATE_HPP
#define OPENMW_MP_RUNTIMECONTENTBOOTSTRAPGATE_HPP

#include <optional>
#include <string>
#include <utility>

namespace mwmp
{
    /// Retains the selected character's bootstrap payload until authoritative
    /// runtime content definitions and server-selected executable policy are
    /// both ready for the current session.
    template <class CharacterData>
    class RuntimeContentBootstrapGate
    {
    public:
        enum class State
        {
            Waiting,
            Complete,
            Failed,
        };

        void reset()
        {
            mState = State::Waiting;
            mRuntimeContentReady = false;
            mServerLuaReady = false;
            mPendingCharacterData.reset();
            mError.clear();
        }

        void retainCharacterData(CharacterData data) { mPendingCharacterData = std::move(data); }

        bool finishRuntimeContent(bool definitionsInstalled, std::string error = {})
        {
            if (!definitionsInstalled)
                return fail(error.empty() ? "runtime content definitions were not installed" : std::move(error));
            if (mState == State::Failed)
                return false;
            mRuntimeContentReady = true;
            updateState();
            return true;
        }

        bool finishServerLua(bool packagesActivated, std::string error = {})
        {
            if (!packagesActivated)
                return fail(error.empty() ? "server Lua packages were not activated" : std::move(error));
            if (mState == State::Failed)
                return false;
            mServerLuaReady = true;
            updateState();
            return true;
        }

        /// Compatibility helper for callers that have no independent executable-policy gate.
        bool finish(bool definitionsInstalled, std::string error = {})
        {
            if (!definitionsInstalled)
                return finishRuntimeContent(false, std::move(error));
            return finishRuntimeContent(true) && finishServerLua(true);
        }

        std::optional<CharacterData> takeReadyCharacterData()
        {
            if (mState != State::Complete || !mPendingCharacterData)
                return std::nullopt;
            std::optional<CharacterData> result(std::move(mPendingCharacterData));
            mPendingCharacterData.reset();
            return result;
        }

        State state() const { return mState; }
        bool isContentReady() const { return mState == State::Complete; }
        bool isRuntimeContentReady() const { return mRuntimeContentReady; }
        bool isServerLuaReady() const { return mServerLuaReady; }
        bool hasPendingCharacterData() const { return mPendingCharacterData.has_value(); }
        const std::string& error() const { return mError; }

    private:
        bool fail(std::string error)
        {
            mState = State::Failed;
            if (mError.empty())
                mError = std::move(error);
            return false;
        }

        void updateState()
        {
            if (mRuntimeContentReady && mServerLuaReady)
                mState = State::Complete;
        }

        State mState = State::Waiting;
        bool mRuntimeContentReady = false;
        bool mServerLuaReady = false;
        std::optional<CharacterData> mPendingCharacterData;
        std::string mError;
    };
}

#endif
