#include "PlayerFactionState.hpp"

#include "PlayerCrimeState.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace mwmp
{
    namespace
    {
        constexpr std::string_view RequestMagic = "OMFQ";
        constexpr std::string_view ResultMagic = "OMFR";

        bool isValidUtf8(std::string_view value)
        {
            for (std::size_t i = 0; i < value.size();)
            {
                const auto lead = static_cast<unsigned char>(value[i]);
                std::size_t length = 0;
                std::uint32_t codepoint = 0;
                if (lead <= 0x7f)
                {
                    length = 1;
                    codepoint = lead;
                }
                else if ((lead & 0xe0) == 0xc0)
                {
                    length = 2;
                    codepoint = lead & 0x1f;
                }
                else if ((lead & 0xf0) == 0xe0)
                {
                    length = 3;
                    codepoint = lead & 0x0f;
                }
                else if ((lead & 0xf8) == 0xf0)
                {
                    length = 4;
                    codepoint = lead & 0x07;
                }
                else
                    return false;
                if (i + length > value.size())
                    return false;
                for (std::size_t offset = 1; offset < length; ++offset)
                {
                    const auto continuation = static_cast<unsigned char>(value[i + offset]);
                    if ((continuation & 0xc0) != 0x80)
                        return false;
                    codepoint = (codepoint << 6) | (continuation & 0x3f);
                }
                if ((length == 2 && codepoint < 0x80) || (length == 3 && codepoint < 0x800)
                    || (length == 4 && codepoint < 0x10000) || codepoint > 0x10ffff
                    || (codepoint >= 0xd800 && codepoint <= 0xdfff))
                    return false;
                i += length;
            }
            return true;
        }

        bool validId(std::string_view id)
        {
            return !id.empty() && id.size() <= MaximumFactionIdLength && id.find('\0') == std::string_view::npos
                && isValidUtf8(id);
        }

        bool isDefault(const PlayerFactionEntry& entry)
        {
            return entry.rank == -1 && entry.reputation == 0 && !entry.expelled;
        }

        bool validKind(FactionMutationKind kind)
        {
            return kind >= FactionMutationKind::JoinFaction && kind <= FactionMutationKind::ClearFactionExpulsion;
        }

        class Writer
        {
        public:
            void u8(std::uint8_t value) { mBytes.push_back(static_cast<char>(value)); }
            void u16(std::uint16_t value)
            {
                u8(static_cast<std::uint8_t>(value));
                u8(static_cast<std::uint8_t>(value >> 8));
            }
            void u32(std::uint32_t value)
            {
                for (int shift = 0; shift < 32; shift += 8)
                    u8(static_cast<std::uint8_t>(value >> shift));
            }
            void u64(std::uint64_t value)
            {
                for (int shift = 0; shift < 64; shift += 8)
                    u8(static_cast<std::uint8_t>(value >> shift));
            }
            void i32(std::int32_t value) { u32(std::bit_cast<std::uint32_t>(value)); }
            void i64(std::int64_t value) { u64(std::bit_cast<std::uint64_t>(value)); }
            void string(std::string_view value)
            {
                if (value.size() > std::numeric_limits<std::uint32_t>::max())
                    throw std::length_error("Faction semantic string is too long");
                u32(static_cast<std::uint32_t>(value.size()));
                mBytes.append(value);
            }
            void fixed(std::string_view value) { mBytes.append(value); }
            std::string take() { return std::move(mBytes); }

        private:
            std::string mBytes;
        };

        class Reader
        {
        public:
            explicit Reader(std::string_view bytes)
                : mBytes(bytes)
            {
            }

            std::uint8_t u8()
            {
                need(1);
                return static_cast<std::uint8_t>(mBytes[mPosition++]);
            }
            std::uint16_t u16()
            {
                const std::uint16_t first = u8();
                return static_cast<std::uint16_t>(first | (static_cast<std::uint16_t>(u8()) << 8));
            }
            std::uint32_t u32()
            {
                std::uint32_t value = 0;
                for (int shift = 0; shift < 32; shift += 8)
                    value |= static_cast<std::uint32_t>(u8()) << shift;
                return value;
            }
            std::uint64_t u64()
            {
                std::uint64_t value = 0;
                for (int shift = 0; shift < 64; shift += 8)
                    value |= static_cast<std::uint64_t>(u8()) << shift;
                return value;
            }
            std::int32_t i32() { return std::bit_cast<std::int32_t>(u32()); }
            std::int64_t i64() { return std::bit_cast<std::int64_t>(u64()); }
            std::string string(std::size_t maximum)
            {
                const std::uint32_t size = u32();
                if (size > maximum)
                    throw std::runtime_error("Faction semantic string exceeds limit");
                need(size);
                std::string result(mBytes.substr(mPosition, size));
                mPosition += size;
                return result;
            }
            void expect(std::string_view expected)
            {
                need(expected.size());
                if (mBytes.substr(mPosition, expected.size()) != expected)
                    throw std::runtime_error("Invalid faction semantic payload magic");
                mPosition += expected.size();
            }
            void finish() const
            {
                if (mPosition != mBytes.size())
                    throw std::runtime_error("Trailing bytes in faction semantic payload");
            }

        private:
            void need(std::size_t count) const
            {
                if (count > mBytes.size() - mPosition)
                    throw std::runtime_error("Truncated faction semantic payload");
            }

            std::string_view mBytes;
            std::size_t mPosition = 0;
        };

        void writeState(Writer& writer, const PlayerFactionState& state)
        {
            writer.u16(state.schemaVersion);
            writer.u64(state.revision);
            writer.u16(static_cast<std::uint16_t>(state.factions.size()));
            for (const PlayerFactionEntry& entry : state.factions)
            {
                writer.string(entry.factionId);
                writer.i32(entry.rank);
                writer.i32(entry.reputation);
                writer.u8(entry.expelled ? 1 : 0);
            }
        }

        PlayerFactionState readState(Reader& reader)
        {
            PlayerFactionState state;
            state.schemaVersion = reader.u16();
            state.revision = reader.u64();
            const std::uint16_t count = reader.u16();
            if (count > MaximumPlayerFactions)
                throw std::runtime_error("Too many player factions");
            state.factions.resize(count);
            for (PlayerFactionEntry& entry : state.factions)
            {
                entry.factionId = reader.string(MaximumFactionIdLength);
                entry.rank = reader.i32();
                entry.reputation = reader.i32();
                const std::uint8_t expelled = reader.u8();
                if (expelled > 1)
                    throw std::runtime_error("Invalid faction expulsion flag");
                entry.expelled = expelled != 0;
            }
            if (validatePlayerFactionState(state) != FactionError::None)
                throw std::runtime_error("Invalid player faction state");
            return state;
        }

        PlayerFactionEntry entryOrDefault(
            const std::map<std::string, PlayerFactionEntry>& entries, const std::string& id)
        {
            const auto found = entries.find(id);
            if (found != entries.end())
                return found->second;
            PlayerFactionEntry result;
            result.factionId = id;
            return result;
        }
    }

    std::string canonicalizeFactionId(std::string_view id)
    {
        std::string result(id);
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char value) {
            return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : static_cast<char>(value);
        });
        return result;
    }

    PlayerFactionState canonicalizePlayerFactionState(PlayerFactionState state)
    {
        for (PlayerFactionEntry& entry : state.factions)
            entry.factionId = canonicalizeFactionId(entry.factionId);
        std::sort(state.factions.begin(), state.factions.end(),
            [](const auto& left, const auto& right) { return left.factionId < right.factionId; });
        return state;
    }

    FactionMutationRequest canonicalizeFactionMutationRequest(FactionMutationRequest request)
    {
        for (FactionMutation& mutation : request.mutations)
            mutation.factionId = canonicalizeFactionId(mutation.factionId);
        return request;
    }

    FactionError validatePlayerFactionState(const PlayerFactionState& state)
    {
        if (state.schemaVersion != PlayerFactionStateSchemaVersion)
            return FactionError::UnsupportedVersion;
        if (state.revision > MaximumPersistedRevision)
            return FactionError::RevisionOverflow;
        if (state.factions.size() > MaximumPlayerFactions)
            return FactionError::InvalidRequest;

        std::string_view previous;
        for (const PlayerFactionEntry& entry : state.factions)
        {
            if (!validId(entry.factionId) || canonicalizeFactionId(entry.factionId) != entry.factionId)
                return FactionError::InvalidFaction;
            if (!previous.empty() && previous >= entry.factionId)
                return FactionError::InvalidRequest;
            if (entry.rank < -1 || entry.rank > MaximumProtocolFactionRank)
                return FactionError::InvalidRank;
            if (isDefault(entry))
                return FactionError::InvalidRequest;
            previous = entry.factionId;
        }
        return FactionError::None;
    }

    FactionError validateFactionMutationRequest(const FactionMutationRequest& request)
    {
        if (request.protocolVersion != FactionServiceProtocolVersion)
            return FactionError::UnsupportedVersion;
        if (request.requestId.empty() || request.requestId.size() > MaximumSemanticRequestIdLength
            || request.requestId.find('\0') != std::string::npos || !isValidUtf8(request.requestId)
            || request.source.empty() || request.source.size() > MaximumSemanticSourceLength
            || request.source.find('\0') != std::string::npos || !isValidUtf8(request.source)
            || request.mutations.empty() || request.mutations.size() > MaximumFactionMutations)
            return FactionError::InvalidRequest;
        if (request.expectedRevision && *request.expectedRevision > MaximumPersistedRevision)
            return FactionError::StaleRevision;

        for (const FactionMutation& mutation : request.mutations)
        {
            if (!validKind(mutation.kind) || !validId(mutation.factionId)
                || canonicalizeFactionId(mutation.factionId) != mutation.factionId)
                return FactionError::InvalidRequest;
            switch (mutation.kind)
            {
                case FactionMutationKind::SetFactionRank:
                    if (mutation.value < 0 || mutation.value > MaximumProtocolFactionRank)
                        return FactionError::InvalidRank;
                    break;
                case FactionMutationKind::ModifyFactionRank:
                    if (mutation.value == 0 || mutation.value < -MaximumProtocolFactionRank
                        || mutation.value > MaximumProtocolFactionRank)
                        return FactionError::InvalidRank;
                    break;
                case FactionMutationKind::SetFactionReputation:
                case FactionMutationKind::ModifyFactionReputation:
                    if (mutation.value < std::numeric_limits<std::int32_t>::min()
                        || mutation.value > std::numeric_limits<std::int32_t>::max())
                        return FactionError::InvalidReputation;
                    break;
                default:
                    if (mutation.value != 0)
                        return FactionError::InvalidRequest;
                    break;
            }
        }
        return FactionError::None;
    }

    std::string_view getFactionErrorCode(FactionError error)
    {
        switch (error)
        {
            case FactionError::None:
                return "none";
            case FactionError::InvalidRequest:
                return "faction_invalid_request";
            case FactionError::UnsupportedVersion:
                return "faction_unsupported_version";
            case FactionError::StaleRevision:
                return "faction_stale_revision";
            case FactionError::InvalidFaction:
                return "faction_invalid_faction";
            case FactionError::InvalidRank:
                return "faction_invalid_rank";
            case FactionError::InvalidReputation:
                return "faction_invalid_reputation";
            case FactionError::InvalidTransition:
                return "faction_invalid_transition";
            case FactionError::Unauthorized:
                return "faction_unauthorized";
            case FactionError::DuplicateConflict:
                return "faction_duplicate_conflict";
            case FactionError::PersistenceFailure:
                return "faction_persistence_failure";
            case FactionError::RevisionOverflow:
                return "faction_revision_overflow";
            case FactionError::CorruptStoredResult:
                return "faction_corrupt_stored_result";
        }
        return "faction_unknown_error";
    }

    std::vector<FactionMutation> deriveFactionMutations(
        const PlayerFactionState& authoritative, const PlayerFactionState& desired)
    {
        if (validatePlayerFactionState(authoritative) != FactionError::None
            || validatePlayerFactionState(desired) != FactionError::None)
            throw std::invalid_argument("Faction states must be canonical before deriving mutations");

        std::map<std::string, PlayerFactionEntry> from;
        std::map<std::string, PlayerFactionEntry> to;
        std::set<std::string> ids;
        for (const PlayerFactionEntry& entry : authoritative.factions)
        {
            from.emplace(entry.factionId, entry);
            ids.insert(entry.factionId);
        }
        for (const PlayerFactionEntry& entry : desired.factions)
        {
            to.emplace(entry.factionId, entry);
            ids.insert(entry.factionId);
        }

        std::vector<FactionMutation> result;
        for (const std::string& id : ids)
        {
            PlayerFactionEntry working = entryOrDefault(from, id);
            const PlayerFactionEntry target = entryOrDefault(to, id);
            if (working.rank != target.rank)
            {
                if (target.rank < 0)
                {
                    result.push_back({ FactionMutationKind::LeaveFaction, id, 0 });
                    working.rank = -1;
                    working.expelled = false;
                }
                else
                {
                    if (working.rank < 0)
                    {
                        result.push_back({ FactionMutationKind::JoinFaction, id, 0 });
                        working.rank = 0;
                    }
                    if (working.rank != target.rank)
                    {
                        result.push_back({ FactionMutationKind::SetFactionRank, id, target.rank });
                        working.rank = target.rank;
                    }
                }
            }
            if (working.reputation != target.reputation)
            {
                result.push_back({ FactionMutationKind::SetFactionReputation, id, target.reputation });
                working.reputation = target.reputation;
            }
            if (working.expelled != target.expelled)
            {
                result.push_back({ target.expelled ? FactionMutationKind::ExpelFromFaction
                                                   : FactionMutationKind::ClearFactionExpulsion,
                    id, 0 });
            }
        }
        return result;
    }

    std::string encodeFactionMutationRequest(const FactionMutationRequest& request)
    {
        Writer writer;
        writer.fixed(RequestMagic);
        writer.u16(request.protocolVersion);
        writer.string(request.requestId);
        writer.u16(static_cast<std::uint16_t>(request.mutations.size()));
        for (const FactionMutation& mutation : request.mutations)
        {
            writer.u8(static_cast<std::uint8_t>(mutation.kind));
            writer.string(mutation.factionId);
            writer.i64(mutation.value);
        }
        writer.u8(request.expectedRevision.has_value() ? 1 : 0);
        if (request.expectedRevision)
            writer.u64(*request.expectedRevision);
        writer.string(request.source);
        return writer.take();
    }

    FactionMutationRequest decodeFactionMutationRequest(std::string_view bytes)
    {
        Reader reader(bytes);
        reader.expect(RequestMagic);
        FactionMutationRequest request;
        request.protocolVersion = reader.u16();
        request.requestId = reader.string(MaximumSemanticRequestIdLength);
        const std::uint16_t count = reader.u16();
        if (count > MaximumFactionMutations)
            throw std::runtime_error("Too many faction mutations");
        request.mutations.resize(count);
        for (FactionMutation& mutation : request.mutations)
        {
            mutation.kind = static_cast<FactionMutationKind>(reader.u8());
            mutation.factionId = reader.string(MaximumFactionIdLength);
            mutation.value = reader.i64();
        }
        const std::uint8_t hasRevision = reader.u8();
        if (hasRevision > 1)
            throw std::runtime_error("Invalid faction revision flag");
        if (hasRevision)
            request.expectedRevision = reader.u64();
        request.source = reader.string(MaximumSemanticSourceLength);
        reader.finish();
        if (validateFactionMutationRequest(request) != FactionError::None)
            throw std::runtime_error("Invalid faction mutation request");
        return request;
    }

    std::string encodeFactionMutationResult(const FactionMutationResult& result)
    {
        Writer writer;
        writer.fixed(ResultMagic);
        writer.u16(result.protocolVersion);
        writer.string(result.requestId);
        writer.u8(result.accepted ? 1 : 0);
        writer.u16(static_cast<std::uint16_t>(result.error));
        writeState(writer, result.state);
        return writer.take();
    }

    FactionMutationResult decodeFactionMutationResult(std::string_view bytes)
    {
        Reader reader(bytes);
        reader.expect(ResultMagic);
        FactionMutationResult result;
        result.protocolVersion = reader.u16();
        if (result.protocolVersion != FactionServiceProtocolVersion)
            throw std::runtime_error("Unsupported faction result version");
        result.requestId = reader.string(MaximumSemanticRequestIdLength);
        const std::uint8_t accepted = reader.u8();
        if (accepted > 1)
            throw std::runtime_error("Invalid faction result acceptance flag");
        result.accepted = accepted != 0;
        result.error = static_cast<FactionError>(reader.u16());
        if (getFactionErrorCode(result.error) == "faction_unknown_error"
            || (result.accepted && result.error != FactionError::None)
            || (!result.accepted && result.error == FactionError::None))
            throw std::runtime_error("Invalid faction result error code");
        result.state = readState(reader);
        reader.finish();
        return result;
    }
}
