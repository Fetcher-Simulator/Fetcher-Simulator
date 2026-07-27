#ifndef OPENMW_COMPONENTS_OPENMW_MP_MASTER_SERVER_PROTOCOL_HPP
#define OPENMW_COMPONENTS_OPENMW_MP_MASTER_SERVER_PROTOCOL_HPP

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mwmp
{
    inline constexpr int MultiplayerProtocolVersion = 1;
    inline constexpr std::string_view MultiplayerBuildVersion = "0.1.0";
    inline constexpr std::string_view DefaultMasterServerUrl = "https://master.fetchers.org";

    struct PublicServerEntry
    {
        std::string id;
        std::string host;
        std::uint16_t port = 0;
        std::string name;
        std::string buildVersion;
        int protocolVersion = 0;
        std::string gameMode;
        std::string country;
        int currentPlayers = 0;
        int maxPlayers = 0;
    };

    struct ServerListParseResult
    {
        std::vector<PublicServerEntry> entries;
        std::size_t skippedEntries = 0;
        std::string error;
    };

    enum class ServerSortColumn
    {
        Name,
        Players,
        BuildVersion,
        GameMode,
        Country,
    };

    ServerListParseResult parsePublicServerList(std::string_view json);
    std::string parseRegistrationToken(std::string_view json);

    bool isProtocolCompatible(const PublicServerEntry& entry);
    bool serverEntryLess(
        const PublicServerEntry& left, const PublicServerEntry& right, ServerSortColumn column, bool ascending);
    std::vector<std::size_t> sortedServerIndices(
        const std::vector<PublicServerEntry>& entries, ServerSortColumn column, bool ascending);

    std::string ellipsizeUtf8(std::string_view value, std::size_t maxCodePoints);

    std::chrono::seconds registrationRetryBackoff(unsigned attempt);
    bool heartbeatStatusRequiresRegistration(int httpStatus);
}

#endif
