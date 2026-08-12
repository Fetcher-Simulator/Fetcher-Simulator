#ifndef OPENMW_MP_PLAYERFACTIONSTATE_HPP
#define OPENMW_MP_PLAYERFACTIONSTATE_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mwmp
{
    inline constexpr std::uint16_t PlayerFactionStateSchemaVersion = 1;
    inline constexpr std::uint16_t FactionServiceProtocolVersion = 1;
    inline constexpr std::size_t MaximumPlayerFactions = 256;
    inline constexpr std::size_t MaximumFactionMutations = MaximumPlayerFactions * 4;
    inline constexpr std::size_t MaximumFactionIdLength = 256;
    inline constexpr std::int32_t MaximumProtocolFactionRank = 127;

    struct PlayerFactionEntry
    {
        std::string factionId;
        std::int32_t rank = -1;
        std::int32_t reputation = 0;
        bool expelled = false;

        bool operator==(const PlayerFactionEntry&) const = default;
    };

    struct PlayerFactionState
    {
        std::uint16_t schemaVersion = PlayerFactionStateSchemaVersion;
        std::uint64_t revision = 0;
        std::vector<PlayerFactionEntry> factions;

        bool operator==(const PlayerFactionState&) const = default;
    };

    enum class FactionMutationKind : std::uint8_t
    {
        JoinFaction = 1,
        LeaveFaction = 2,
        SetFactionRank = 3,
        ModifyFactionRank = 4,
        SetFactionReputation = 5,
        ModifyFactionReputation = 6,
        ExpelFromFaction = 7,
        ClearFactionExpulsion = 8,
    };

    struct FactionMutation
    {
        FactionMutationKind kind = FactionMutationKind::JoinFaction;
        std::string factionId;
        std::int64_t value = 0;

        bool operator==(const FactionMutation&) const = default;
    };

    struct FactionMutationRequest
    {
        std::uint16_t protocolVersion = FactionServiceProtocolVersion;
        std::string requestId;
        std::vector<FactionMutation> mutations;
        std::optional<std::uint64_t> expectedRevision;
        std::string source;

        bool operator==(const FactionMutationRequest&) const = default;
    };

    enum class FactionError : std::uint16_t
    {
        None = 0,
        InvalidRequest = 1,
        UnsupportedVersion = 2,
        StaleRevision = 3,
        InvalidFaction = 4,
        InvalidRank = 5,
        InvalidReputation = 6,
        InvalidTransition = 7,
        Unauthorized = 8,
        DuplicateConflict = 9,
        PersistenceFailure = 10,
        RevisionOverflow = 11,
        CorruptStoredResult = 12,
    };

    struct FactionMutationResult
    {
        std::uint16_t protocolVersion = FactionServiceProtocolVersion;
        std::string requestId;
        bool accepted = false;
        FactionError error = FactionError::None;
        PlayerFactionState state;

        bool operator==(const FactionMutationResult&) const = default;
    };

    std::string canonicalizeFactionId(std::string_view id);
    PlayerFactionState canonicalizePlayerFactionState(PlayerFactionState state);
    FactionMutationRequest canonicalizeFactionMutationRequest(FactionMutationRequest request);
    FactionError validatePlayerFactionState(const PlayerFactionState& state);
    FactionError validateFactionMutationRequest(const FactionMutationRequest& request);
    std::string_view getFactionErrorCode(FactionError error);

    /// Produce typed transitions from one canonical semantic state to another.
    /// Operation order accounts for LeaveFaction clearing expulsion in NpcStats.
    std::vector<FactionMutation> deriveFactionMutations(
        const PlayerFactionState& authoritative, const PlayerFactionState& desired);

    std::string encodeFactionMutationRequest(const FactionMutationRequest& request);
    FactionMutationRequest decodeFactionMutationRequest(std::string_view bytes);
    std::string encodeFactionMutationResult(const FactionMutationResult& result);
    FactionMutationResult decodeFactionMutationResult(std::string_view bytes);
}

#endif
