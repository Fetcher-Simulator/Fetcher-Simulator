#ifndef OPENMW_SERVER_ENCHANTINGSERVICE_HPP
#define OPENMW_SERVER_ENCHANTINGSERVICE_HPP

#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <components/openmw-mp/Records/EnchantingProtocol.hpp>

#include "DynamicRecordService.hpp"
#include "PlayerDatabase.hpp"

namespace MWWorld
{
    class ESMStore;
}

namespace mwmp
{
    /// Server-authoritative native enchanting. Owns the semantic request
    /// validation, the shared OpenMW mechanics execution, and the atomic
    /// gameplay transaction. The client contributes player choices only;
    /// every calculated value (effect costs, charge, capacity, success, gold,
    /// skill progression, record identity) is derived server-side and
    /// committed all-or-nothing together with the Enchantment + owning-item
    /// record pair.
    class EnchantingService
    {
    public:
        /// Everything DynamicRecordService::Context provides (canonical record
        /// creation provenance, scopes, fixed IDs, validation versions) plus
        /// the enchanting-specific authoritative state.
        struct Context : public DynamicRecordService::Context
        {
            const BasePlayer* player = nullptr;           // authoritative character statistics
            const std::vector<Item>* inventory = nullptr; // authoritative inventory mirror
            const MWWorld::ESMStore* store = nullptr;     // authoritative resolved content

            // Canonical dynamic-record plumbing (the same callbacks
            // DynamicRecordService::execute receives as parameters).
            DynamicRecordService::FindEquivalent findEquivalent;
            DynamicRecordService::AllocateId allocateId;
            std::function<uint64_t()> nextCommitSequence;

            /// Enumerates every runtime dynamic Enchantment record as
            /// (recordId, OMDR definition). Used for the native
            /// getRecord-equivalent reuse search (type/cost/charge/flags/
            /// effects, dynamic records only, exactly like single-player
            /// enchanting).
            std::function<std::vector<std::pair<std::string, std::string>>()> listDynamicEnchantments;

            /// Resolves a paid-service enchanter by its server-issued actor
            /// net id. Returns the authoritative actor state (content refId,
            /// synced dynamic stats, and whether the requester has the
            /// actor's cell loaded).
            struct EnchanterInfo
            {
                std::string refId;
                std::optional<DynamicStats> dynamicStats;
                bool cellLoaded = false;
            };
            std::function<std::optional<EnchanterInfo>(std::uint64_t actorNetId)> resolveEnchanter;

            /// Assigns stable instance IDs to newly granted stacks of the
            /// proposed inventory before the atomic commit.
            std::function<void(std::vector<Item>&)> reconcileInventory;

            /// Server setting mirroring the client "projectiles enchant
            /// multiplier"; 0 = one projectile at a time (native default).
            float projectilesEnchantMultiplier = 0.f;

            /// Optional fixed seed for the authoritative roll generator.
            /// Production callers omit it (a fresh time-based seed is used);
            /// deterministic tests set it to reproduce exact outcomes.
            std::optional<std::uint32_t> rngSeed;
        };

        struct Outcome
        {
            records::EnchantingResult result;
            std::vector<uint8_t> encodedResult;
            std::vector<DynamicRecordService::CommittedRecord> newRecords; // definitions to broadcast
            bool replayed = false;
            bool committed = false;
            std::vector<Item> resultingInventory;    // valid when committed
            uint64_t resultingInventoryRevision = 0; // valid when committed
            std::optional<BasePlayer> resultingStats; // set when stats changed
        };

        explicit EnchantingService(PlayerDatabase& database)
            : mDatabase(database)
        {
        }

        /// Processes one semantic enchanting request. Terminal outcomes
        /// (accepted or rejected) are durably journaled; retries replay the
        /// exact original result.
        Outcome execute(
            const records::EnchantingRequest& request, std::string_view requestHash, const Context& context);

        static records::EnchantingResult makeError(
            std::string requestId, records::EnchantingError error, uint64_t inventoryRevision);

    private:
        PlayerDatabase& mDatabase;
    };
}

#endif
