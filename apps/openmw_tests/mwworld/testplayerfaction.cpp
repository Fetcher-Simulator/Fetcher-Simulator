#include <gtest/gtest.h>

#include <map>
#include <set>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwmechanics/npcstats.hpp>
#include <apps/openmw/mwmp/sync/PlayerSync.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>

TEST(PlayerFactionMirror, NarrowReplacementIsVisibleThroughNormalNpcStatsGetters)
{
    MWBase::Environment environment;
    MWWorld::ESMStore store;
    environment.setESMStore(store);
    MWMechanics::NpcStats stats;
    stats.setBounty(750);
    stats.setBaseDisposition(42);

    mwmp::PlayerFactionState state;
    state.revision = 9;
    state.factions = {
        { "fighters guild", 2, 17, true },
        { "mages guild", 0, -4, false },
    };
    mwmp::applyPlayerFactionState(stats, state);

    const ESM::RefId fighters = ESM::RefId::stringRefId("fighters guild");
    const ESM::RefId mages = ESM::RefId::stringRefId("mages guild");
    EXPECT_TRUE(stats.isInFaction(fighters));
    EXPECT_EQ(stats.getFactionRank(fighters), 2);
    EXPECT_EQ(stats.getFactionReputation(fighters), 17);
    EXPECT_TRUE(stats.getExpelled(fighters));
    EXPECT_TRUE(stats.isInFaction(mages));
    EXPECT_EQ(stats.getFactionRank(mages), 0);
    EXPECT_EQ(stats.getFactionReputation(mages), -4);
    EXPECT_FALSE(stats.getExpelled(mages));

    // The faction seam must not replace unrelated NpcStats domains.
    EXPECT_EQ(stats.getBounty(), 750);
    EXPECT_EQ(stats.getBaseDisposition(), 42);
}

TEST(PlayerFactionMirror, CaptureIncludesReputationAndExpulsionWithoutMembership)
{
    MWBase::Environment environment;
    MWWorld::ESMStore store;
    environment.setESMStore(store);
    MWMechanics::NpcStats stats;
    const ESM::RefId faction = ESM::RefId::stringRefId("mages guild");
    stats.setFactionReputation(faction, 8);
    stats.expell(faction, false);

    const mwmp::PlayerFactionState state = mwmp::capturePlayerFactionState(stats, 5);
    ASSERT_EQ(state.factions.size(), 1u);
    EXPECT_EQ(state.revision, 5u);
    EXPECT_EQ(state.factions[0], (mwmp::PlayerFactionEntry{ "mages guild", -1, 8, true }));
}

TEST(PlayerFactionMirror, AuthoritativeRemovalClearsOnlyFactionMaps)
{
    MWBase::Environment environment;
    MWWorld::ESMStore store;
    environment.setESMStore(store);
    MWMechanics::NpcStats stats;
    const ESM::RefId faction = ESM::RefId::stringRefId("mages guild");
    stats.joinFaction(faction);
    stats.setFactionReputation(faction, 8);
    stats.expell(faction, false);
    stats.setBounty(90);

    mwmp::applyPlayerFactionState(stats, mwmp::PlayerFactionState{});
    EXPECT_FALSE(stats.isInFaction(faction));
    EXPECT_EQ(stats.getFactionReputation(faction), 0);
    EXPECT_FALSE(stats.getExpelled(faction));
    EXPECT_EQ(stats.getBounty(), 90);
}
