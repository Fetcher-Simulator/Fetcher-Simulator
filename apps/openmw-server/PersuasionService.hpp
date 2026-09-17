#pragma once
#include "PlayerDatabase.hpp"
#include <components/openmw-mp/PersuasionMechanics.hpp>
#include <components/openmw-mp/PersuasionProtocol.hpp>
#include <functional>

namespace mwmp
{
    class PersuasionService
    {
    public:
        struct Context
        {
            std::int64_t accountId = 0, characterId = 0;
            std::uint64_t inventoryRevision = 0;
            const BasePlayer* player = nullptr;
            bool actorAvailable = false;
            std::uint32_t generation = 0;
            NpcRelationship relationship;
            int currentBase = 0, derivedOffset = 0;
            PersuasionInput mechanics;
            std::optional<ContainerRecord> container;
            std::function<float(std::string_view)> gmst;
            std::function<int()> roll;
        };
        struct Outcome
        {
            PersuasionResult result;
            std::vector<std::uint8_t> encoded;
            std::vector<Item> inventory;
            std::optional<ContainerRecord> container;
            bool committed = false, replayed = false;
        };
        explicit PersuasionService(PlayerDatabase& db) : mDb(db) {}
        Outcome execute(const PersuasionRequest&, std::string_view hash, const Context&);
    private:
        PlayerDatabase& mDb;
    };
}
