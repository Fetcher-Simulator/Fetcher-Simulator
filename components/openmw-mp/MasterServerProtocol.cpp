#include "MasterServerProtocol.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <regex>

#include <picojson.h>

namespace mwmp
{
    namespace
    {
        constexpr int MaxPublicPlayers = 4096;

        const picojson::value* find(const picojson::object& object, const char* key)
        {
            const auto it = object.find(key);
            return it == object.end() ? nullptr : &it->second;
        }

        bool readString(const picojson::object& object, const char* key, std::string& result, bool allowEmpty = false)
        {
            const picojson::value* value = find(object, key);
            if (value == nullptr || !value->is<std::string>())
                return false;
            result = value->get<std::string>();
            return allowEmpty || !result.empty();
        }

        bool readInt(const picojson::object& object, const char* key, int& result, int minimum, int maximum)
        {
            const picojson::value* value = find(object, key);
            if (value == nullptr || !value->is<double>())
                return false;
            const double number = value->get<double>();
            if (!std::isfinite(number) || std::floor(number) != number || number < minimum || number > maximum)
                return false;
            result = static_cast<int>(number);
            return true;
        }

        bool parseEntry(const picojson::value& value, PublicServerEntry& entry)
        {
            if (!value.is<picojson::object>())
                return false;

            const picojson::object& object = value.get<picojson::object>();
            int port = 0;
            if (!readString(object, "id", entry.id) || !readString(object, "host", entry.host)
                || !readInt(object, "port", port, 1, 65535) || !readString(object, "name", entry.name)
                || !readString(object, "build_version", entry.buildVersion)
                || !readInt(object, "protocol_version", entry.protocolVersion, 1, std::numeric_limits<int>::max())
                || !readString(object, "game_mode", entry.gameMode)
                || !readString(object, "country", entry.country, true)
                || !readInt(object, "current_players", entry.currentPlayers, 0, MaxPublicPlayers)
                || !readInt(object, "max_players", entry.maxPlayers, 1, MaxPublicPlayers)
                || entry.currentPlayers > entry.maxPlayers)
            {
                return false;
            }

            entry.port = static_cast<std::uint16_t>(port);
            return true;
        }

        template <class T>
        bool orderedLess(const T& left, const T& right, bool ascending)
        {
            return ascending ? left < right : right < left;
        }

        std::size_t utf8SequenceLength(unsigned char lead)
        {
            if ((lead & 0x80u) == 0)
                return 1;
            if ((lead & 0xe0u) == 0xc0u)
                return 2;
            if ((lead & 0xf0u) == 0xe0u)
                return 3;
            if ((lead & 0xf8u) == 0xf0u)
                return 4;
            return 1;
        }
    }

    ServerListParseResult parsePublicServerList(std::string_view json)
    {
        ServerListParseResult result;
        picojson::value root;
        const std::string error = picojson::parse(root, std::string(json));
        if (!error.empty())
        {
            result.error = error;
            return result;
        }
        if (!root.is<picojson::array>())
        {
            result.error = "root value is not an array";
            return result;
        }

        for (const picojson::value& value : root.get<picojson::array>())
        {
            PublicServerEntry entry;
            if (parseEntry(value, entry))
                result.entries.push_back(std::move(entry));
            else
                ++result.skippedEntries;
        }
        return result;
    }

    std::string parseRegistrationToken(std::string_view json)
    {
        picojson::value root;
        if (!picojson::parse(root, std::string(json)).empty() || !root.is<picojson::object>())
            return {};

        std::string token;
        if (!readString(root.get<picojson::object>(), "token", token))
            return {};

        static const std::regex tokenPattern(
            "^[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{"
            "3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$");
        return std::regex_match(token, tokenPattern) ? token : std::string();
    }

    std::string masterServerAuthority(std::string_view url)
    {
        const std::size_t schemeEnd = url.find("://");
        if (schemeEnd == std::string_view::npos)
            return {};
        const std::size_t authorityBegin = schemeEnd + 3;
        const std::size_t authorityEnd = url.find_first_of("/?#", authorityBegin);
        std::string_view authority = url.substr(authorityBegin, authorityEnd - authorityBegin);
        const std::size_t userInfoEnd = authority.rfind('@');
        if (userInfoEnd != std::string_view::npos)
            authority.remove_prefix(userInfoEnd + 1);
        return authority.size() <= 253 ? std::string(authority) : std::string();
    }

    std::string serializeTlsDiagnosticReport(const TlsDiagnosticReport& report)
    {
        picojson::object object;
        object["schema_version"] = picojson::value(1.0);
        object["request_id"] = picojson::value(report.requestId);
        object["build_commit"] = picojson::value(report.buildCommit);
        object["master_host"] = picojson::value(report.masterHost);
        object["resolved_addresses"] = picojson::value(report.resolvedAddresses);
        object["error"] = picojson::value(report.error);
        object["ssl_error"] = picojson::value(static_cast<double>(report.sslError));
        object["ssl_backend_error"] = picojson::value(std::to_string(report.sslBackendError));
        object["client_time_unix"] = picojson::value(static_cast<double>(report.clientTimeUnix));
        object["elapsed_ms"] = picojson::value(static_cast<double>(report.elapsedMs));
        return picojson::value(std::move(object)).serialize();
    }

    bool isProtocolCompatible(const PublicServerEntry& entry)
    {
        return entry.protocolVersion == MultiplayerProtocolVersion;
    }

    bool serverEntryLess(
        const PublicServerEntry& left, const PublicServerEntry& right, ServerSortColumn column, bool ascending)
    {
        switch (column)
        {
            case ServerSortColumn::Name:
                return orderedLess(left.name, right.name, ascending);
            case ServerSortColumn::Players:
                return orderedLess(left.currentPlayers, right.currentPlayers, ascending);
            case ServerSortColumn::BuildVersion:
                return orderedLess(left.buildVersion, right.buildVersion, ascending);
            case ServerSortColumn::GameMode:
                return orderedLess(left.gameMode, right.gameMode, ascending);
            case ServerSortColumn::Country:
                return orderedLess(left.country, right.country, ascending);
        }
        return false;
    }

    std::vector<std::size_t> sortedServerIndices(
        const std::vector<PublicServerEntry>& entries, ServerSortColumn column, bool ascending)
    {
        std::vector<std::size_t> indices(entries.size());
        std::iota(indices.begin(), indices.end(), 0);
        std::stable_sort(indices.begin(), indices.end(), [&](std::size_t left, std::size_t right) {
            return serverEntryLess(entries[left], entries[right], column, ascending);
        });
        return indices;
    }

    std::string ellipsizeUtf8(std::string_view value, std::size_t maxCodePoints)
    {
        if (maxCodePoints == 0)
            return {};

        std::size_t offset = 0;
        std::size_t codePoints = 0;
        while (offset < value.size() && codePoints < maxCodePoints)
        {
            const std::size_t length = utf8SequenceLength(static_cast<unsigned char>(value[offset]));
            if (offset + length > value.size())
                break;
            offset += length;
            ++codePoints;
        }
        if (offset == value.size())
            return std::string(value);

        if (maxCodePoints == 1)
            return "\xe2\x80\xa6";

        offset = 0;
        for (std::size_t i = 0; i < maxCodePoints - 1 && offset < value.size(); ++i)
            offset += utf8SequenceLength(static_cast<unsigned char>(value[offset]));
        return std::string(value.substr(0, offset)) + "\xe2\x80\xa6";
    }

    std::chrono::seconds registrationRetryBackoff(unsigned attempt)
    {
        constexpr unsigned MaxShift = 6;
        return std::chrono::seconds(1u << std::min(attempt, MaxShift));
    }

    bool heartbeatStatusRequiresRegistration(int httpStatus)
    {
        return httpStatus == 401 || httpStatus == 404;
    }
}
