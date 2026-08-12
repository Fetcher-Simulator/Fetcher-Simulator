#include "FactionService.hpp"

#include <limits>
#include <map>
#include <stdexcept>

#include <components/openmw-mp/PlayerCrimeState.hpp>
#include <components/openmw-mp/Sha256.hpp>

namespace mwmp
{
    namespace
    {
        bool defaultEntry(const PlayerFactionEntry& entry)
        {
            return entry.rank == -1 && entry.reputation == 0 && !entry.expelled;
        }

        bool rankExists(const FactionService::FactionDefinition& definition, std::int64_t rank)
        {
            return rank >= 0 && static_cast<std::size_t>(rank) < definition.validRanks.size()
                && definition.validRanks[static_cast<std::size_t>(rank)];
        }
    }

    FactionService::Outcome FactionService::execute(FactionMutationRequest request, const Context& context)
    {
        request = canonicalizeFactionMutationRequest(std::move(request));
        Outcome outcome;
        outcome.result.requestId = request.requestId;

        if (context.accountId <= 0 || context.characterId <= 0 || !context.findFaction)
        {
            outcome.result.error = FactionError::Unauthorized;
            return outcome;
        }

        std::string canonicalRequest;
        try
        {
            canonicalRequest = encodeFactionMutationRequest(request);
        }
        catch (const std::exception&)
        {
            outcome.result.error = FactionError::InvalidRequest;
            return outcome;
        }
        const std::string requestHash = crypto::sha256hex(canonicalRequest);
        const auto existing
            = mDatabase.loadSemanticRequest("faction", context.accountId, context.characterId, request.requestId);
        if (existing)
        {
            if (existing->requestHash != requestHash)
            {
                outcome.result.error = FactionError::DuplicateConflict;
                outcome.result.state = mDatabase.loadPlayerFactionState(context.characterId);
                return outcome;
            }
            try
            {
                outcome.result = decodeFactionMutationResult(existing->resultPayload);
            }
            catch (const std::exception&)
            {
                outcome.result.error = FactionError::CorruptStoredResult;
                outcome.result.state = mDatabase.loadPlayerFactionState(context.characterId);
                return outcome;
            }
            outcome.replayed = true;
            return outcome;
        }

        outcome.result.state = mDatabase.loadPlayerFactionState(context.characterId);
        auto reject = [&](FactionError error) {
            outcome.result.accepted = false;
            outcome.result.error = error;
            if (request.requestId.empty() || request.requestId.size() > MaximumSemanticRequestIdLength
                || request.source.empty() || request.source.size() > MaximumSemanticSourceLength)
                return;

            SemanticRequestRecord journal;
            journal.service = "faction";
            journal.accountId = context.accountId;
            journal.characterId = context.characterId;
            journal.requestId = request.requestId;
            journal.requestHash = requestHash;
            journal.status = "rejected";
            journal.errorCode = static_cast<std::uint16_t>(error);
            journal.resultPayload = encodeFactionMutationResult(outcome.result);
            journal.source = request.source;
            mDatabase.insertRejectedSemanticRequest(journal);
        };

        if (const FactionError error = validateFactionMutationRequest(request); error != FactionError::None)
        {
            reject(error);
            return outcome;
        }
        if (request.expectedRevision && *request.expectedRevision != outcome.result.state.revision)
        {
            reject(FactionError::StaleRevision);
            return outcome;
        }
        if (outcome.result.state.revision >= MaximumPersistedRevision)
        {
            reject(FactionError::RevisionOverflow);
            return outcome;
        }

        std::map<std::string, PlayerFactionEntry> entries;
        for (const PlayerFactionEntry& entry : outcome.result.state.factions)
            entries.emplace(entry.factionId, entry);

        for (const FactionMutation& mutation : request.mutations)
        {
            const std::optional<FactionDefinition> definition = context.findFaction(mutation.factionId);
            if (!definition)
            {
                reject(FactionError::InvalidFaction);
                return outcome;
            }

            PlayerFactionEntry& entry = entries[mutation.factionId];
            entry.factionId = mutation.factionId;
            switch (mutation.kind)
            {
                case FactionMutationKind::JoinFaction:
                    if (!rankExists(*definition, 0))
                    {
                        reject(FactionError::InvalidRank);
                        return outcome;
                    }
                    if (entry.rank < 0)
                        entry.rank = 0;
                    break;
                case FactionMutationKind::LeaveFaction:
                    entry.rank = -1;
                    entry.expelled = false;
                    break;
                case FactionMutationKind::SetFactionRank:
                    if (entry.rank < 0)
                    {
                        reject(FactionError::InvalidTransition);
                        return outcome;
                    }
                    if (!rankExists(*definition, mutation.value))
                    {
                        reject(FactionError::InvalidRank);
                        return outcome;
                    }
                    entry.rank = static_cast<std::int32_t>(mutation.value);
                    break;
                case FactionMutationKind::ModifyFactionRank:
                {
                    if (entry.rank < 0)
                    {
                        reject(FactionError::InvalidTransition);
                        return outcome;
                    }
                    const std::int64_t next = static_cast<std::int64_t>(entry.rank) + mutation.value;
                    if (next < -1 || (next >= 0 && !rankExists(*definition, next)))
                    {
                        reject(FactionError::InvalidRank);
                        return outcome;
                    }
                    entry.rank = static_cast<std::int32_t>(next);
                    if (entry.rank < 0)
                        entry.expelled = false;
                    break;
                }
                case FactionMutationKind::SetFactionReputation:
                    entry.reputation = static_cast<std::int32_t>(mutation.value);
                    break;
                case FactionMutationKind::ModifyFactionReputation:
                {
                    const std::int64_t next = static_cast<std::int64_t>(entry.reputation) + mutation.value;
                    if (next < std::numeric_limits<std::int32_t>::min()
                        || next > std::numeric_limits<std::int32_t>::max())
                    {
                        reject(FactionError::InvalidReputation);
                        return outcome;
                    }
                    entry.reputation = static_cast<std::int32_t>(next);
                    break;
                }
                case FactionMutationKind::ExpelFromFaction:
                    entry.expelled = true;
                    break;
                case FactionMutationKind::ClearFactionExpulsion:
                    entry.expelled = false;
                    break;
            }
        }

        PlayerFactionState next;
        next.revision = outcome.result.state.revision + 1;
        for (auto& [id, entry] : entries)
        {
            if (!defaultEntry(entry))
                next.factions.push_back(std::move(entry));
        }
        next = canonicalizePlayerFactionState(std::move(next));
        if (const FactionError error = validatePlayerFactionState(next); error != FactionError::None)
        {
            reject(error);
            return outcome;
        }

        outcome.result.accepted = true;
        outcome.result.error = FactionError::None;
        outcome.result.state = next;

        FactionMutationCommit commit;
        commit.accountId = context.accountId;
        commit.characterId = context.characterId;
        commit.requestId = request.requestId;
        commit.requestHash = requestHash;
        commit.resultPayload = encodeFactionMutationResult(outcome.result);
        commit.source = request.source;
        commit.expectedRevision = next.revision - 1;
        commit.resultingState = next;
        commit.failurePoint = context.failurePoint;

        const FactionCommitResult committed = mDatabase.commitPlayerFactionMutation(commit);
        switch (committed.status)
        {
            case FactionCommitStatus::Committed:
                outcome.committed = true;
                return outcome;
            case FactionCommitStatus::DuplicateRequest:
                outcome.result = decodeFactionMutationResult(committed.storedResultPayload);
                outcome.replayed = true;
                return outcome;
            case FactionCommitStatus::DuplicateRequestConflict:
                outcome.result.accepted = false;
                outcome.result.error = FactionError::DuplicateConflict;
                outcome.result.state = mDatabase.loadPlayerFactionState(context.characterId);
                return outcome;
            case FactionCommitStatus::StaleRevision:
                outcome.result.accepted = false;
                outcome.result.error = FactionError::StaleRevision;
                outcome.result.state = committed.currentState;
                return outcome;
        }
        throw std::logic_error("Unknown faction commit status");
    }
}
