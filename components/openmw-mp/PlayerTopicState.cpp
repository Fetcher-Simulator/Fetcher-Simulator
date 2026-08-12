#include "PlayerTopicState.hpp"

#include "PlayerCrimeState.hpp"

#include <algorithm>

namespace mwmp
{
    namespace
    {
        bool isValidUtf8(std::string_view value)
        {
            for (std::size_t i = 0; i < value.size();)
            {
                const auto first = static_cast<unsigned char>(value[i]);
                std::size_t continuationCount = 0;
                std::uint32_t codepoint = 0;
                if (first <= 0x7f)
                {
                    ++i;
                    continue;
                }
                if ((first & 0xe0) == 0xc0)
                {
                    continuationCount = 1;
                    codepoint = first & 0x1f;
                }
                else if ((first & 0xf0) == 0xe0)
                {
                    continuationCount = 2;
                    codepoint = first & 0x0f;
                }
                else if ((first & 0xf8) == 0xf0)
                {
                    continuationCount = 3;
                    codepoint = first & 0x07;
                }
                else
                    return false;
                if (i + continuationCount >= value.size())
                    return false;
                for (std::size_t offset = 1; offset <= continuationCount; ++offset)
                {
                    const auto next = static_cast<unsigned char>(value[i + offset]);
                    if ((next & 0xc0) != 0x80)
                        return false;
                    codepoint = (codepoint << 6) | (next & 0x3f);
                }
                if ((continuationCount == 1 && codepoint < 0x80)
                    || (continuationCount == 2 && codepoint < 0x800)
                    || (continuationCount == 3 && codepoint < 0x10000)
                    || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff))
                    return false;
                i += continuationCount + 1;
            }
            return true;
        }

        bool validId(std::string_view id)
        {
            return !id.empty() && id.size() <= MaximumTopicIdLength
                && id.find('\0') == std::string_view::npos && isValidUtf8(id);
        }
    }

    std::string canonicalizeTopicId(std::string_view id)
    {
        std::string result(id);
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c);
        });
        return result;
    }

    std::vector<std::string> canonicalizeTopicIds(const std::vector<std::string>& ids)
    {
        std::vector<std::string> result;
        result.reserve(ids.size());
        for (const std::string& id : ids)
            result.push_back(canonicalizeTopicId(id));
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }

    TopicStateError validatePlayerTopicState(const PlayerTopicState& state)
    {
        if (state.schemaVersion != PlayerTopicStateSchemaVersion)
            return TopicStateError::UnsupportedVersion;
        if (state.revision > MaximumPersistedRevision)
            return TopicStateError::RevisionOverflow;
        if (state.knownTopicIds.size() > MaximumKnownTopics)
            return TopicStateError::TooManyTopics;
        for (const std::string& id : state.knownTopicIds)
        {
            if (!validId(id))
                return TopicStateError::InvalidTopicId;
        }
        if (canonicalizeTopicIds(state.knownTopicIds) != state.knownTopicIds)
            return TopicStateError::NonCanonicalTopics;
        return TopicStateError::None;
    }

    std::string_view getTopicStateErrorCode(TopicStateError error)
    {
        switch (error)
        {
            case TopicStateError::None: return "none";
            case TopicStateError::UnsupportedVersion: return "topic_unsupported_version";
            case TopicStateError::RevisionOverflow: return "topic_revision_overflow";
            case TopicStateError::TooManyTopics: return "topic_too_many_topics";
            case TopicStateError::InvalidTopicId: return "topic_invalid_id";
            case TopicStateError::NonCanonicalTopics: return "topic_noncanonical_state";
        }
        return "topic_unknown_error";
    }
}
