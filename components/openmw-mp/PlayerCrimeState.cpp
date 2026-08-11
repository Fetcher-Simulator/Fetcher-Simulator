#include "PlayerCrimeState.hpp"

#include <bit>
#include <limits>
#include <stdexcept>

namespace mwmp
{
    namespace
    {
        constexpr std::string_view RequestMagic = "OMCQ";
        constexpr std::string_view ResultMagic = "OMCR";

        bool isValidUtf8(std::string_view value)
        {
            std::size_t i = 0;
            while (i < value.size())
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
                for (std::size_t j = 1; j < length; ++j)
                {
                    const auto continuation = static_cast<unsigned char>(value[i + j]);
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
                    throw std::length_error("Semantic service string is too long");
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
                const std::uint16_t a = u8();
                return static_cast<std::uint16_t>(a | (static_cast<std::uint16_t>(u8()) << 8));
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
                    throw std::runtime_error("Semantic service string exceeds limit");
                need(size);
                std::string result(mBytes.substr(mPosition, size));
                mPosition += size;
                return result;
            }
            void expect(std::string_view expected)
            {
                need(expected.size());
                if (mBytes.substr(mPosition, expected.size()) != expected)
                    throw std::runtime_error("Invalid semantic service payload magic");
                mPosition += expected.size();
            }
            void finish() const
            {
                if (mPosition != mBytes.size())
                    throw std::runtime_error("Trailing bytes in semantic service payload");
            }

        private:
            void need(std::size_t count) const
            {
                if (count > mBytes.size() - mPosition)
                    throw std::runtime_error("Truncated semantic service payload");
            }
            std::string_view mBytes;
            std::size_t mPosition = 0;
        };

        void writeState(Writer& writer, const PlayerCrimeState& state)
        {
            writer.u16(state.schemaVersion);
            writer.i32(state.bounty);
            writer.i32(state.currentCrimeId);
            writer.i32(state.paidCrimeId);
            writer.u64(state.revision);
        }

        PlayerCrimeState readState(Reader& reader)
        {
            PlayerCrimeState state;
            state.schemaVersion = reader.u16();
            state.bounty = reader.i32();
            state.currentCrimeId = reader.i32();
            state.paidCrimeId = reader.i32();
            state.revision = reader.u64();
            if (validatePlayerCrimeState(state) != CrimeError::None)
                throw std::runtime_error("Invalid player crime state");
            return state;
        }
    }

    CrimeError validatePlayerCrimeState(const PlayerCrimeState& state)
    {
        if (state.schemaVersion != PlayerCrimeStateSchemaVersion)
            return CrimeError::UnsupportedVersion;
        if (state.bounty < 0)
            return CrimeError::InvalidBounty;
        if (state.currentCrimeId < -1 || state.paidCrimeId < -1 || state.paidCrimeId > state.currentCrimeId)
            return CrimeError::InvalidCrimeId;
        if (state.revision > MaximumPersistedRevision)
            return CrimeError::RevisionOverflow;
        return CrimeError::None;
    }

    CrimeError validateCrimeMutationRequest(const CrimeMutationRequest& request)
    {
        if (request.protocolVersion != CrimeServiceProtocolVersion)
            return CrimeError::UnsupportedVersion;
        if (request.requestId.empty() || request.requestId.size() > MaximumSemanticRequestIdLength
            || request.requestId.find('\0') != std::string::npos || !isValidUtf8(request.requestId)
            || request.source.empty() || request.source.size() > MaximumSemanticSourceLength
            || request.source.find('\0') != std::string::npos || !isValidUtf8(request.source))
            return CrimeError::InvalidRequest;
        if (request.kind != CrimeMutationKind::SetBounty && request.kind != CrimeMutationKind::ModifyBounty)
            return CrimeError::InvalidRequest;
        if (request.expectedRevision && *request.expectedRevision > MaximumPersistedRevision)
            return CrimeError::StaleRevision;
        return CrimeError::None;
    }

    std::string_view getCrimeErrorCode(CrimeError error)
    {
        switch (error)
        {
            case CrimeError::None: return "none";
            case CrimeError::InvalidRequest: return "crime_invalid_request";
            case CrimeError::UnsupportedVersion: return "crime_unsupported_version";
            case CrimeError::StaleRevision: return "crime_stale_revision";
            case CrimeError::InvalidBounty: return "crime_invalid_bounty";
            case CrimeError::InvalidCrimeId: return "crime_invalid_crime_id";
            case CrimeError::Unauthorized: return "crime_unauthorized";
            case CrimeError::DuplicateConflict: return "crime_duplicate_conflict";
            case CrimeError::StateUnavailable: return "crime_state_unavailable";
            case CrimeError::PersistenceFailure: return "crime_persistence_failure";
            case CrimeError::RevisionOverflow: return "crime_revision_overflow";
            case CrimeError::CorruptStoredResult: return "crime_corrupt_stored_result";
        }
        return "crime_unknown_error";
    }

    std::string encodeCrimeMutationRequest(const CrimeMutationRequest& request)
    {
        Writer writer;
        writer.fixed(RequestMagic);
        writer.u16(request.protocolVersion);
        writer.string(request.requestId);
        writer.u8(static_cast<std::uint8_t>(request.kind));
        writer.i64(request.value);
        writer.u8(request.expectedRevision.has_value() ? 1 : 0);
        if (request.expectedRevision)
            writer.u64(*request.expectedRevision);
        writer.string(request.source);
        return writer.take();
    }

    std::string encodeCrimeMutationResult(const CrimeMutationResult& result)
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

    CrimeMutationResult decodeCrimeMutationResult(std::string_view bytes)
    {
        Reader reader(bytes);
        reader.expect(ResultMagic);
        CrimeMutationResult result;
        result.protocolVersion = reader.u16();
        if (result.protocolVersion != CrimeServiceProtocolVersion)
            throw std::runtime_error("Unsupported crime result version");
        result.requestId = reader.string(MaximumSemanticRequestIdLength);
        const std::uint8_t accepted = reader.u8();
        if (accepted > 1)
            throw std::runtime_error("Invalid crime result acceptance flag");
        result.accepted = accepted != 0;
        result.error = static_cast<CrimeError>(reader.u16());
        if (getCrimeErrorCode(result.error) == "crime_unknown_error")
            throw std::runtime_error("Unknown crime result error code");
        result.state = readState(reader);
        reader.finish();
        return result;
    }
}
