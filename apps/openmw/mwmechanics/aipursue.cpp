#include "aipursue.hpp"

#include <components/esm3/aisequence.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/class.hpp"

#include "actorutil.hpp"
#include "character.hpp"
#include "creaturestats.hpp"
#include "npcstats.hpp"

#ifdef BUILD_MULTIPLAYER
#include "../mwmp/Main.hpp"
#endif

namespace MWMechanics
{

    AiPursue::AiPursue(const MWWorld::Ptr& actor, bool serverAuthorizedCrimePursuit)
        : mServerAuthorizedCrimePursuit(serverAuthorizedCrimePursuit)
    {
        mTargetActor = actor.getCellRef().getRefNum();
    }

    AiPursue::AiPursue(const ESM::AiSequence::AiPursue* pursue)
    {
        mTargetActor = pursue->mTargetActor;
    }

    bool AiPursue::execute(
        const MWWorld::Ptr& actor, CharacterController& characterController, AiState& state, float duration)
    {
        if (actor.getClass().getCreatureStats(actor).isDead())
            return true;

        const MWWorld::Ptr target = getTarget(); // The target to follow

        // Stop if the target doesn't exist
        if (target.isEmpty() || !target.getCellRef().getCount() || !target.getRefData().isEnabled()
            || !target.getRefData().getBaseNode())
            return true;

        if (isTargetMagicallyHidden(target)
            && !MWBase::Environment::get().getMechanicsManager()->awarenessCheck(target, actor, false))
            return false;

        if (target.getClass().getCreatureStats(target).isDead())
            return true;

        if (!mServerAuthorizedCrimePursuit && target.getClass().getNpcStats(target).getBounty() <= 0)
            return true;

        actor.getClass().getCreatureStats(actor).setDrawState(DrawState::Nothing);

        // Set the target destination
        const osg::Vec3f dest = target.getRefData().getPosition().asVec3();
        const osg::Vec3f actorPos = actor.getRefData().getPosition().asVec3();

        const float pathTolerance = 100.f;

        // check the true distance in case the target is far away in Z-direction
        bool reached = pathTo(actor, dest, duration, characterController.getSupportedMovementDirections(),
                           pathTolerance, (actorPos - dest).length(), PathType::Partial)
            && std::abs(dest.z() - actorPos.z()) < pathTolerance;

        if (reached)
        {
            if (!MWBase::Environment::get().getWorld()->getLOS(target, actor))
                return false;

#ifdef BUILD_MULTIPLAYER
            // Multiplayer does not pause the simulation while dialogue is
            // open. Servers can therefore replace the modal arrest choice
            // with an immediate resist-arrest outcome.
            if (mwmp::Main::isInitialised())
            {
                mwmp::Main& multiplayer = mwmp::Main::get();
                if (multiplayer.getGuardArrestMode() == mwmp::GuardArrestMode::Combat)
                {
                    MWBase::Environment::get().getMechanicsManager()->startCombat(actor, target, nullptr);
                    return true;
                }

                if (mServerAuthorizedCrimePursuit && target != MWMechanics::getPlayer())
                {
                    int offenderGuid = 0;
                    if (auto* baseNode = target.getRefData().getBaseNode())
                        baseNode->getUserValue("mp_player_guid", offenderGuid);
                    if (offenderGuid > 0)
                    {
                        // The actor-authority client owns the guard simulation,
                        // not the pursued remote player's UI. Route the arrest
                        // prompt through the server instead of opening dialogue
                        // on whichever client happens to own this guard.
                        if (multiplayer.reportGuardArrestReach(actor, static_cast<std::uint32_t>(offenderGuid)))
                            return true;
                        return false;
                    }
                }
            }
#endif

            MWBase::WindowManager* windowManager = MWBase::Environment::get().getWindowManager();
            if (windowManager->containsMode(MWGui::GM_Dialogue))
                return false;

            windowManager->pushGuiMode(MWGui::GM_Dialogue, actor); // Arrest player when reached
            return true;
        }

        actor.getClass().getCreatureStats(actor).setMovementFlag(
            MWMechanics::CreatureStats::Flag_Run, true); // Make NPC run

        return false;
    }

    void AiPursue::writeState(ESM::AiSequence::AiSequence& sequence) const
    {
        auto pursue = std::make_unique<ESM::AiSequence::AiPursue>();
        pursue->mTargetActor = mTargetActor;

        ESM::AiSequence::AiPackageContainer package;
        package.mType = ESM::AiSequence::Ai_Pursue;
        package.mPackage = std::move(pursue);
        sequence.mPackages.push_back(std::move(package));
    }

} // namespace MWMechanics
