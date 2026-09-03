#ifndef OPENMW_SERVER_ACTORREGISTRYINVARIANT_HPP
#define OPENMW_SERVER_ACTORREGISTRYINVARIANT_HPP

#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

#include <components/openmw-mp/Base/ActorSyncProtocol.hpp>

namespace mwmp
{
    // The server owns actor lifetime generations. Zero represents an
    // uninitialized canonical lifetime and must never be published for an actor
    // with a usable network identity.
    inline void ensureCanonicalActorMigrationGeneration(
        std::uint32_t& migrationGeneration, BaseActor& actor)
    {
        if (migrationGeneration == 0)
            migrationGeneration = 1;
        actor.migrationGeneration = migrationGeneration;
    }

    inline std::optional<ActorIdentityRecord> makeCorpseDisposedActorIdentity(
        BaseActor actor, std::string_view cellId)
    {
        if (cellId.empty() || actor.mpNum != 0 || actor.refId.empty() || actor.refNum == 0
            || actor.cellId != cellId)
            return std::nullopt;

        const ActorInstanceId actorNetId = actorInstanceIdFromActor(actor);
        if (!isValidActorInstanceId(actorNetId))
            return std::nullopt;

        actor.mpNum = 0;
        actor.cellId = std::string(cellId);
        actor.isDead = false;
        std::uint32_t migrationGeneration = actor.migrationGeneration;
        ensureCanonicalActorMigrationGeneration(migrationGeneration, actor);

        ActorIdentityRecord identity;
        identity.actorNetId = actorNetId;
        identity.persistent = false;
        identity.serverSpawned = false;
        identity.removed = true;
        identity.migrationGeneration = migrationGeneration;
        identity.removalReason = ActorRemovalReason::CorpseDisposed;
        identity.actor = std::move(actor);
        return identity;
    }

    inline std::optional<BaseActor> makeDurableVanillaDeathBaseline(
        BaseActor actor, std::string_view cellId)
    {
        if (cellId.empty() || actor.mpNum != 0 || actor.refId.empty()
            || actor.refNum == 0 || !actor.isDead || actor.cellId != cellId)
            return std::nullopt;

        if (!isValidActorInstanceId(actorInstanceIdFromActor(actor)))
            return std::nullopt;

        actor.mpNum = 0;
        actor.cellId = std::string(cellId);
        actor.isDead = true;
        actor.isInstantDeath = true;
        actor.isMoving = false;
        actor.isAttackingOrCasting = false;
        actor.velocity = {};
        actor.dynamicStats.health.current = 0.f;
        return actor;
    }

    inline bool shouldSendDurableVanillaDeathToClient(bool isSender,
        bool characterSelected, uint32_t actorSyncProtocolVersion, bool hasActorCellLoaded)
    {
        return !isSender && characterSelected
            && actorSyncProtocolVersion >= ActorSyncProtocolVersionV2
            && !hasActorCellLoaded;
    }

    inline bool isGloballyDurableVanillaRemoval(
        ActorRemovalReason removalReason, const BaseActor& actor)
    {
        return removalReason == ActorRemovalReason::CorpseDisposed && actor.mpNum == 0;
    }
}

#endif // OPENMW_SERVER_ACTORREGISTRYINVARIANT_HPP
