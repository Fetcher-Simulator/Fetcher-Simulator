#ifndef OPENMW_SERVER_CRIMESERVICE_HPP
#define OPENMW_SERVER_CRIMESERVICE_HPP

#include <cstdint>
#include <string>

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

        /// Trusted server-domain transition produced after semantic crime
        /// evaluation. This is deliberately not a client or wire request.
        struct AuthoritativeTransition
        {
            std::string requestId;
            std::string requestHash;
            std::string source;
            std::int64_t bountyDelta = 0;
            bool advanceCurrentCrimeId = false;
            std::uint64_t expectedRevision = 0;
            std::string terminalResultPayload;
        };

        struct TransitionOutcome
        {
            CrimeCommitStatus status = CrimeCommitStatus::Committed;
            PlayerCrimeState state;
            std::string storedResultPayload;
        };

        explicit CrimeService(PlayerDatabase& database)
            : mDatabase(database)
        {
        }

        Outcome execute(const CrimeMutationRequest& request, const Context& context);
        TransitionOutcome commitAuthoritativeTransition(
            const AuthoritativeTransition& transition, const Context& context);

    private:
        PlayerDatabase& mDatabase;
    };
}

#endif
