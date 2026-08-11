#ifndef OPENMW_MP_PLAYERCRIMESTATE_HPP
#define OPENMW_MP_PLAYERCRIMESTATE_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace mwmp
{
    inline constexpr std::uint16_t PlayerCrimeStateSchemaVersion = 1;
    inline constexpr std::uint16_t CrimeServiceProtocolVersion = 1;
    inline constexpr std::size_t MaximumSemanticRequestIdLength = 128;
    inline constexpr std::size_t MaximumSemanticSourceLength = 128;
    inline constexpr std::uint64_t MaximumPersistedRevision = 0x7fffffffffffffffULL;

    struct PlayerCrimeState
    {
        std::uint16_t schemaVersion = PlayerCrimeStateSchemaVersion;
        std::int32_t bounty = 0;
        std::int32_t currentCrimeId = -1;
        std::int32_t paidCrimeId = -1;
        std::uint64_t revision = 0;

        bool operator==(const PlayerCrimeState&) const = default;
    };

    enum class CrimeMutationKind : std::uint8_t
    {
        SetBounty = 1,
        ModifyBounty = 2,
    };

    enum class CrimeError : std::uint16_t
    {
        None = 0,
        InvalidRequest = 1,
        UnsupportedVersion = 2,
        StaleRevision = 3,
        InvalidBounty = 4,
        InvalidCrimeId = 5,
        Unauthorized = 6,
        DuplicateConflict = 7,
        StateUnavailable = 8,
        PersistenceFailure = 9,
        RevisionOverflow = 10,
        CorruptStoredResult = 11,
    };

    struct CrimeMutationRequest
    {
        std::uint16_t protocolVersion = CrimeServiceProtocolVersion;
        std::string requestId;
        CrimeMutationKind kind = CrimeMutationKind::SetBounty;
        std::int64_t value = 0;
        std::optional<std::uint64_t> expectedRevision;
        std::string source;

        bool operator==(const CrimeMutationRequest&) const = default;
    };

    struct CrimeMutationResult
    {
        std::uint16_t protocolVersion = CrimeServiceProtocolVersion;
        std::string requestId;
        bool accepted = false;
        CrimeError error = CrimeError::None;
        PlayerCrimeState state;

        bool operator==(const CrimeMutationResult&) const = default;
    };

    CrimeError validatePlayerCrimeState(const PlayerCrimeState& state);
    CrimeError validateCrimeMutationRequest(const CrimeMutationRequest& request);
    std::string_view getCrimeErrorCode(CrimeError error);

    /// Canonical, little-endian encodings used for request hashing and durable
    /// terminal-result replay. These are independent of C++ object layout.
    std::string encodeCrimeMutationRequest(const CrimeMutationRequest& request);
    std::string encodeCrimeMutationResult(const CrimeMutationResult& result);
    CrimeMutationResult decodeCrimeMutationResult(std::string_view bytes);
}

#endif
