#ifndef OPENMW_SERVER_ALCHEMYSERVICE_HPP
#define OPENMW_SERVER_ALCHEMYSERVICE_HPP

#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <components/openmw-mp/Records/AlchemyProtocol.hpp>

#include "DynamicRecordService.hpp"
#include "PlayerDatabase.hpp"

namespace MWWorld
{
    class ESMStore;
}

namespace mwmp
{
    /// Server-authoritative native alchemy. Owns the semantic request
    /// validation, the shared OpenMW mechanics execution, and the atomic
    /// gameplay transaction. The client contributes player choices only;
    /// every calculated value (effects, magnitudes, durations, weight, value,
    /// success, consumption, skill progression, record identity) is derived
    /// server-side and committed all-or-nothing.
    class AlchemyService
    {
    public:
        /// Everything DynamicRecordService::Context provides (canonical record
        /// creation provenance, scopes, fixed IDs, validation versions) plus
        /// the alchemy-specific authoritative state.
        struct Context : public DynamicRecordService::Context
        {
            const BasePlayer* player = nullptr;            // authoritative character statistics
            const std::vector<Item>* inventory = nullptr;  // authoritative inventory mirror
            const MWWorld::ESMStore* store = nullptr;      // authoritative resolved content

            // Canonical dynamic-record plumbing (the same callbacks
            // DynamicRecordService::execute receives as parameters).
            DynamicRecordService::FindEquivalent findEquivalent;
            DynamicRecordService::AllocateId allocateId;
            std::function<uint64_t()> nextCommitSequence;

            /// Enumerates every runtime dynamic Potion record as (recordId,
            /// OMDR definition). Used for the native getRecord-equivalent
            /// reuse search (name/weight/value/flags/effects, ignoring
            /// model/icon, exactly like single-player alchemy).
            std::function<std::vector<std::pair<std::string, std::string>>()> listDynamicPotions;

            /// Assigns stable instance IDs to newly granted stacks of the
            /// proposed inventory before the atomic commit.
            std::function<void(std::vector<Item>&)> reconcileInventory;

            /// Optional fixed seed for the authoritative roll generator.
            /// Production callers omit it (a fresh time-based seed is used);
            /// deterministic tests set it to reproduce exact outcomes.
            std::optional<std::uint32_t> rngSeed;
        };

        struct Outcome
        {
            records::AlchemyResult result;
            std::vector<uint8_t> encodedResult;
            std::vector<DynamicRecordService::CommittedRecord> newRecords; // definitions to broadcast
            bool replayed = false;
            bool committed = false;
            std::vector<Item> resultingInventory;    // valid when committed
            uint64_t resultingInventoryRevision = 0; // valid when committed
            std::optional<BasePlayer> resultingStats; // set when stats changed
        };

        explicit AlchemyService(PlayerDatabase& database)
            : mDatabase(database)
        {
        }

        /// Processes one semantic alchemy request. Terminal outcomes
        /// (accepted or rejected) are durably journaled; retries replay the
        /// exact original result.
        Outcome execute(const records::AlchemyRequest& request, std::string_view requestHash, const Context& context);

        static records::AlchemyResult makeError(
            std::string requestId, records::AlchemyError error, uint64_t inventoryRevision);

    private:
        PlayerDatabase& mDatabase;
    };
}

#endif
