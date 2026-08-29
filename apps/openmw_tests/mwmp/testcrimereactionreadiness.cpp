#include <gtest/gtest.h>

#include <apps/openmw/mwmp/sync/CrimeReactionReadiness.hpp>

namespace
{
    mwmp::CrimeReactionDirective directiveWithFlags(std::uint8_t flags)
    {
        mwmp::CrimeReactionDirective directive;
        directive.actors.push_back({ 42, 1, mwmp::CrimeReactionDialogue::None, flags, 0 });
        return directive;
    }
}

TEST(CrimeReactionReadiness, DialogueOnlyDoesNotRequireOffender)
{
    mwmp::CrimeReactionDirective directive;
    directive.actors.push_back({ 42, 1, mwmp::CrimeReactionDialogue::Thief, 0, 0 });

    EXPECT_TRUE(mwmp::crimeReactionOffenderReady(directive, false, false));
}

TEST(CrimeReactionReadiness, ClearPursuitRequiresOffenderButNotSameCell)
{
    const auto directive = directiveWithFlags(mwmp::CrimeReactionClearPursuit);

    EXPECT_FALSE(mwmp::crimeReactionOffenderReady(directive, false, false));
    EXPECT_TRUE(mwmp::crimeReactionOffenderReady(directive, true, false));
}

TEST(CrimeReactionReadiness, PursuitRequiresOffenderInDirectiveCell)
{
    const auto directive = directiveWithFlags(static_cast<std::uint8_t>(
        mwmp::CrimeReactionSetAlarmed | mwmp::CrimeReactionPursueOffender));

    EXPECT_FALSE(mwmp::crimeReactionOffenderReady(directive, false, false));
    EXPECT_FALSE(mwmp::crimeReactionOffenderReady(directive, true, false));
    EXPECT_TRUE(mwmp::crimeReactionOffenderReady(directive, true, true));
}

TEST(CrimeReactionReadiness, CombatRequiresOffenderInDirectiveCell)
{
    const auto directive = directiveWithFlags(static_cast<std::uint8_t>(
        mwmp::CrimeReactionSetAlarmed | mwmp::CrimeReactionStartCombat));

    EXPECT_FALSE(mwmp::crimeReactionOffenderReady(directive, false, false));
    EXPECT_FALSE(mwmp::crimeReactionOffenderReady(directive, true, false));
    EXPECT_TRUE(mwmp::crimeReactionOffenderReady(directive, true, true));
}
