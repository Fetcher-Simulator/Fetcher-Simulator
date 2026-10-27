#include <components/openmw-mp/Packets/System/PacketGameSettings.hpp>
#include <gtest/gtest.h>
#include <limits>

TEST(SurfMovement, NormalizationRequiresExplicitSurfOptIn)
{
    mwmp::SurfPhysicsSettings settings;
    EXPECT_FALSE(settings.hasNormalizedMovement());
    settings.normalizeMovement = true;
    EXPECT_FALSE(settings.hasNormalizedMovement());
    settings.enabled = true;
    EXPECT_TRUE(settings.hasNormalizedMovement());
    EXPECT_FLOAT_EQ(settings.referenceAcrobatics, 50.f);
}

TEST(SurfMovement, MapProfileRejectsPreviouslySetPersonalOverrides)
{
    mwmp::SurfPhysicsSettings global, cell, player;
    cell.enabled = cell.normalizeMovement = true;
    cell.gravityMultiplier = 1.6f;
    player.enabled = false;
    player.gravityMultiplier = 0.1f;
    player.referenceSpeed = 0.f;
    const auto effective = mwmp::resolveSurfPhysicsSettings(global, &cell, &player, "surf_kitsune_mw");
    EXPECT_TRUE(effective.hasNormalizedMovement());
    EXPECT_FLOAT_EQ(effective.gravityMultiplier, 1.6f);
    EXPECT_FLOAT_EQ(effective.referenceSpeed, 100.f);
    EXPECT_EQ(effective.cellId, "surf_kitsune_mw");
    // Leaving the map immediately restores the normal override precedence.
    const auto outside = mwmp::resolveSurfPhysicsSettings(global, nullptr, &player, "Balmora");
    EXPECT_FALSE(outside.hasNormalizedMovement());
    EXPECT_FLOAT_EQ(outside.gravityMultiplier, 0.1f);
}

TEST(SurfMovement, GlobalProfileAlsoRejectsPersonalOverrides)
{
    mwmp::SurfPhysicsSettings global, player;
    global.enabled = global.normalizeMovement = true;
    player.enabled = false;
    EXPECT_TRUE(mwmp::resolveSurfPhysicsSettings(global, nullptr, &player, "Arena").hasNormalizedMovement());
}

TEST(SurfMovement, PacketRetainsProfileAndExistingPhysics)
{
    mwmp::PacketGameSettings sent, received;
    sent.settings.cellId = "surf_mesa_mw";
    sent.settings.enabled = sent.settings.normalizeMovement = true;
    sent.settings.referenceSpeed = 90.f;
    sent.settings.referenceAthletics = 80.f;
    sent.settings.referenceAcrobatics = 50.f;
    sent.settings.airAcceleration = 600.f;
    sent.guardArrestMode = mwmp::GuardArrestMode::Dialogue;
    const auto bytes = sent.encode();
    ASSERT_TRUE(received.decode(bytes.data(), bytes.size()));
    EXPECT_TRUE(received.settings.hasNormalizedMovement());
    EXPECT_EQ(received.settings.cellId, sent.settings.cellId);
    EXPECT_FLOAT_EQ(received.settings.referenceSpeed, 90.f);
    EXPECT_FLOAT_EQ(received.settings.referenceAthletics, 80.f);
    EXPECT_FLOAT_EQ(received.settings.referenceAcrobatics, 50.f);
    EXPECT_FLOAT_EQ(received.settings.airAcceleration, 600.f);
    EXPECT_EQ(received.guardArrestMode, mwmp::GuardArrestMode::Dialogue);
    EXPECT_FALSE(received.decode(bytes.data(), bytes.size() - 1));
}

TEST(SurfMovement, RejectsNonFiniteAndOutOfRangeReferences)
{
    for (float bad : {-1.f, 101.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    {
        mwmp::PacketGameSettings sent, received;
        sent.settings.referenceAcrobatics = bad;
        const auto bytes = sent.encode();
        EXPECT_FALSE(received.decode(bytes.data(), bytes.size()));
    }
}
