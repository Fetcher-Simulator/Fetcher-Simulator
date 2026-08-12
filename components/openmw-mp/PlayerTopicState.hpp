#ifndef OPENMW_MP_PLAYERTOPICSTATE_HPP
#define OPENMW_MP_PLAYERTOPICSTATE_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mwmp
{
    inline constexpr std::uint16_t PlayerTopicStateSchemaVersion = 1;
    inline constexpr std::size_t MaximumKnownTopics = 4096;
    inline constexpr std::size_t MaximumTopicIdLength = 256;

    struct PlayerTopicState
    {
        std::uint16_t schemaVersion = PlayerTopicStateSchemaVersion;
        std::uint64_t revision = 0;
        std::vector<std::string> knownTopicIds;

        bool operator==(const PlayerTopicState&) const = default;
    };

    enum class TopicStateError : std::uint8_t
    {
        None,
        UnsupportedVersion,
        RevisionOverflow,
        TooManyTopics,
        InvalidTopicId,
        NonCanonicalTopics,
    };

    std::string canonicalizeTopicId(std::string_view id);
    std::vector<std::string> canonicalizeTopicIds(const std::vector<std::string>& ids);
    TopicStateError validatePlayerTopicState(const PlayerTopicState& state);
    std::string_view getTopicStateErrorCode(TopicStateError error);
}

#endif
