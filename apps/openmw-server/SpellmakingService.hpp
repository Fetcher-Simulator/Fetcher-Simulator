#pragma once
#include "DynamicRecordService.hpp"
#include <components/esm3/loadspel.hpp>
#include <components/openmw-mp/Records/SpellmakingProtocol.hpp>

namespace MWWorld
{
    class ESMStore;
}
namespace mwmp
{
    class SpellmakingService
    {
    public:
        struct ServiceActor
        {
            std::string refId;
            DynamicStats dynamicStats;
            bool available = false; // living, in the player's cell and within interaction range
            MerchantGoldMutation gold;
        };
        struct Context : DynamicRecordService::Context
        {
            const BasePlayer* player = nullptr;
            const MWWorld::ESMStore* store = nullptr;
            std::uint64_t spellbookRevision = 0;
            std::function<std::optional<ServiceActor>(std::uint64_t)> resolveActor;
            std::function<std::optional<ESM::Spell>(const std::string&)> lookupSpell;
            DynamicRecordService::FindEquivalent findEquivalent;
            DynamicRecordService::AllocateId allocateId;
            std::function<std::uint64_t()> nextCommitSequence;
        };
        struct Outcome
        {
            records::SpellmakingResult result;
            std::vector<std::uint8_t> encodedResult;
            std::vector<DynamicRecordService::CommittedRecord> newRecords;
            std::vector<Item> inventory;
            std::vector<std::string> spellbook;
            bool committed = false;
            bool replayed = false;
        };
        explicit SpellmakingService(PlayerDatabase& db)
            : mDatabase(db)
        {
        }
        Outcome execute(const records::SpellmakingRequest&, std::string_view hash, const Context&);

    private:
        PlayerDatabase& mDatabase;
    };
}
