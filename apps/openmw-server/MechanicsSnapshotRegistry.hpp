#ifndef OPENMW_APPS_OPENMW_SERVER_MECHANICSSNAPSHOTREGISTRY_HPP
#define OPENMW_APPS_OPENMW_SERVER_MECHANICSSNAPSHOTREGISTRY_HPP

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>

#include <components/openmw-mp/Base/MechanicsSnapshot.hpp>

namespace mwmp
{
    enum class MechanicsSnapshotSource : std::uint8_t
    {
        PlayerClientDelegated,
        ActorAuthorityDelegated,
    };

    enum class MechanicsSnapshotError : std::uint8_t
    {
        None,
        InvalidSnapshot,
        UnknownSubject,
        WrongSubject,
        WrongActorKind,
        WrongCell,
        WrongMigrationGeneration,
        WrongAuthorityGeneration,
        UnauthorizedSender,
        ReplayOrOutOfOrder,
    };

    struct MechanicsSubjectKey
    {
        MechanicsSubjectKind kind = MechanicsSubjectKind::Player;
        std::uint32_t playerGuid = 0;
        ActorInstanceId actorInstanceId = 0;

        bool operator==(const MechanicsSubjectKey&) const = default;
        bool operator<(const MechanicsSubjectKey& other) const
        {
            if (kind != other.kind)
                return kind < other.kind;
            if (playerGuid != other.playerGuid)
                return playerGuid < other.playerGuid;
            return actorInstanceId < other.actorInstanceId;
        }
    };

    struct MechanicsSnapshotExpectation
    {
        MechanicsSubjectKey subject;
        std::string cellId;
        std::uint32_t migrationGeneration = 0;
        std::uint32_t authorityGeneration = 0;
        std::uint32_t authenticatedPlayerGuid = 0;
        bool actorSenderEntitled = false;
    };

    struct AcceptedMechanicsSnapshot
    {
        MechanicsSnapshot snapshot;
        MechanicsSnapshotSource source = MechanicsSnapshotSource::PlayerClientDelegated;
        std::uint64_t receivedAtMs = 0;

        bool operator==(const AcceptedMechanicsSnapshot&) const = default;
    };

    class MechanicsSnapshotRegistry
    {
    public:
        MechanicsSnapshotError accept(const MechanicsSnapshot& proposed,
            const std::optional<MechanicsSnapshotExpectation>& expectation, std::uint64_t receivedAtMs);

        const AcceptedMechanicsSnapshot* find(const MechanicsSubjectKey& subject) const;
        const AcceptedMechanicsSnapshot* findFresh(
            const MechanicsSubjectKey& subject, std::uint64_t nowMs, std::uint64_t maximumAgeMs) const;
        bool erase(const MechanicsSubjectKey& subject);
        std::size_t erasePlayer(std::uint32_t playerGuid);
        std::size_t size() const { return mSnapshots.size(); }
        void clear() { mSnapshots.clear(); }

    private:
        std::map<MechanicsSubjectKey, AcceptedMechanicsSnapshot> mSnapshots;
    };
}

#endif
