#include "CrimeService.hpp"

#include <limits>
#include <stdexcept>

#include <components/openmw-mp/Sha256.hpp>

namespace mwmp
{
    CrimeService::TransitionOutcome CrimeService::commitAuthoritativeTransition(
        const AuthoritativeTransition& transition, const Context& context)
    {
        if (context.accountId <= 0 || context.characterId <= 0)
            throw std::invalid_argument("Crime transition requires an authenticated character");
        if (transition.requestId.empty() || transition.requestId.size() > MaximumSemanticRequestIdLength
            || transition.requestId.find('\0') != std::string::npos || transition.requestHash.size() != 64
            || transition.source.empty() || transition.source.size() > MaximumSemanticSourceLength
            || transition.source.find('\0') != std::string::npos || transition.bountyDelta < 0
            || transition.terminalResultPayload.empty())
            throw std::invalid_argument("Invalid authoritative crime transition");

        const PlayerCrimeState current = mDatabase.loadPlayerCrimeState(context.characterId);
        TransitionOutcome outcome;
        outcome.state = current;
        if (current.revision != transition.expectedRevision)
        {
            outcome.status = CrimeCommitStatus::StaleRevision;
            return outcome;
        }

        const bool changesState = transition.bountyDelta != 0 || transition.advanceCurrentCrimeId;
        if (changesState && current.revision >= MaximumPersistedRevision)
            throw std::overflow_error("Authoritative crime-state revision overflow");
        if (transition.bountyDelta > std::numeric_limits<std::int32_t>::max() - current.bounty)
            throw std::overflow_error("Authoritative crime bounty overflow");
        if (transition.advanceCurrentCrimeId && current.currentCrimeId == std::numeric_limits<std::int32_t>::max())
            throw std::overflow_error("Authoritative current crime id overflow");

        PlayerCrimeState next = current;
        next.bounty += static_cast<std::int32_t>(transition.bountyDelta);
        if (transition.advanceCurrentCrimeId)
            ++next.currentCrimeId;
        if (changesState)
            ++next.revision;

        CrimeMutationCommit commit;
        commit.service = "crime-event";
        commit.accountId = context.accountId;
        commit.characterId = context.characterId;
        commit.requestId = transition.requestId;
        commit.requestHash = transition.requestHash;
        commit.resultPayload = transition.terminalResultPayload;
        commit.source = transition.source;
        commit.expectedRevision = current.revision;
        commit.resultingState = next;
        commit.failurePoint = context.failurePoint;

        const CrimeCommitResult committed = mDatabase.commitPlayerCrimeMutation(commit);
        outcome.status = committed.status;
        outcome.state = committed.status == CrimeCommitStatus::Committed ? next : committed.currentState;
        outcome.storedResultPayload = committed.storedResultPayload;
        return outcome;
    }

    CrimeService::Outcome CrimeService::execute(const CrimeMutationRequest& request, const Context& context)
    {
        Outcome outcome;
        outcome.result.requestId = request.requestId;

        if (context.accountId <= 0 || context.characterId <= 0)
        {
            outcome.result.error = CrimeError::Unauthorized;
            return outcome;
        }

        const std::string canonicalRequest = encodeCrimeMutationRequest(request);
        const std::string requestHash = crypto::sha256hex(canonicalRequest);
        const auto existing
            = mDatabase.loadSemanticRequest("crime", context.accountId, context.characterId, request.requestId);
        if (existing)
        {
            if (existing->requestHash != requestHash)
            {
                outcome.result.error = CrimeError::DuplicateConflict;
                outcome.result.state = mDatabase.loadPlayerCrimeState(context.characterId);
                return outcome;
            }
            try
            {
                outcome.result = decodeCrimeMutationResult(existing->resultPayload);
            }
            catch (const std::exception&)
            {
                outcome.result.error = CrimeError::CorruptStoredResult;
                outcome.result.state = mDatabase.loadPlayerCrimeState(context.characterId);
                return outcome;
            }
            outcome.replayed = true;
            return outcome;
        }

        outcome.result.state = mDatabase.loadPlayerCrimeState(context.characterId);

        auto reject = [&](CrimeError error) {
            outcome.result.accepted = false;
            outcome.result.error = error;
            if (request.requestId.empty() || request.requestId.size() > MaximumSemanticRequestIdLength
                || request.source.empty() || request.source.size() > MaximumSemanticSourceLength)
                return;

            SemanticRequestRecord journal;
            journal.service = "crime";
            journal.accountId = context.accountId;
            journal.characterId = context.characterId;
            journal.requestId = request.requestId;
            journal.requestHash = requestHash;
            journal.status = "rejected";
            journal.errorCode = static_cast<std::uint16_t>(error);
            journal.resultPayload = encodeCrimeMutationResult(outcome.result);
            journal.source = request.source;
            mDatabase.insertRejectedSemanticRequest(journal);
        };

        if (const CrimeError error = validateCrimeMutationRequest(request); error != CrimeError::None)
        {
            reject(error);
            return outcome;
        }
        if (request.expectedRevision && *request.expectedRevision != outcome.result.state.revision)
        {
            reject(CrimeError::StaleRevision);
            return outcome;
        }
        if (outcome.result.state.revision >= MaximumPersistedRevision)
        {
            reject(CrimeError::RevisionOverflow);
            return outcome;
        }

        std::int64_t nextBounty = 0;
        if (request.kind == CrimeMutationKind::SetBounty)
            nextBounty = request.value;
        else
        {
            const std::int64_t current = outcome.result.state.bounty;
            if (request.value > std::numeric_limits<std::int32_t>::max() - current || request.value < -current)
            {
                reject(CrimeError::InvalidBounty);
                return outcome;
            }
            nextBounty = current + request.value;
        }
        if (nextBounty < 0 || nextBounty > std::numeric_limits<std::int32_t>::max())
        {
            reject(CrimeError::InvalidBounty);
            return outcome;
        }

        PlayerCrimeState next = outcome.result.state;
        next.bounty = static_cast<std::int32_t>(nextBounty);
        if (next.bounty == 0)
            next.paidCrimeId = next.currentCrimeId;
        ++next.revision;

        outcome.result.accepted = true;
        outcome.result.error = CrimeError::None;
        outcome.result.state = next;

        CrimeMutationCommit commit;
        commit.accountId = context.accountId;
        commit.characterId = context.characterId;
        commit.requestId = request.requestId;
        commit.requestHash = requestHash;
        commit.resultPayload = encodeCrimeMutationResult(outcome.result);
        commit.source = request.source;
        commit.expectedRevision = next.revision - 1;
        commit.resultingState = next;
        commit.failurePoint = context.failurePoint;

        const CrimeCommitResult committed = mDatabase.commitPlayerCrimeMutation(commit);
        switch (committed.status)
        {
            case CrimeCommitStatus::Committed:
                outcome.committed = true;
                return outcome;
            case CrimeCommitStatus::DuplicateRequest:
                outcome.result = decodeCrimeMutationResult(committed.storedResultPayload);
                outcome.replayed = true;
                return outcome;
            case CrimeCommitStatus::DuplicateRequestConflict:
                outcome.result.accepted = false;
                outcome.result.error = CrimeError::DuplicateConflict;
                outcome.result.state = mDatabase.loadPlayerCrimeState(context.characterId);
                return outcome;
            case CrimeCommitStatus::StaleRevision:
                outcome.result.accepted = false;
                outcome.result.error = CrimeError::StaleRevision;
                outcome.result.state = committed.currentState;
                return outcome;
        }
        throw std::logic_error("Unknown crime commit status");
    }
}
