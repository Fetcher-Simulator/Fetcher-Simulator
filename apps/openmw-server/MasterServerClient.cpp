#include "MasterServerClient.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <sstream>

#include <components/debug/debuglog.hpp>
#include <components/openmw-mp/MasterServerProtocol.hpp>

#include <httplib.h>

namespace mwmp
{
    namespace
    {
        std::string jsonEscape(const std::string& value)
        {
            std::string result;
            result.reserve(value.size() + 4);
            for (const char character : value)
            {
                switch (character)
                {
                    case '"':
                        result += "\\\"";
                        break;
                    case '\\':
                        result += "\\\\";
                        break;
                    case '\n':
                        result += "\\n";
                        break;
                    case '\r':
                        result += "\\r";
                        break;
                    case '\t':
                        result += "\\t";
                        break;
                    default:
                        if (static_cast<unsigned char>(character) < 0x20)
                        {
                            char buffer[8];
                            std::snprintf(buffer, sizeof(buffer), "\\u%04x",
                                static_cast<unsigned>(static_cast<unsigned char>(character)));
                            result += buffer;
                        }
                        else
                            result += character;
                }
            }
            return result;
        }

        std::string registerBody(const MasterServerClient::Config& config)
        {
            std::ostringstream stream;
            stream << '{' << "\"name\":\"" << jsonEscape(config.serverName) << "\","
                   << "\"port\":" << config.port << ',' << "\"max_players\":" << config.maxPlayers << ','
                   << "\"build_version\":\"" << jsonEscape(config.buildVersion) << "\","
                   << "\"protocol_version\":" << config.protocolVersion << ',' << "\"game_mode\":\""
                   << jsonEscape(config.gameMode) << "\"";
            if (!config.lanAddress.empty())
                stream << ",\"lan_address\":\"" << jsonEscape(config.lanAddress) << "\"";
            stream << '}';
            return stream.str();
        }

        std::string heartbeatBody(const std::string& token, int currentPlayers)
        {
            return "{\"token\":\"" + jsonEscape(token) + "\",\"current_players\":" + std::to_string(currentPlayers)
                + '}';
        }

        std::string unregisterBody(const std::string& token)
        {
            return "{\"token\":\"" + jsonEscape(token) + "\"}";
        }

        httplib::Client makeClient(const std::string& url)
        {
            httplib::Client client(url);
            client.set_connection_timeout(3);
            client.set_read_timeout(5);
            client.set_write_timeout(5);
            client.enable_server_certificate_verification(true);
            return client;
        }

        std::chrono::milliseconds jitteredBackoff(unsigned attempt, std::mt19937& random)
        {
            const auto base = std::chrono::duration_cast<std::chrono::milliseconds>(registrationRetryBackoff(attempt));
            std::uniform_int_distribution<int> jitterPercent(-10, 10);
            return base + base * jitterPercent(random) / 100;
        }
    }

    MasterServerClient::~MasterServerClient()
    {
        unregister();
    }

    void MasterServerClient::registerAsync(const Config& config)
    {
        unregister();
        if (config.masterUrl.empty())
            return;

        {
            std::lock_guard lock(mMutex);
            mConfig = config;
            mToken.clear();
            mCurrentPlayers = 0;
            mState = State::Registering;
        }
        mWorker = std::jthread([this](std::stop_token stopToken) { workerLoop(stopToken); });
    }

    void MasterServerClient::tickHeartbeat(float, int currentPlayers)
    {
        std::lock_guard lock(mMutex);
        mCurrentPlayers = std::max(0, currentPlayers);
    }

    void MasterServerClient::unregister()
    {
        if (!mWorker.joinable())
            return;

        {
            std::lock_guard lock(mMutex);
            mState = State::Stopping;
        }
        mWorker.request_stop();
        mCondition.notify_all();
        mWorker.join();
    }

    bool MasterServerClient::isRegistered() const
    {
        std::lock_guard lock(mMutex);
        return mState == State::Registered && !mToken.empty();
    }

    MasterServerClient::State MasterServerClient::state() const
    {
        std::lock_guard lock(mMutex);
        return mState;
    }

    void MasterServerClient::workerLoop(std::stop_token stopToken)
    {
        std::random_device randomDevice;
        std::mt19937 random(randomDevice());
        unsigned retryAttempt = 0;

        while (!stopToken.stop_requested())
        {
            Config config;
            {
                std::lock_guard lock(mMutex);
                config = mConfig;
                mState = State::Registering;
            }

            std::optional<std::string> token = registerServer(config);
            if (!token)
            {
                const auto delay = jitteredBackoff(retryAttempt++, random);
                std::unique_lock lock(mMutex);
                mState = State::RetryWaiting;
                mCondition.wait_for(lock, stopToken, delay, [] { return false; });
                continue;
            }

            retryAttempt = 0;
            {
                std::lock_guard lock(mMutex);
                mToken = *token;
                mState = State::Registered;
            }
            Log(Debug::Info) << "[MasterServer] registered \"" << config.serverName << "\" at " << config.masterUrl;

            while (!stopToken.stop_requested())
            {
                {
                    std::unique_lock lock(mMutex);
                    mCondition.wait_for(
                        lock, stopToken, std::chrono::seconds(HeartbeatIntervalSeconds), [] { return false; });
                }
                if (stopToken.stop_requested())
                    break;

                int currentPlayers = 0;
                {
                    std::lock_guard lock(mMutex);
                    currentPlayers = mCurrentPlayers;
                    token = mToken;
                }

                const HeartbeatResult result = sendHeartbeat(config, *token, currentPlayers);
                if (result == HeartbeatResult::RegisterAgain)
                {
                    Log(Debug::Info) << "[MasterServer] registration expired; registering again";
                    std::lock_guard lock(mMutex);
                    mToken.clear();
                    mState = State::Registering;
                    break;
                }
            }
        }

        Config config;
        std::string token;
        {
            std::lock_guard lock(mMutex);
            config = mConfig;
            token = mToken;
        }
        if (!token.empty())
            sendUnregister(config, token);

        {
            std::lock_guard lock(mMutex);
            mToken.clear();
            mState = State::Disabled;
        }
    }

    std::optional<std::string> MasterServerClient::registerServer(const Config& config)
    {
        try
        {
            auto client = makeClient(config.masterUrl);
            const auto response = client.Post("/v1/servers/register", registerBody(config), "application/json");
            if (!response)
            {
                Log(Debug::Warning) << "[MasterServer] registration failed: " << httplib::to_string(response.error());
                return std::nullopt;
            }
            if (response->status != 200 && response->status != 201)
            {
                Log(Debug::Warning) << "[MasterServer] registration returned HTTP " << response->status;
                return std::nullopt;
            }

            const std::string token = parseRegistrationToken(response->body);
            if (token.empty())
            {
                Log(Debug::Warning) << "[MasterServer] registration response did not "
                                       "contain a valid token";
                return std::nullopt;
            }
            return token;
        }
        catch (const std::exception& error)
        {
            Log(Debug::Warning) << "[MasterServer] registration error: " << error.what();
            return std::nullopt;
        }
    }

    MasterServerClient::HeartbeatResult MasterServerClient::sendHeartbeat(
        const Config& config, const std::string& token, int currentPlayers)
    {
        try
        {
            auto client = makeClient(config.masterUrl);
            const auto response
                = client.Post("/v1/servers/heartbeat", heartbeatBody(token, currentPlayers), "application/json");
            if (!response)
            {
                Log(Debug::Warning) << "[MasterServer] heartbeat failed: " << httplib::to_string(response.error());
                return HeartbeatResult::RetryLater;
            }
            if (heartbeatStatusRequiresRegistration(response->status))
                return HeartbeatResult::RegisterAgain;
            if (response->status != 200)
            {
                Log(Debug::Warning) << "[MasterServer] heartbeat returned HTTP " << response->status;
                return HeartbeatResult::RetryLater;
            }
            return HeartbeatResult::Success;
        }
        catch (const std::exception& error)
        {
            Log(Debug::Warning) << "[MasterServer] heartbeat error: " << error.what();
            return HeartbeatResult::RetryLater;
        }
    }

    void MasterServerClient::sendUnregister(const Config& config, const std::string& token)
    {
        try
        {
            auto client = makeClient(config.masterUrl);
            const auto response = client.Post("/v1/servers/unregister", unregisterBody(token), "application/json");
            if (!response || response->status != 200)
            {
                Log(Debug::Warning) << "[MasterServer] unregistration failed"
                                    << (response ? " with HTTP " + std::to_string(response->status) : "");
                return;
            }
            Log(Debug::Info) << "[MasterServer] unregistered cleanly";
        }
        catch (const std::exception& error)
        {
            Log(Debug::Warning) << "[MasterServer] unregistration error: " << error.what();
        }
    }
}
