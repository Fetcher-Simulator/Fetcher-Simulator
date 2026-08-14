#include "LiveObservationRuntime.hpp"

#include <chrono>
#include <cmath>
#include <random>

namespace
{
    mwmp::ObservationActorKind observationKind(mwmp::MechanicsSubjectKind kind)
    {
        switch (kind)
        {
            case mwmp::MechanicsSubjectKind::Player: return mwmp::ObservationActorKind::Player;
            case mwmp::MechanicsSubjectKind::Npc: return mwmp::ObservationActorKind::Npc;
            case mwmp::MechanicsSubjectKind::Creature: return mwmp::ObservationActorKind::Creature;
        }
        return mwmp::ObservationActorKind::Player;
    }

    bool hasFlag(const mwmp::MechanicsSnapshot& snapshot, mwmp::MechanicsStateFlags flag)
    {
        return (snapshot.stateFlags & static_cast<std::uint8_t>(flag)) != 0;
    }
}

mwmp::ServerAwarenessRollSource::ServerAwarenessRollSource()
    : ServerAwarenessRollSource(static_cast<std::uint64_t>(std::random_device{}())
        ^ static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()))
{
}

mwmp::ServerAwarenessRollSource::ServerAwarenessRollSource(std::uint64_t seed)
    : mEngine(seed)
{
}

int mwmp::ServerAwarenessRollSource::nextRoll0To99()
{
    return mDistribution(mEngine);
}

mwmp::ObservationActorSnapshot mwmp::makeLiveObservationSnapshot(
    const AcceptedMechanicsSnapshot& accepted, float serverDerivedBootWeight)
{
    const MechanicsSnapshot& snapshot = accepted.snapshot;
    ObservationActorSnapshot result;
    result.identity.kind = observationKind(snapshot.kind);
    result.identity.playerGuid = snapshot.playerGuid;
    result.identity.actorInstanceId = snapshot.actorInstanceId;
    result.position = { snapshot.position.pos[0], snapshot.position.pos[1], snapshot.position.pos[2] };
    const float yaw = snapshot.position.rot[2];
    result.forward = { -std::sin(yaw), std::cos(yaw), 0.f };
    result.hasFacing = true;
    result.eligibilityKnown = true;
    result.awarenessInputsKnown = true;
    result.enabled = hasFlag(snapshot, MechanicsEnabled);
    result.alive = hasFlag(snapshot, MechanicsAlive);
    result.conscious = hasFlag(snapshot, MechanicsConscious);
    result.sneaking = hasFlag(snapshot, MechanicsSneaking);
    result.onGround = hasFlag(snapshot, MechanicsOnGround);
    result.sneakSkill = snapshot.sneakSkill;
    result.agility = snapshot.agility;
    result.luck = snapshot.luck;
    result.fatigueCurrent = snapshot.fatigueCurrent;
    result.fatigueMaximumModified = snapshot.fatigueMaximumModified;
    result.bootWeight = serverDerivedBootWeight;
    result.chameleon = snapshot.chameleon;
    result.invisibility = snapshot.invisibility;
    result.blind = snapshot.blind;
    result.migrationGeneration = snapshot.migrationGeneration;
    result.authorityGeneration = snapshot.authorityGeneration;
    result.snapshotGeneration = snapshot.snapshotSequence;
    result.sampledAtMs = accepted.receivedAtMs;
    result.authority = accepted.source == MechanicsSnapshotSource::PlayerClientDelegated
        ? ObservationAuthority::PlayerClientDelegated
        : ObservationAuthority::ActorAuthorityDelegated;
    return result;
}
