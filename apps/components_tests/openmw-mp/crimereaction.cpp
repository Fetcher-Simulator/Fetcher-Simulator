#include <components/openmw-mp/CrimeReaction.hpp>
#include <components/openmw-mp/Packets/Actor/PacketCrimeReaction.hpp>

TEST(CrimeReactionProtocol, DirectiveRoundTripsStrictly)
{
    mwmp::PacketCrimeReaction outgoing;
    outgoing.directive.eventId = "crime-event-1";
    outgoing.directive.cellId = "Caldera, Irgola: Pawnbroker";
    outgoing.directive.offenderGuid = 22;
    outgoing.directive.actors = {
        { 172965, 1, mwmp::CrimeReactionDialogue::Intruder, 0 },
        { 273861, 1, mwmp::CrimeReactionDialogue::Intruder,
            static_cast<std::uint8_t>(mwmp::CrimeReactionSetAlarmed | mwmp::CrimeReactionPursueOffender), 0 },
        { 373862, 1, mwmp::CrimeReactionDialogue::None,
            static_cast<std::uint8_t>(mwmp::CrimeReactionSetAlarmed | mwmp::CrimeReactionStartCombat
                | mwmp::CrimeReactionSetFight), 100 },
    };

    const auto encoded = outgoing.encode();
    mwmp::PacketCrimeReaction incoming;
    ASSERT_TRUE(incoming.decode(encoded));
    EXPECT_EQ(incoming.directive, outgoing.directive);

    auto trailing = encoded;
    trailing.push_back(0);
    EXPECT_FALSE(incoming.decode(trailing));
}

TEST(CrimeReactionProtocol, RejectsInvalidOrAmbiguousDirectives)
{
    mwmp::CrimeReactionDirective directive;
    directive.eventId = "crime-event-2";
    directive.cellId = "Balmora";
    directive.offenderGuid = 7;
    directive.actors.push_back({ 42, 1, mwmp::CrimeReactionDialogue::Thief, 0 });
    ASSERT_TRUE(mwmp::validateCrimeReactionDirective(directive));

    auto invalid = directive;
    invalid.actors[0].actorNetId = 0;
    EXPECT_FALSE(mwmp::validateCrimeReactionDirective(invalid));

    invalid = directive;
    invalid.actors[0].migrationGeneration = 0;
    EXPECT_FALSE(mwmp::validateCrimeReactionDirective(invalid));

    invalid = directive;
    invalid.actors[0].dialogue = static_cast<mwmp::CrimeReactionDialogue>(99);
    EXPECT_FALSE(mwmp::validateCrimeReactionDirective(invalid));

    invalid = directive;
    invalid.actors[0].dialogue = mwmp::CrimeReactionDialogue::None;
    invalid.actors[0].flags = mwmp::CrimeReactionPursueOffender;
    EXPECT_FALSE(mwmp::validateCrimeReactionDirective(invalid));

    invalid = directive;
    invalid.actors.push_back(invalid.actors.front());
    EXPECT_FALSE(mwmp::validateCrimeReactionDirective(invalid));

    auto clear = directive;
    clear.actors[0].dialogue = mwmp::CrimeReactionDialogue::None;
    clear.actors[0].flags = mwmp::CrimeReactionClearPursuit;
    EXPECT_TRUE(mwmp::validateCrimeReactionDirective(clear));

    auto combat = directive;
    combat.actors[0].dialogue = mwmp::CrimeReactionDialogue::None;
    combat.actors[0].flags = static_cast<std::uint8_t>(
        mwmp::CrimeReactionSetAlarmed | mwmp::CrimeReactionStartCombat);
    EXPECT_TRUE(mwmp::validateCrimeReactionDirective(combat));

    invalid = combat;
    invalid.offenderGuid = 0;
    EXPECT_FALSE(mwmp::validateCrimeReactionDirective(invalid));

    auto fight = combat;
    fight.actors[0].flags = static_cast<std::uint8_t>(fight.actors[0].flags
        | mwmp::CrimeReactionSetFight);
    fight.actors[0].fight = 100;
    EXPECT_TRUE(mwmp::validateCrimeReactionDirective(fight));

    invalid = fight;
    invalid.actors[0].fight = 101;
    EXPECT_FALSE(mwmp::validateCrimeReactionDirective(invalid));

    invalid = directive;
    invalid.actors[0].fight = 30;
    EXPECT_FALSE(mwmp::validateCrimeReactionDirective(invalid));

    invalid = combat;
    invalid.actors[0].flags = static_cast<std::uint8_t>(mwmp::CrimeReactionStartCombat);
    EXPECT_FALSE(mwmp::validateCrimeReactionDirective(invalid));

    invalid = clear;
    invalid.actors[0].flags = static_cast<std::uint8_t>(mwmp::CrimeReactionSetAlarmed
        | mwmp::CrimeReactionPursueOffender | mwmp::CrimeReactionClearPursuit);
    EXPECT_FALSE(mwmp::validateCrimeReactionDirective(invalid));

    invalid = combat;
    invalid.actors[0].flags = static_cast<std::uint8_t>(mwmp::CrimeReactionSetAlarmed
        | mwmp::CrimeReactionPursueOffender | mwmp::CrimeReactionStartCombat);
    EXPECT_FALSE(mwmp::validateCrimeReactionDirective(invalid));
}

TEST(CrimeReactionProtocol, RemoteOffenderTargetNeverUsesAuthorityPlayer)
{
    constexpr std::uint32_t marioAuthorityGuid = 11;
    constexpr std::uint32_t sithOffenderGuid = 22;

    EXPECT_EQ(mwmp::crimeReactionOffenderTargetId(sithOffenderGuid), "mp_remote_22");
    EXPECT_NE(mwmp::crimeReactionOffenderTargetId(sithOffenderGuid),
        mwmp::crimeReactionOffenderTargetId(marioAuthorityGuid));
    EXPECT_TRUE(mwmp::crimeReactionOffenderTargetId(0).empty());
}
