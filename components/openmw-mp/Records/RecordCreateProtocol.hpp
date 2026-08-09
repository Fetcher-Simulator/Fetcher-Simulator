#ifndef OPENMW_MP_RECORD_CREATE_PROTOCOL_HPP
#define OPENMW_MP_RECORD_CREATE_PROTOCOL_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "DynamicRecordTypes.hpp"

namespace mwmp::records
{
    inline constexpr std::uint16_t CurrentCreateProtocolVersion = 2;

    enum class CreateOperation : std::uint8_t
    {
        CustomRecord = 0,
        Alchemy = 1,
        Enchanting = 2,
        ServerScript = 3,
    };

    enum class CreateError : std::uint16_t
    {
        None = 0,
        InvalidRequest = 1,
        UnsupportedProtocol = 2,
        UnsupportedSchema = 3,
        Unauthorized = 4,
        RecordTypeNotPermitted = 5,
        InvalidScriptIdentity = 6,
        InvalidDefinition = 7,
        InvalidDependency = 8,
        InvalidAsset = 9,
        ContentMismatch = 10,
        StaleInventoryRevision = 11,
        MissingSourceItem = 12,
        InsufficientIngredients = 13,
        InsufficientGold = 14,
        InvalidEffect = 15,
        EnchantCapacityExceeded = 16,
        InvalidSoul = 17,
        CraftFailed = 18,
        RateLimited = 19,
        QuotaExceeded = 20,
        DuplicateRequestConflict = 21,
        RequestPending = 22,
        ServerError = 23,
        InvalidAuthoringMode = 24,
        NewRecordStaticCollision = 25,
        OverrideMissingStatic = 26,
        FixedRecordConflict = 27,
        OverrideNotBootstrap = 28,
        DurableReferenceConflict = 29,
        ScriptCompileFailed = 30,
    };

    struct RecordCreateRequest
    {
        std::uint16_t protocolVersion = CurrentCreateProtocolVersion;
        std::string requestId;
        CreateOperation operation = CreateOperation::CustomRecord;
        std::uint64_t inventoryRevision = 0;
        std::string scriptPackageId;
        DynamicRecordBundle bundle;
        std::string evidence;
        bool operator==(const RecordCreateRequest&) const = default;
    };

    struct CreatedRecord
    {
        std::string temporaryKey;
        std::string recordId;
        bool reused = false;
        std::string definition;
        bool operator==(const CreatedRecord&) const = default;
    };

    struct RecordCreateResult
    {
        std::uint16_t protocolVersion = CurrentCreateProtocolVersion;
        std::string requestId;
        bool accepted = false;
        CreateError error = CreateError::None;
        std::uint64_t inventoryRevision = 0;
        std::uint64_t commitSequence = 0;
        std::vector<CreatedRecord> records;
        bool operator==(const RecordCreateResult&) const = default;
    };

    std::string_view getCreateErrorCode(CreateError error);
}

#endif
