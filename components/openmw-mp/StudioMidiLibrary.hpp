#ifndef OPENMW_MP_STUDIOMIDILIBRARY_HPP
#define OPENMW_MP_STUDIOMIDILIBRARY_HPP

#include "Sha256.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mwmp::studiomidi
{
    inline constexpr std::size_t MaxFileSize = 512 * 1024;
    inline constexpr std::size_t MaxSongs = 1024;

    struct Entry
    {
        std::string name;
        std::size_t size;
        std::string hash;
    };

    inline bool validName(std::string_view name)
    {
        if (name.empty() || name.size() > 187 || name.front() == '/' || name.back() == '/')
            return false;
        for (unsigned char c : name)
            if (c < 32 || c == 127 || c == '\\' || c == ':')
                return false;
        std::size_t start = 0;
        while (start < name.size())
        {
            auto end = name.find('/', start);
            auto part = name.substr(start, end == name.npos ? name.size() - start : end - start);
            if (part.empty() || part == "." || part == "..")
                return false;
            if (end == name.npos)
                break;
            start = end + 1;
        }
        const auto dot = name.find_last_of('.');
        if (dot == name.npos)
            return false;
        std::string extension(name.substr(dot));
        std::transform(extension.begin(), extension.end(), extension.begin(),
            [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; });
        return extension == ".mid" || extension == ".midi";
    }

    // RMID is a RIFF container around an ordinary SMF. Strip only that container;
    // retain the original server file and its complete MIDI event stream.
    inline std::optional<std::string> unwrap(std::string bytes)
    {
        if (bytes.starts_with("MThd"))
            return bytes;
        if (bytes.size() < 12 || bytes.substr(0, 4) != "RIFF" || bytes.substr(8, 4) != "RMID")
            return std::nullopt;
        auto u32 = [&](std::size_t at) {
            return std::uint32_t(static_cast<unsigned char>(bytes[at]))
                | (std::uint32_t(static_cast<unsigned char>(bytes[at + 1])) << 8)
                | (std::uint32_t(static_cast<unsigned char>(bytes[at + 2])) << 16)
                | (std::uint32_t(static_cast<unsigned char>(bytes[at + 3])) << 24);
        };
        const std::uint64_t end = std::uint64_t(u32(4)) + 8;
        if (end > bytes.size() || end < 12)
            return std::nullopt;
        for (std::size_t at = 12; at + 8 <= end;)
        {
            const std::uint64_t length = u32(at + 4);
            if (at + 8 + length > end)
                return std::nullopt;
            if (bytes.compare(at, 4, "data") == 0)
            {
                std::string midi = bytes.substr(at + 8, length);
                return midi.starts_with("MThd") ? std::optional(std::move(midi)) : std::nullopt;
            }
            at += 8 + length + (length & 1);
        }
        return std::nullopt;
    }

    inline std::optional<std::string> read(const std::filesystem::path& directory, std::string_view name)
    {
        if (!validName(name))
            return std::nullopt;
        std::error_code ec;
        const auto root = std::filesystem::weakly_canonical(directory, ec);
        if (ec)
            return std::nullopt;
        const auto path = std::filesystem::weakly_canonical(
            root / std::filesystem::path(std::u8string(name.begin(), name.end())), ec);
        if (ec)
            return std::nullopt;
        const auto relative = path.lexically_relative(root);
        if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
            return std::nullopt;
        if (!std::filesystem::is_regular_file(path, ec) || ec)
            return std::nullopt;
        const auto size = std::filesystem::file_size(path, ec);
        if (ec || size < 4 || size > MaxFileSize)
            return std::nullopt;
        std::ifstream file(path, std::ios::binary);
        std::string bytes(size, '\0');
        if (!file.read(bytes.data(), bytes.size()) || file.peek() != std::char_traits<char>::eof())
            return std::nullopt;
        return unwrap(std::move(bytes));
    }

    inline std::vector<Entry> scan(const std::filesystem::path& directory)
    {
        std::vector<std::string> names;
        std::error_code ec;
        std::size_t visited = 0;
        for (std::filesystem::recursive_directory_iterator it(directory,
                 std::filesystem::directory_options::skip_permission_denied, ec), end;
             !ec && it != end && visited < 4096; it.increment(ec), ++visited)
        {
            if (!it->is_regular_file(ec) || ec)
            {
                ec.clear();
                continue;
            }
            const auto path = it->path().lexically_relative(directory).generic_u8string();
            std::string name(reinterpret_cast<const char*>(path.data()), path.size());
            if (validName(name))
                names.push_back(std::move(name));
        }
        std::sort(names.begin(), names.end());
        std::vector<Entry> entries;
        for (const auto& name : names)
        {
            if (auto bytes = read(directory, name))
                entries.push_back({ name, bytes->size(), crypto::sha256hex(*bytes) });
            if (entries.size() == MaxSongs)
                break;
        }
        return entries;
    }
}
#endif
