#include "GuardArrest.hpp"

#include <bit>
#include <stdexcept>

namespace
{
    constexpr std::string_view RequestMagic = "OMGA";
    constexpr std::string_view ResultMagic = "OMGR";

    bool validString(std::string_view value, std::size_t maximum)
    {
        if (value.empty() || value.size() > maximum)
            return false;
        for (unsigned char c : value)
        {
            if (c == 0 || c < 0x20 || c == 0x7f)
                return false;
        }
        return true;
    }

    bool validAction(mwmp::GuardArrestAction action)
    {
        return action == mwmp::GuardArrestAction::PayFine
            || action == mwmp::GuardArrestAction::Surrender
            || action == mwmp::GuardArrestAction::Resist;
    }

    bool validError(mwmp::GuardArrestError error)
    {
        return static_cast<std::uint16_t>(error)
            <= static_cast<std::uint16_t>(mwmp::GuardArrestError::PersistenceFailure);
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
            const std::uint16_t low = u8();
            return static_cast<std::uint16_t>(low | (static_cast<std::uint16_t>(u8()) << 8));
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
                throw std::runtime_error("Guard arrest string exceeds limit");
            need(size);
            std::string value(mBytes.substr(mPosition, size));
            mPosition += size;
            return value;
        }
        void expect(std::string_view value)
        {
            need(value.size());
            if (mBytes.substr(mPosition, value.size()) != value)
                throw std::runtime_error("Invalid guard arrest payload magic");
            mPosition += value.size();
        }
        void finish() const
        {
            if (mPosition != mBytes.size())
                throw std::runtime_error("Trailing guard arrest payload bytes");
        }

    private:
        void need(std::size_t size) const
        {
            if (size > mBytes.size() - mPosition)
                throw std::runtime_error("Truncated guard arrest payload");
        }

        std::string_view mBytes;
        std::size_t mPosition = 0;
    };
}

bool mwmp::validateGuardArrestRequest(const GuardArrestRequest& request)
{
    return request.protocolVersion == GuardArrestProtocolVersion
        && validString(request.requestId, MaximumGuardArrestRequestIdLength)
        && validAction(request.action)
        && validString(request.cellId, MaximumGuardArrestCellIdLength)
        && isValidActorInstanceId(request.actorNetId)
        && request.migrationGeneration != 0
        && request.expectedCrimeRevision <= MaximumPersistedRevision;
}

bool mwmp::validateGuardArrestReach(const GuardArrestReach& reach)
{
    return reach.protocolVersion == GuardArrestProtocolVersion
        && validString(reach.cellId, MaximumGuardArrestCellIdLength)
        && isValidActorInstanceId(reach.actorNetId)
        && reach.migrationGeneration != 0
        && reach.offenderGuid != 0;
}

bool mwmp::validateGuardArrestResult(const GuardArrestResult& result)
{
    if (result.protocolVersion != GuardArrestProtocolVersion
        || !validString(result.requestId, MaximumGuardArrestRequestIdLength)
        || !validAction(result.action) || !validError(result.error)
        || validatePlayerCrimeState(result.crimeState) != CrimeError::None
        || result.goldPaid < 0)
        return false;

    if (result.accepted != (result.error == GuardArrestError::None))
        return false;
    if (!result.accepted)
        return result.goldPaid == 0 && result.sentenceDays == 0;

    switch (result.action)
    {
        case GuardArrestAction::PayFine:
            return result.goldPaid > 0 && result.sentenceDays == 0 && result.crimeState.bounty == 0;
        case GuardArrestAction::Surrender:
            return result.goldPaid == 0 && result.sentenceDays > 0 && result.crimeState.bounty == 0;
        case GuardArrestAction::Resist:
            return result.goldPaid == 0 && result.sentenceDays == 0 && result.crimeState.bounty > 0;
    }
    return false;
}

std::string mwmp::canonicalGuardArrestRequest(const GuardArrestRequest& request)
{
    Writer writer;
    writer.fixed(RequestMagic);
    writer.u16(request.protocolVersion);
    writer.string(request.requestId);
    writer.u8(static_cast<std::uint8_t>(request.action));
    writer.string(request.cellId);
    writer.u64(request.actorNetId);
    writer.u32(request.migrationGeneration);
    writer.u64(request.expectedCrimeRevision);
    writer.u64(request.expectedInventoryRevision);
    return writer.take();
}

std::string mwmp::encodeGuardArrestResult(const GuardArrestResult& result)
{
    if (!validateGuardArrestResult(result))
        throw std::invalid_argument("Invalid guard arrest result");

    Writer writer;
    writer.fixed(ResultMagic);
    writer.u16(result.protocolVersion);
    writer.string(result.requestId);
    writer.u8(static_cast<std::uint8_t>(result.action));
    writer.u8(result.accepted ? 1 : 0);
    writer.u16(static_cast<std::uint16_t>(result.error));
    writer.u16(result.crimeState.schemaVersion);
    writer.i32(result.crimeState.bounty);
    writer.i32(result.crimeState.currentCrimeId);
    writer.i32(result.crimeState.paidCrimeId);
    writer.u64(result.crimeState.revision);
    writer.u64(result.inventoryRevision);
    writer.i64(result.goldPaid);
    writer.u32(result.sentenceDays);
    return writer.take();
}

mwmp::GuardArrestResult mwmp::decodeGuardArrestResult(std::string_view bytes)
{
    Reader reader(bytes);
    reader.expect(ResultMagic);
    GuardArrestResult result;
    result.protocolVersion = reader.u16();
    result.requestId = reader.string(MaximumGuardArrestRequestIdLength);
    result.action = static_cast<GuardArrestAction>(reader.u8());
    const std::uint8_t accepted = reader.u8();
    if (accepted > 1)
        throw std::runtime_error("Invalid guard arrest acceptance flag");
    result.accepted = accepted != 0;
    result.error = static_cast<GuardArrestError>(reader.u16());
    result.crimeState.schemaVersion = reader.u16();
    result.crimeState.bounty = reader.i32();
    result.crimeState.currentCrimeId = reader.i32();
    result.crimeState.paidCrimeId = reader.i32();
    result.crimeState.revision = reader.u64();
    result.inventoryRevision = reader.u64();
    result.goldPaid = reader.i64();
    result.sentenceDays = reader.u32();
    reader.finish();
    if (!validateGuardArrestResult(result))
        throw std::runtime_error("Invalid guard arrest result payload");
    return result;
}

std::string_view mwmp::getGuardArrestErrorCode(GuardArrestError error)
{
    switch (error)
    {
        case GuardArrestError::None: return "none";
        case GuardArrestError::InvalidRequest: return "guard_arrest_invalid_request";
        case GuardArrestError::Unauthorized: return "guard_arrest_unauthorized";
        case GuardArrestError::StaleCrimeRevision: return "guard_arrest_stale_crime_revision";
        case GuardArrestError::WrongCell: return "guard_arrest_wrong_cell";
        case GuardArrestError::UnknownGuard: return "guard_arrest_unknown_guard";
        case GuardArrestError::InvalidGuard: return "guard_arrest_invalid_guard";
        case GuardArrestError::SnapshotUnavailable: return "guard_arrest_snapshot_unavailable";
        case GuardArrestError::OutOfRange: return "guard_arrest_out_of_range";
        case GuardArrestError::PlayerDead: return "guard_arrest_player_dead";
        case GuardArrestError::NoBounty: return "guard_arrest_no_bounty";
        case GuardArrestError::InsufficientGold: return "guard_arrest_insufficient_gold";
        case GuardArrestError::StaleInventoryRevision: return "guard_arrest_stale_inventory_revision";
        case GuardArrestError::DuplicateConflict: return "guard_arrest_duplicate_conflict";
        case GuardArrestError::PersistenceFailure: return "guard_arrest_persistence_failure";
    }
    return "guard_arrest_unknown_error";
}
