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
    // Version 3 adds the explicit runtime-content bootstrap completion barrier.
    // Version 4 adds independently versioned server-supplied OpenMW Lua package
    // negotiation and bounded manifest/chunk/bootstrap packets.
    // Version 5 adds authoritative revisioned PlayerBounty semantic state.
    // Version 6 adds authoritative revisioned player known-topic state.
    // Version 7 adds typed faction transitions and authoritative faction state.
    // Version 8 adds bootstrap-only trusted static Clothing record overrides.
    // Protocol 9 adds an atomic mechanics-grade observation snapshot. Protocol
    // 8 peers cannot safely reconstruct this state from the independent
    // position, stats, effects, and presentation lanes. Protocol 10 extends
    // that same actor-authority snapshot with effective Alarm, recursive
    // player-follower membership, and canonical combat-target identity.
    // Protocol 11 carries the original validated combat proposal's hit geometry
    // back in ActorCombatResult so blood/impact presentation can follow the
    // server-accepted canonical victim instead of a coarse refId/mpNum guess.
    inline constexpr int MultiplayerProtocolVersion = 11;
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
