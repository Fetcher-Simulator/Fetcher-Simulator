#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include <components/esm3/loadspel.hpp>
#include <components/openmw-mp/Base/BasePlayer.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerSpellbook.hpp>
#include <components/openmw-mp/Packets/System/PacketHandshake.hpp>
#include <components/openmw-mp/SpellbookSync.hpp>

namespace
{
    using Action = mwmp::BasePlayer::SpellbookChanges::Action;

    std::vector<uint8_t> craftWire(
        uint8_t action, uint64_t revision, const std::vector<std::string>& spells, uint16_t countOverride = 0)
    {
        std::vector<uint8_t> payload;
        const auto appendPayload = [&](const void* data, size_t size) {
            const auto* bytes = static_cast<const uint8_t*>(data);
            payload.insert(payload.end(), bytes, bytes + size);
        };

        const uint32_t guid = 7;
        appendPayload(&guid, sizeof(guid));
        appendPayload(&action, sizeof(action));
        appendPayload(&revision, sizeof(revision));
        const uint16_t count = countOverride != 0 ? countOverride : static_cast<uint16_t>(spells.size());
        appendPayload(&count, sizeof(count));
        for (const std::string& spell : spells)
        {
            const uint16_t len = static_cast<uint16_t>(spell.size());
            appendPayload(&len, sizeof(len));
            appendPayload(spell.data(), spell.size());
        }

        std::vector<uint8_t> wire;
        const auto appendWire = [&](const void* data, size_t size) {
            const auto* bytes = static_cast<const uint8_t*>(data);
            wire.insert(wire.end(), bytes, bytes + size);
        };
        const uint16_t type = static_cast<uint16_t>(mwmp::PacketType::PlayerSpellbook);
        const uint32_t payloadSize = static_cast<uint32_t>(payload.size());
        const uint32_t sequence = 0;
        appendWire(&type, sizeof(type));
        appendWire(&payloadSize, sizeof(payloadSize));
        appendWire(&sequence, sizeof(sequence));
        wire.insert(wire.end(), payload.begin(), payload.end());
        return wire;
    }

    void expectRoundTrip(Action action, uint64_t revision, const std::vector<std::string>& spells)
    {
        mwmp::BasePlayer outgoing;
        outgoing.guid = 42;
        outgoing.spellbookChanges.action = action;
        outgoing.spellbookChanges.revision = revision;
        outgoing.spellbookChanges.spellIds = spells;

        mwmp::PacketPlayerSpellbook encoder;
        encoder.setPlayer(&outgoing);
        const std::vector<uint8_t> bytes = encoder.encode(7);

        mwmp::BasePlayer incoming;
        mwmp::PacketPlayerSpellbook decoder;
        decoder.setPlayer(&incoming);
        ASSERT_TRUE(decoder.decode(bytes));
        EXPECT_EQ(incoming.guid, 42u);
        EXPECT_EQ(incoming.spellbookChanges.action, action);
        EXPECT_EQ(incoming.spellbookChanges.revision, revision);
        EXPECT_EQ(incoming.spellbookChanges.spellIds, spells);
    }
}

TEST(CharacterDataPacket, RoundTripsSavedSpellbookFlag)
{
    mwmp::PacketCharacterData outgoing;
    outgoing.isNewCharacter = false;
    outgoing.characterId = 17;
    outgoing.characterName = "ass";
    outgoing.hasSavedSpellbook = true;

    const std::vector<uint8_t> bytes = outgoing.encode(9);

    mwmp::PacketCharacterData incoming;
    ASSERT_TRUE(incoming.decode(bytes));
    EXPECT_FALSE(incoming.isNewCharacter);
    EXPECT_EQ(incoming.characterId, 17);
    EXPECT_EQ(incoming.characterName, "ass");
    EXPECT_TRUE(incoming.hasSavedSpellbook);
}

TEST(PlayerSpellbookPacket, RoundTripsSet)
{
    expectRoundTrip(Action::Set, 3, { "fireball", "frostbite", "summon_scamp" });
}

TEST(PlayerSpellbookPacket, RoundTripsAdd)
{
    expectRoundTrip(Action::Add, 4, { "fireball" });
}

TEST(PlayerSpellbookPacket, RoundTripsRemove)
{
    expectRoundTrip(Action::Remove, 5, { "summon_scamp" });
}

TEST(PlayerSpellbookPacket, RoundTripsEmptySet)
{
    expectRoundTrip(Action::Set, 0, {});
}

TEST(PlayerSpellbookPacket, RoundTripsMultipleIds)
{
    std::vector<std::string> spells;
    for (int i = 0; i < 64; ++i)
        spells.push_back("spell_" + std::to_string(i));
    expectRoundTrip(Action::Set, 9, spells);
}

TEST(PlayerSpellbookPacket, RejectsTruncatedPayload)
{
    mwmp::BasePlayer outgoing;
    outgoing.guid = 1;
    outgoing.spellbookChanges.action = Action::Set;
    outgoing.spellbookChanges.spellIds = { "fireball", "frostbite" };

    mwmp::PacketPlayerSpellbook encoder;
    encoder.setPlayer(&outgoing);
    std::vector<uint8_t> bytes = encoder.encode(1);
    bytes.resize(bytes.size() - 3); // cut into the second spell id

    mwmp::BasePlayer incoming;
    mwmp::PacketPlayerSpellbook decoder;
    decoder.setPlayer(&incoming);
    EXPECT_FALSE(decoder.decode(bytes));
}

TEST(PlayerSpellbookPacket, RejectsOversizedSpellCount)
{
    const std::vector<uint8_t> bytes = craftWire(static_cast<uint8_t>(Action::Set), 0, {}, 2000);

    mwmp::BasePlayer incoming;
    mwmp::PacketPlayerSpellbook decoder;
    decoder.setPlayer(&incoming);
    EXPECT_FALSE(decoder.decode(bytes));
}

TEST(PlayerSpellbookPacket, RejectsOversizedSpellId)
{
    const std::string huge(mwmp::MAX_SPELL_ID_LENGTH + 1, 'x');
    const std::vector<uint8_t> bytes = craftWire(static_cast<uint8_t>(Action::Add), 0, { huge });

    mwmp::BasePlayer incoming;
    mwmp::PacketPlayerSpellbook decoder;
    decoder.setPlayer(&incoming);
    EXPECT_FALSE(decoder.decode(bytes));
}

TEST(PlayerSpellbookPacket, RejectsInvalidAction)
{
    const std::vector<uint8_t> bytes = craftWire(3, 0, { "fireball" });

    mwmp::BasePlayer incoming;
    mwmp::PacketPlayerSpellbook decoder;
    decoder.setPlayer(&incoming);
    EXPECT_FALSE(decoder.decode(bytes));
}

TEST(PlayerSpellbookPacket, ToleratesDuplicateIds)
{
    // Duplicate IDs are legal wire data; canonicalization happens server-side.
    expectRoundTrip(Action::Set, 2, { "fireball", "fireball", "frostbite" });
}

TEST(PlayerSpellbookPacket, ToleratesTrailingBytes)
{
    // The project convention is lenient decoding: trailing bytes after the
    // declared fields are ignored (no packet rejects them today).
    mwmp::BasePlayer outgoing;
    outgoing.guid = 42;
    outgoing.spellbookChanges.action = Action::Set;
    outgoing.spellbookChanges.spellIds = { "fireball" };

    mwmp::PacketPlayerSpellbook encoder;
    encoder.setPlayer(&outgoing);
    std::vector<uint8_t> bytes = encoder.encode(7);
    bytes.push_back(0xAA);
    bytes.push_back(0xBB);

    mwmp::BasePlayer incoming;
    mwmp::PacketPlayerSpellbook decoder;
    decoder.setPlayer(&incoming);
    EXPECT_TRUE(decoder.decode(bytes));
    EXPECT_EQ(incoming.spellbookChanges.spellIds, (std::vector<std::string>{ "fireball" }));
}

TEST(SpellbookSync, CanonicalizesDeduplicatesSorts)
{
    EXPECT_EQ(mwmp::canonicalizeSpellIds({ "frostbite", "fireball", "fireball", "frostbite", "summon_scamp" }),
        (std::vector<std::string>{ "fireball", "frostbite", "summon_scamp" }));
    EXPECT_TRUE(mwmp::canonicalizeSpellIds({}).empty());
}

TEST(SpellbookSync, AppliesSetAddRemove)
{
    using mwmp::applySpellbookAction;

    const std::vector<std::string> current = mwmp::canonicalizeSpellIds({ "fireball", "frostbite" });

    // Set replaces wholesale and canonicalizes duplicates.
    EXPECT_EQ(applySpellbookAction(Action::Set, current, { "frostbite", "frostbite", "summon_scamp" }),
        (std::vector<std::string>{ "frostbite", "summon_scamp" }));

    // Add is a union; adding an already-known spell is a no-op.
    EXPECT_EQ(applySpellbookAction(Action::Add, current, { "summon_scamp" }),
        (std::vector<std::string>{ "fireball", "frostbite", "summon_scamp" }));
    EXPECT_EQ(applySpellbookAction(Action::Add, current, { "fireball" }), current);

    // Remove deletes exactly the listed ids; removing an absent spell is a no-op.
    EXPECT_EQ(applySpellbookAction(Action::Remove, current, { "fireball" }),
        (std::vector<std::string>{ "frostbite" }));
    EXPECT_EQ(applySpellbookAction(Action::Remove, current, { "summon_scamp" }), current);
    EXPECT_EQ(applySpellbookAction(Action::Remove, current, {}), current);
}

TEST(SpellbookSync, LearnedFilterAcceptsOnlySpells)
{
    auto makeSpell = [](ESM::RefId id, int type) {
        ESM::Spell spell;
        spell.blank();
        spell.mId = std::move(id);
        spell.mData.mType = type;
        return spell;
    };

    const ESM::Spell spell = makeSpell(ESM::RefId::stringRefId("fireball"), ESM::Spell::ST_Spell);
    const ESM::Spell power = makeSpell(ESM::RefId::stringRefId("race_power"), ESM::Spell::ST_Power);
    const ESM::Spell ability = makeSpell(ESM::RefId::stringRefId("item_ability"), ESM::Spell::ST_Ability);
    const ESM::Spell disease = makeSpell(ESM::RefId::stringRefId("blight_x"), ESM::Spell::ST_Blight);

    EXPECT_TRUE(mwmp::isLearnedSpell(&spell));
    EXPECT_FALSE(mwmp::isLearnedSpell(&power));
    EXPECT_FALSE(mwmp::isLearnedSpell(&ability));
    EXPECT_FALSE(mwmp::isLearnedSpell(&disease));
    EXPECT_FALSE(mwmp::isLearnedSpell(nullptr));
}

TEST(SpellbookSync, ValidatesSpellIds)
{
    using mwmp::SpellbookError;
    using mwmp::validateSpellbookSpellId;

    const std::string generatedPrefix = "$custom_";

    ESM::Spell fireball;
    fireball.blank();
    fireball.mId = ESM::RefId::stringRefId("fireball");
    fireball.mData.mType = ESM::Spell::ST_Spell;

    ESM::Spell racePower;
    racePower.blank();
    racePower.mId = ESM::RefId::stringRefId("race_power");
    racePower.mData.mType = ESM::Spell::ST_Power;

    const auto contentLookup = [&](const std::string& id) -> const ESM::Spell* {
        if (id == "fireball")
            return &fireball;
        if (id == "race_power")
            return &racePower;
        return nullptr;
    };
    const auto dynamicLookup = [&](const std::string& id) -> std::optional<bool> {
        if (id == "$custom_spell_1")
            return true;
        if (id == "$custom_spell_2")
            return false;
        return std::nullopt;
    };

    EXPECT_EQ(validateSpellbookSpellId("fireball", generatedPrefix, contentLookup, dynamicLookup),
        SpellbookError::None);
    EXPECT_EQ(validateSpellbookSpellId("unknown_spell", generatedPrefix, contentLookup, dynamicLookup),
        SpellbookError::UnknownSpell);
    EXPECT_EQ(validateSpellbookSpellId("race_power", generatedPrefix, contentLookup, dynamicLookup),
        SpellbookError::WrongRecordType);
    EXPECT_EQ(validateSpellbookSpellId("", generatedPrefix, contentLookup, dynamicLookup),
        SpellbookError::InvalidSpellId);
    EXPECT_EQ(validateSpellbookSpellId(std::string(mwmp::MAX_SPELL_ID_LENGTH + 1, 'x'), generatedPrefix,
                  contentLookup, dynamicLookup),
        SpellbookError::InvalidSpellId);

    // Generated-looking IDs only resolve through the dynamic catalog.
    EXPECT_EQ(validateSpellbookSpellId("$custom_spell_1", generatedPrefix, contentLookup, dynamicLookup),
        SpellbookError::None);
    EXPECT_EQ(validateSpellbookSpellId("$custom_spell_2", generatedPrefix, contentLookup, dynamicLookup),
        SpellbookError::NonPersistentDynamicRecord);
    EXPECT_EQ(validateSpellbookSpellId("$custom_spell_999", generatedPrefix, contentLookup, dynamicLookup),
        SpellbookError::UnknownDynamicRecord);

    // A content record can never back a generated-looking ID even if the
    // content lookup would answer.
    const auto greedyContentLookup = [&](const std::string&) -> const ESM::Spell* { return &fireball; };
    EXPECT_EQ(validateSpellbookSpellId("$custom_spell_999", generatedPrefix, greedyContentLookup, dynamicLookup),
        SpellbookError::UnknownDynamicRecord);
}
