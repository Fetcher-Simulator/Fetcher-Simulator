#include "MechanicsSnapshotRegistry.hpp"

namespace mwmp
{
    namespace
    {
        MechanicsSubjectKey subjectOf(const MechanicsSnapshot& snapshot)
        {
            return { snapshot.kind, snapshot.playerGuid, snapshot.actorInstanceId };
        }
    }

    MechanicsSnapshotError MechanicsSnapshotRegistry::accept(const MechanicsSnapshot& proposed,
        const std::optional<MechanicsSnapshotExpectation>& expectation, std::uint64_t receivedAtMs)
    {
        if (!validateMechanicsSnapshot(proposed) || receivedAtMs == 0)
            return MechanicsSnapshotError::InvalidSnapshot;
        if (!expectation)
            return MechanicsSnapshotError::UnknownSubject;

        const MechanicsSubjectKey proposedSubject = subjectOf(proposed);
        if (proposedSubject.playerGuid != expectation->subject.playerGuid
            || proposedSubject.actorInstanceId != expectation->subject.actorInstanceId)
            return MechanicsSnapshotError::WrongSubject;
        if (proposedSubject.kind != expectation->subject.kind)
            return MechanicsSnapshotError::WrongActorKind;
        if (proposed.cellId != expectation->cellId)
            return MechanicsSnapshotError::WrongCell;
        if (proposed.migrationGeneration != expectation->migrationGeneration)
            return MechanicsSnapshotError::WrongMigrationGeneration;
        if (proposed.authorityGeneration != expectation->authorityGeneration)
            return MechanicsSnapshotError::WrongAuthorityGeneration;

        MechanicsSnapshotSource source = MechanicsSnapshotSource::PlayerClientDelegated;
        if (proposed.kind == MechanicsSubjectKind::Player)
        {
            if (expectation->authenticatedPlayerGuid == 0
                || proposed.playerGuid != expectation->authenticatedPlayerGuid)
                return MechanicsSnapshotError::UnauthorizedSender;
        }
        else
        {
            if (!expectation->actorSenderEntitled)
                return MechanicsSnapshotError::UnauthorizedSender;
            source = MechanicsSnapshotSource::ActorAuthorityDelegated;
        }

        const auto current = mSnapshots.find(proposedSubject);
        if (current != mSnapshots.end()
            && current->second.snapshot.migrationGeneration == proposed.migrationGeneration
            && current->second.snapshot.authorityGeneration == proposed.authorityGeneration
            && proposed.snapshotSequence <= current->second.snapshot.snapshotSequence)
            return MechanicsSnapshotError::ReplayOrOutOfOrder;

        mSnapshots.insert_or_assign(proposedSubject,
            AcceptedMechanicsSnapshot { proposed, source, receivedAtMs });
        return MechanicsSnapshotError::None;
    }

    const AcceptedMechanicsSnapshot* MechanicsSnapshotRegistry::find(const MechanicsSubjectKey& subject) const
    {
        const auto found = mSnapshots.find(subject);
        return found == mSnapshots.end() ? nullptr : &found->second;
    }

    const AcceptedMechanicsSnapshot* MechanicsSnapshotRegistry::findFresh(
        const MechanicsSubjectKey& subject, std::uint64_t nowMs, std::uint64_t maximumAgeMs) const
    {
        const AcceptedMechanicsSnapshot* accepted = find(subject);
        if (accepted == nullptr || nowMs < accepted->receivedAtMs
            || nowMs - accepted->receivedAtMs > maximumAgeMs)
            return nullptr;
        return accepted;
    }

    bool MechanicsSnapshotRegistry::erase(const MechanicsSubjectKey& subject)
    {
        return mSnapshots.erase(subject) != 0;
    }

    std::size_t MechanicsSnapshotRegistry::erasePlayer(std::uint32_t playerGuid)
    {
        std::size_t erased = 0;
        for (auto current = mSnapshots.begin(); current != mSnapshots.end();)
        {
            if (current->first.kind == MechanicsSubjectKind::Player
                && current->first.playerGuid == playerGuid)
            {
                current = mSnapshots.erase(current);
                ++erased;
            }
            else
                ++current;
        }
        return erased;
    }
}
