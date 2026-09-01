#ifndef OPENMW_APPS_OPENMW_SERVER_CONFIGUREDCELL_HPP
#define OPENMW_APPS_OPENMW_SERVER_CONFIGUREDCELL_HPP

#include <charconv>
#include <string>
#include <string_view>
#include <system_error>

namespace mwmp
{
    inline std::string normalizeConfiguredCell(std::string raw)
    {
        std::string_view value(raw);
        std::string_view prefix;
        if (value.size() >= 4 && value.substr(0, 4) == "EXT:")
        {
            prefix = value.substr(0, 4);
            value.remove_prefix(4);
        }

        const std::size_t comma = value.find(',');
        if (comma == std::string_view::npos || value.find(',', comma + 1) != std::string_view::npos)
            return raw;

        auto trim = [](std::string_view part) {
            while (!part.empty() && part.front() == ' ')
                part.remove_prefix(1);
            while (!part.empty() && part.back() == ' ')
                part.remove_suffix(1);
            return part;
        };
        auto isInteger = [](std::string_view part) {
            if (part.empty())
                return false;
            int parsed = 0;
            const char* first = part.data();
            const char* last = first + part.size();
            const auto result = std::from_chars(first, last, parsed);
            return result.ec == std::errc{} && result.ptr == last;
        };

        const std::string_view x = trim(value.substr(0, comma));
        const std::string_view y = trim(value.substr(comma + 1));
        if (!isInteger(x) || !isInteger(y))
            return raw;

        std::string normalized;
        normalized.reserve(prefix.size() + x.size() + y.size() + 1);
        normalized.append(prefix);
        normalized.append(x);
        normalized.push_back(',');
        normalized.append(y);
        return normalized;
    }
}

#endif
