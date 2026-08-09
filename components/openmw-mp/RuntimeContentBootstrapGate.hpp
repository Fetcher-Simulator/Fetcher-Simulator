#ifndef OPENMW_MP_RUNTIMECONTENTBOOTSTRAPGATE_HPP
#define OPENMW_MP_RUNTIMECONTENTBOOTSTRAPGATE_HPP

#include <optional>
#include <string>
#include <utility>

namespace mwmp
{
    /// Retains the selected character's bootstrap payload until authoritative
    /// runtime content has been installed successfully for the current session.
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
            mPendingCharacterData.reset();
            mError.clear();
        }

        void retainCharacterData(CharacterData data) { mPendingCharacterData = std::move(data); }

        bool finish(bool definitionsInstalled, std::string error = {})
        {
            if (!definitionsInstalled)
            {
                mState = State::Failed;
                mError = error.empty() ? "runtime content definitions were not installed" : std::move(error);
                return false;
            }

            if (mState == State::Failed)
                return false;
            mState = State::Complete;
            return true;
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
        bool hasPendingCharacterData() const { return mPendingCharacterData.has_value(); }
        const std::string& error() const { return mError; }

    private:
        State mState = State::Waiting;
        std::optional<CharacterData> mPendingCharacterData;
        std::string mError;
    };
}

#endif
