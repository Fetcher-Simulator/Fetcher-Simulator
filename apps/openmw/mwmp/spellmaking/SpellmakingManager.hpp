#pragma once
#include <chrono>
#include <components/openmw-mp/Records/SpellmakingProtocol.hpp>
#include <functional>
#include <optional>
namespace mwmp
{
    class NetworkClient;
    class RecordCreationManager;
    class PlayerSync;
    class SpellmakingManager
    {
    public:
        using Completion = std::function<void(const records::SpellmakingResult&)>;
        SpellmakingManager(NetworkClient&, RecordCreationManager&, PlayerSync&);
        bool request(records::SpellmakingRequest, Completion, std::string& error);
        void onResult(records::SpellmakingResult);
        void update();
        void cancelAll();
        bool hasPending() const { return mPending.has_value(); }

    private:
        struct Pending
        {
            records::SpellmakingRequest request;
            Completion completion;
            std::optional<records::SpellmakingResult> result;
            std::chrono::steady_clock::time_point lastSend;
        };
        bool ready(const records::SpellmakingResult&) const;
        NetworkClient& mClient;
        RecordCreationManager& mRecords;
        PlayerSync& mPlayer;
        std::optional<Pending> mPending;
    };
}
