#include <gtest/gtest.h>

#include <stdexcept>

#include <components/esm3/loadnpc.hpp>

#include <apps/openmw/mwclass/npc.hpp>
#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwmechanics/npcstats.hpp>
#include <apps/openmw/mwmp/sync/PlayerSync.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/player.hpp>

namespace MWWorld
{
    TEST(PlayerCrimeState, InstallsAuthoritativeCrimeGenerationsInNormalPlayerState)
    {
        MWClass::Npc::registerSelf();
        ESM::NPC npc;
        npc.blank();
        npc.mId = ESM::RefId::stringRefId("Player");
        Player player(&npc);

        EXPECT_EQ(player.getCurrentCrimeId(), -1);
        EXPECT_EQ(player.getCrimeId(), -1);

        MWBase::Environment environment;
        ESMStore store;
        environment.setESMStore(store);
        MWMechanics::NpcStats npcStats;
        mwmp::PlayerCrimeState state;
        state.bounty = 750;
        state.currentCrimeId = 17;
        state.paidCrimeId = 11;
        mwmp::applyAuthoritativeCrimeState(npcStats, player, state);

        EXPECT_EQ(npcStats.getBounty(), 750);
        EXPECT_EQ(player.getCurrentCrimeId(), 17);
        EXPECT_EQ(player.getCrimeId(), 11);
    }

    TEST(PlayerCrimeState, RejectsInvalidAuthoritativeCrimeGenerations)
    {
        MWClass::Npc::registerSelf();
        ESM::NPC npc;
        npc.blank();
        npc.mId = ESM::RefId::stringRefId("Player");
        Player player(&npc);

        EXPECT_THROW(player.setCrimeIds(-2, -1), std::invalid_argument);
        EXPECT_THROW(player.setCrimeIds(4, 5), std::invalid_argument);
        EXPECT_EQ(player.getCurrentCrimeId(), -1);
        EXPECT_EQ(player.getCrimeId(), -1);
    }
}
