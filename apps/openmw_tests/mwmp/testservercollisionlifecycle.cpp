#include <gtest/gtest.h>

#include <apps/openmw-server/ServerCollisionLifecycle.hpp>

TEST(ServerCollisionLifecycle, DuplicateAcquireAndReleaseUseOneLoadedCell)
{
    mwmp::ServerCollisionLifecycle lifecycle;

    const auto first = lifecycle.acquire("EXT:-3,-2");
    EXPECT_TRUE(first.accepted);
    EXPECT_TRUE(first.load);
    EXPECT_EQ(first.state.refCount, 1u);
    EXPECT_EQ(first.state.generation, 1u);

    const auto duplicate = lifecycle.acquire("EXT:-3,-2");
    EXPECT_TRUE(duplicate.accepted);
    EXPECT_FALSE(duplicate.load);
    EXPECT_EQ(duplicate.state.refCount, 2u);
    EXPECT_EQ(duplicate.state.generation, 1u);

    const auto retained = lifecycle.release("EXT:-3,-2");
    EXPECT_TRUE(retained.accepted);
    EXPECT_FALSE(retained.unload);
    EXPECT_EQ(retained.state.refCount, 1u);
    EXPECT_EQ(retained.state.generation, 1u);

    const auto unloaded = lifecycle.release("EXT:-3,-2");
    EXPECT_TRUE(unloaded.accepted);
    EXPECT_TRUE(unloaded.unload);
    EXPECT_EQ(unloaded.state.refCount, 0u);
    EXPECT_EQ(unloaded.state.generation, 2u);
}

TEST(ServerCollisionLifecycle, ReloadAdvancesRetainedGeneration)
{
    mwmp::ServerCollisionLifecycle lifecycle;
    lifecycle.acquire("Balmora, Guild of Mages");
    lifecycle.release("Balmora, Guild of Mages");

    const auto reloaded = lifecycle.acquire("Balmora, Guild of Mages");
    EXPECT_TRUE(reloaded.load);
    EXPECT_EQ(reloaded.state.generation, 3u);
}

TEST(ServerCollisionLifecycle, MultiCellGenerationsInvalidateOnlyChangedCell)
{
    mwmp::ServerCollisionLifecycle lifecycle;
    const auto first = lifecycle.acquire("EXT:-3,-2");
    const auto adjacent = lifecycle.acquire("EXT:-2,-2");

    EXPECT_EQ(first.state.generation, 1u);
    EXPECT_EQ(adjacent.state.generation, 1u);

    const auto changed = lifecycle.touch("EXT:-2,-2");
    EXPECT_EQ(changed.generation, 2u);
    EXPECT_EQ(lifecycle.state("EXT:-3,-2").generation, 1u);

    EXPECT_TRUE(lifecycle.release("EXT:-2,-2").unload);
    EXPECT_EQ(lifecycle.state("EXT:-2,-2").generation, 3u);
    EXPECT_EQ(lifecycle.acquire("EXT:-2,-2").state.generation, 4u);
    EXPECT_EQ(lifecycle.state("EXT:-3,-2").generation, 1u);
}

TEST(ServerCollisionLifecycle, DynamicBlockerTouchInvalidatesLoadedCell)
{
    mwmp::ServerCollisionLifecycle lifecycle;
    lifecycle.acquire("EXT:-3,-2");

    const auto touched = lifecycle.touch("EXT:-3,-2");
    EXPECT_EQ(touched.refCount, 1u);
    EXPECT_EQ(touched.generation, 2u);
    EXPECT_EQ(lifecycle.state("EXT:-3,-2").generation, 2u);
}

TEST(ServerCollisionLifecycle, UnknownOrUnloadedMutationIsRejected)
{
    mwmp::ServerCollisionLifecycle lifecycle;
    EXPECT_FALSE(lifecycle.release("missing").accepted);
    EXPECT_EQ(lifecycle.touch("missing").generation, 0u);

    lifecycle.acquire("known");
    lifecycle.release("known");
    EXPECT_EQ(lifecycle.touch("known").generation, 0u);
    EXPECT_FALSE(lifecycle.release("known").accepted);
}

TEST(ServerCollisionLifecycle, ClearUnloadsEveryActiveCellAndPreservesEpochs)
{
    mwmp::ServerCollisionLifecycle lifecycle;
    lifecycle.acquire("A");
    lifecycle.acquire("B");
    lifecycle.acquire("B");

    const std::vector<std::string> unloaded = lifecycle.clear();
    EXPECT_EQ(unloaded, (std::vector<std::string>{ "A", "B" }));
    EXPECT_EQ(lifecycle.state("A").refCount, 0u);
    EXPECT_EQ(lifecycle.state("A").generation, 2u);
    EXPECT_EQ(lifecycle.state("B").refCount, 0u);
    EXPECT_EQ(lifecycle.state("B").generation, 2u);

    EXPECT_EQ(lifecycle.acquire("B").state.generation, 3u);
}

TEST(ServerCollisionLifecycle, EmptyCellIdentityIsRejected)
{
    mwmp::ServerCollisionLifecycle lifecycle;
    EXPECT_THROW(lifecycle.acquire(""), std::invalid_argument);
}
