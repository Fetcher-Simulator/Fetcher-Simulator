#ifndef OPENMW_MP_GUARDARREST_HPP
#define OPENMW_MP_GUARDARREST_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "Base/ActorSyncProtocol.hpp"
#include "PlayerCrimeState.hpp"

namespace mwmp
{
    inline constexpr std::uint16_t GuardArrestProtocolVersion = 2;
    inline constexpr std::size_t MaximumGuardArrestRequestIdLength = 128;
    inline constexpr std::size_t MaximumGuardArrestCellIdLength = 255;

    enum class GuardArrestAction : std::uint8_t
    {
        PayFine = 1,
        Surrender = 2,
        Resist = 3,
    };

    enum class GuardArrestError : std::uint16_t
    {
        None = 0,
        InvalidRequest,
        Unauthorized,
        StaleCrimeRevision,
        WrongCell,
        UnknownGuard,
        InvalidGuard,
        SnapshotUnavailable,
        OutOfRange,
        PlayerDead,
        NoBounty,
        InsufficientGold,
        StaleInventoryRevision,
        DuplicateConflict,
        PersistenceFailure,
    };

    struct GuardArrestRequest
    {
        std::uint16_t protocolVersion = GuardArrestProtocolVersion;
        std::string requestId;
        GuardArrestAction action = GuardArrestAction::Surrender;
        std::string cellId;
        ActorInstanceId actorNetId = 0;
        std::uint32_t migrationGeneration = 0;
        std::uint64_t expectedCrimeRevision = 0;
        std::uint64_t expectedInventoryRevision = 0;

        bool operator==(const GuardArrestRequest&) const = default;
    };

    struct GuardArrestReach
    {
        std::uint16_t protocolVersion = GuardArrestProtocolVersion;
        std::string cellId;
        ActorInstanceId actorNetId = 0;
        std::uint32_t migrationGeneration = 0;
        std::uint32_t offenderGuid = 0;

        bool operator==(const GuardArrestReach&) const = default;
    };

    struct GuardArrestResult
    {
        std::uint16_t protocolVersion = GuardArrestProtocolVersion;
        std::string requestId;
        GuardArrestAction action = GuardArrestAction::Surrender;
        bool accepted = false;
        GuardArrestError error = GuardArrestError::None;
        PlayerCrimeState crimeState;
        std::uint64_t inventoryRevision = 0;
        std::int64_t goldPaid = 0;
        std::uint32_t sentenceDays = 0;

        bool operator==(const GuardArrestResult&) const = default;
    };

    bool validateGuardArrestRequest(const GuardArrestRequest& request);
    bool validateGuardArrestReach(const GuardArrestReach& reach);
    bool validateGuardArrestResult(const GuardArrestResult& result);
    std::string canonicalGuardArrestRequest(const GuardArrestRequest& request);
    std::string encodeGuardArrestResult(const GuardArrestResult& result);
    GuardArrestResult decodeGuardArrestResult(std::string_view bytes);
    std::string_view getGuardArrestErrorCode(GuardArrestError error);
}

#endif
