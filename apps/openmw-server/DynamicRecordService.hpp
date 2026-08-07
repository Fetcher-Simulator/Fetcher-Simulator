#ifndef OPENMW_SERVER_DYNAMICRECORDSERVICE_HPP
#define OPENMW_SERVER_DYNAMICRECORDSERVICE_HPP

#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include <components/openmw-mp/Records/RecordCreateProtocol.hpp>

#include "PlayerDatabase.hpp"

namespace mwmp
{
    /// The single authoritative boundary for runtime record bundles. It is
    /// deliberately independent of Lua and the graphical client runtime.
    class DynamicRecordService
    {
    public:
        struct CatalogRecord
        {
            std::string recordType;
            std::string recordId;
            std::string fingerprint;
            std::string definition;
        };

        struct Context
        {
            int64_t accountId = 0;
            int64_t characterId = 0;
            uint64_t inventoryRevision = 0;
            std::string creationSource;
            std::uint16_t validationVersion = 1;
            bool trustedServerRequest = false;
            std::string recordScope = "generated";
            bool persistent = true;
            std::string serverRequestSource;
            std::unordered_map<std::string, std::string> fixedRecordIds;
            bool allowCustomDefinitions = false;
            std::unordered_set<records::RecordType> permittedTypes;
            records::CreateError admissionError = records::CreateError::None;
            std::size_t maximumNewRecords = std::numeric_limits<std::size_t>::max();
            std::function<bool(std::string_view)> isContentIdAllowed;
            std::function<bool(std::string_view)> isAssetAllowed;
            std::function<bool(std::string_view)> isModelAllowed;
            std::function<bool(std::string_view)> isIconAllowed;
        };

        struct CommittedRecord
        {
            std::string recordType;
            std::string recordId;
            std::string definition;
            std::vector<std::string> dependencyRecordIds;
        };

        struct Outcome
        {
            records::RecordCreateResult result;
            std::vector<uint8_t> encodedResult;
            std::vector<CommittedRecord> newRecords;
            bool replayed = false;
        };

        using FindEquivalent = std::function<std::optional<CatalogRecord>(
            records::RecordType type, std::string_view fingerprint)>;
        using AllocateId = std::function<std::string(records::RecordType type)>;
        using NextCommitSequence = std::function<uint64_t()>;

        explicit DynamicRecordService(PlayerDatabase& database)
            : mDatabase(database)
        {
        }

        Outcome execute(const records::RecordCreateRequest& request, std::string_view requestHash,
            const Context& context, const FindEquivalent& findEquivalent, const AllocateId& allocateId,
            const NextCommitSequence& nextCommitSequence);

        static records::RecordCreateResult makeError(
            std::string requestId, records::CreateError error, uint64_t inventoryRevision);

    private:
        PlayerDatabase& mDatabase;
    };
}

#endif
