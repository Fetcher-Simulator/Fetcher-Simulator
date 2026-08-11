#ifndef OPENMW_SERVER_CRIMESERVICE_HPP
#define OPENMW_SERVER_CRIMESERVICE_HPP

#include <cstdint>

#include <components/openmw-mp/PlayerCrimeState.hpp>

#include "PlayerDatabase.hpp"

namespace mwmp
{
    /// First typed semantic gameplay service. The server derives ownership
    /// from Context, validates one domain request, and commits authoritative
    /// crime state plus the terminal idempotency result atomically.
    class CrimeService
    {
    public:
        struct Context
        {
            std::int64_t accountId = 0;
            std::int64_t characterId = 0;
            CrimeCommitFailurePoint failurePoint = CrimeCommitFailurePoint::None;
        };

        struct Outcome
        {
            CrimeMutationResult result;
            bool replayed = false;
            bool committed = false;
        };

        explicit CrimeService(PlayerDatabase& database)
            : mDatabase(database)
        {
        }

        Outcome execute(const CrimeMutationRequest& request, const Context& context);

    private:
        PlayerDatabase& mDatabase;
    };
}

#endif
