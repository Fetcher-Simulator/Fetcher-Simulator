#ifndef OPENMW_SERVER_FACTIONSERVICE_HPP
#define OPENMW_SERVER_FACTIONSERVICE_HPP

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

#include <components/openmw-mp/PlayerFactionState.hpp>

#include "PlayerDatabase.hpp"

namespace mwmp
{
    /// Typed faction mutation service. The effective-content lookup is supplied
    /// by the server so this service remains testable without an engine world.
    class FactionService
    {
    public:
        struct FactionDefinition
        {
            std::vector<bool> validRanks;
        };

        struct Context
        {
            std::int64_t accountId = 0;
            std::int64_t characterId = 0;
            std::function<std::optional<FactionDefinition>(std::string_view)> findFaction;
            FactionCommitFailurePoint failurePoint = FactionCommitFailurePoint::None;
        };

        struct Outcome
        {
            FactionMutationResult result;
            bool replayed = false;
            bool committed = false;
        };

        explicit FactionService(PlayerDatabase& database)
            : mDatabase(database)
        {
        }

        Outcome execute(FactionMutationRequest request, const Context& context);

    private:
        PlayerDatabase& mDatabase;
    };
}

#endif
