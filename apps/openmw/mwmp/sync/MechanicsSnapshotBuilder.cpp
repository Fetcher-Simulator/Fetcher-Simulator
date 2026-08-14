#include "MechanicsSnapshotBuilder.hpp"

#include <components/esm/attr.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadskil.hpp>

#include "../../mwbase/environment.hpp"
#include "../../mwbase/mechanicsmanager.hpp"
#include "../../mwbase/world.hpp"
#include "../../mwmechanics/creaturestats.hpp"
#include "../../mwmechanics/magiceffects.hpp"
#include "../../mwworld/class.hpp"
#include "../../mwworld/ptr.hpp"

namespace mwmp
{
    MechanicsSnapshot captureMechanicsSnapshot(const MWWorld::Ptr& ptr, MechanicsSubjectKind kind,
        std::uint32_t playerGuid, ActorInstanceId actorInstanceId, const std::string& cellId,
        std::uint32_t migrationGeneration, std::uint32_t authorityGeneration,
        std::uint32_t snapshotSequence)
    {
        MechanicsSnapshot snapshot;
        snapshot.kind = kind;
        snapshot.playerGuid = playerGuid;
        snapshot.actorInstanceId = actorInstanceId;
        snapshot.cellId = cellId;
        snapshot.migrationGeneration = migrationGeneration;
        snapshot.authorityGeneration = authorityGeneration;
        snapshot.snapshotSequence = snapshotSequence;

        const ESM::Position& position = ptr.getRefData().getPosition();
        for (int axis = 0; axis < 3; ++axis)
        {
            snapshot.position.pos[axis] = position.pos[axis];
            snapshot.position.rot[axis] = position.rot[axis];
        }

        const MWMechanics::CreatureStats& stats = ptr.getClass().getCreatureStats(ptr);
        const bool alive = !stats.isDead();
        const bool conscious = alive && !stats.getKnockedDown();
        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWBase::MechanicsManager* mechanics = MWBase::Environment::get().getMechanicsManager();
        if (ptr.getRefData().isEnabled())
            snapshot.stateFlags |= MechanicsEnabled;
        if (alive)
            snapshot.stateFlags |= MechanicsAlive;
        if (conscious)
            snapshot.stateFlags |= MechanicsConscious;
        if (mechanics != nullptr && mechanics->isSneaking(ptr))
            snapshot.stateFlags |= MechanicsSneaking;
        if (world != nullptr && world->isOnGround(ptr))
            snapshot.stateFlags |= MechanicsOnGround;

        snapshot.sneakSkill = ptr.getClass().getSkill(ptr, ESM::Skill::Sneak);
        snapshot.agility = stats.getAttribute(ESM::Attribute::Agility).getModified();
        snapshot.luck = stats.getAttribute(ESM::Attribute::Luck).getModified();
        snapshot.fatigueCurrent = stats.getFatigue().getCurrent();
        snapshot.fatigueMaximumModified = stats.getFatigue().getModified();
        const MWMechanics::MagicEffects& effects = stats.getMagicEffects();
        snapshot.chameleon = effects.getOrDefault(ESM::MagicEffect::Chameleon).getMagnitude();
        snapshot.invisibility = effects.getOrDefault(ESM::MagicEffect::Invisibility).getMagnitude();
        snapshot.blind = effects.getOrDefault(ESM::MagicEffect::Blind).getMagnitude();
        return snapshot;
    }
}
