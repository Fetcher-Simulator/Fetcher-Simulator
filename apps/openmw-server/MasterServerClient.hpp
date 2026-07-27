#ifndef OPENMW_APPS_OPENMW_SERVER_MASTER_SERVER_CLIENT_HPP
#define OPENMW_APPS_OPENMW_SERVER_MASTER_SERVER_CLIENT_HPP

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>

namespace mwmp
{
    class MasterServerClient
    {
    public:
        static constexpr float HeartbeatIntervalSeconds = 30.f;

        enum class State
        {
            Disabled,
            Registering,
            Registered,
            RetryWaiting,
            Stopping,
        };

        struct Config
        {
            std::string masterUrl;
            std::string serverName;
            int port = 25565;
            int maxPlayers = 32;
            std::string buildVersion;
            int protocolVersion = 1;
            std::string gameMode = "Co-op";
            std::string lanAddress;
        };

        MasterServerClient() = default;
        ~MasterServerClient();

        MasterServerClient(const MasterServerClient&) = delete;
        MasterServerClient& operator=(const MasterServerClient&) = delete;

        void registerAsync(const Config& config);
        void tickHeartbeat(float dt, int currentPlayers);
        void unregister();

        bool isRegistered() const;
        State state() const;

    private:
        enum class HeartbeatResult
        {
            Success,
            RetryLater,
            RegisterAgain,
        };

        void workerLoop(std::stop_token stopToken);
        std::optional<std::string> registerServer(const Config& config);
        HeartbeatResult sendHeartbeat(const Config& config, const std::string& token, int currentPlayers);
        void sendUnregister(const Config& config, const std::string& token);

        mutable std::mutex mMutex;
        std::condition_variable_any mCondition;
        std::jthread mWorker;
        Config mConfig;
        std::string mToken;
        int mCurrentPlayers = 0;
        State mState = State::Disabled;
    };
}

#endif
