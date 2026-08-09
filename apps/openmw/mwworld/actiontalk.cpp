#include "actiontalk.hpp"

#ifdef BUILD_MULTIPLAYER
#include <components/debug/debuglog.hpp>
#endif

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"

#include "../mwmechanics/actorutil.hpp"

#ifdef BUILD_MULTIPLAYER
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwmechanics/npcstats.hpp"
#include "../mwmp/Main.hpp"

#include "class.hpp"
#endif

namespace MWWorld
{
    ActionTalk::ActionTalk(const Ptr& actor)
        : Action(false, actor)
    {
    }

    void ActionTalk::executeImp(const Ptr& actor)
    {
        if (actor != MWMechanics::getPlayer())
            return;

        const Ptr& target = getTarget();
#ifdef BUILD_MULTIPLAYER
        // Player-initiated guard dialogue bypasses AiPursue, so enforce the server's arrest policy here as well.
        if (mwmp::Main::isInitialised()
            && mwmp::Main::get().getGuardArrestMode() == mwmp::GuardArrestMode::Combat
            && target.getClass().isNpc() && target.getClass().isClass(target, "Guard")
            && actor.getClass().getNpcStats(actor).getBounty() > 0)
        {
            Log(Debug::Info) << "[MP] Guard dialogue converted to combat for " << target.toString();
            MWBase::Environment::get().getMechanicsManager()->startCombat(target, actor, nullptr);
            return;
        }
#endif

        MWBase::Environment::get().getWindowManager()->pushGuiMode(MWGui::GM_Dialogue, target);
    }
}
