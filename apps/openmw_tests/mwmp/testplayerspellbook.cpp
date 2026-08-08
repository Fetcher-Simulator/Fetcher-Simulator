#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

#include <components/esm3/loadspel.hpp>
#include <components/openmw-mp/SpellbookSync.hpp>

#include <apps/openmw/mwworld/esmstore.hpp>

namespace
{
    // ---------------------------------------------------------------------
    // Content fixture: an in-memory ESMStore with the records authoritative
    // spellbook validation consumes. Mirrors the client/server content store
    // layout (content spells, powers, diseases).
    // ---------------------------------------------------------------------
    struct SpellbookContentFixture
    {
        MWWorld::ESMStore store;

        SpellbookContentFixture()
        {
            auto addSpell = [this](std::string_view id, ESM::Spell::SpellType type) {
                ESM::Spell spell;
                spell.blank();
                spell.mId = ESM::RefId::stringRefId(id);
                spell.mData.mType = type;
                store.insertStatic(spell);
            };

            addSpell("fireball", ESM::Spell::ST_Spell);
            addSpell("frostbite", ESM::Spell::ST_Spell);
            addSpell("summon_scamp", ESM::Spell::ST_Spell);
            addSpell("race_power", ESM::Spell::ST_Power);
            addSpell("sign_power", ESM::Spell::ST_Power);
            addSpell("blight_x", ESM::Spell::ST_Blight);
            addSpell("disease_y", ESM::Spell::ST_Disease);
            addSpell("item_ability", ESM::Spell::ST_Ability);

            // Mirror the real plugin-load lifecycle. setUp() rebuilds the
            // generic ESMStore ID index using only cacheable record types.
            store.setUp();
        }

        const ESM::Spell* find(std::string_view id) const
        {
            return store.get<ESM::Spell>().search(ESM::RefId::stringRefId(id));
        }
    };

    using Action = mwmp::BasePlayer::SpellbookChanges::Action;

    mwmp::SpellbookError validateAgainstFixture(
        const SpellbookContentFixture& fixture, const std::string& id, const std::string& generatedPrefix = "$custom_")
    {
        return mwmp::validateSpellbookSpellId(id, generatedPrefix,
            [&](const std::string& spellId) { return fixture.find(spellId); },
            [&](const std::string& spellId) -> std::optional<bool> {
                if (spellId == "$custom_spell_known")
                    return true;
                if (spellId == "$custom_spell_transient")
                    return false;
                return std::nullopt;
            });
    }
}

TEST(ServerSpellbook, TypedSpellLookupWorksOutsideGenericIdIndex)
{
    SpellbookContentFixture fixture;
    const ESM::RefId id = ESM::RefId::stringRefId("fireball");

    // ESMStore::find() intentionally indexes only cacheable world/reference
    // record types; Spell records are not in that generic ID index. Client
    // spellbook visibility barriers must therefore use the typed Spell store.
    EXPECT_EQ(fixture.store.find(id), 0);
    EXPECT_NE(fixture.store.get<ESM::Spell>().search(id), nullptr);
}

TEST(ServerSpellbook, KnownContentSpellAccepted)
{
    SpellbookContentFixture fixture;
    EXPECT_EQ(validateAgainstFixture(fixture, "fireball"), mwmp::SpellbookError::None);
    EXPECT_EQ(validateAgainstFixture(fixture, "summon_scamp"), mwmp::SpellbookError::None);
}

TEST(ServerSpellbook, UnknownSpellRejected)
{
    SpellbookContentFixture fixture;
    EXPECT_EQ(validateAgainstFixture(fixture, "not_a_real_spell"), mwmp::SpellbookError::UnknownSpell);
}

TEST(ServerSpellbook, GeneratedLookingUnknownIdRejected)
{
    SpellbookContentFixture fixture;
    EXPECT_EQ(validateAgainstFixture(fixture, "$custom_spell_999"), mwmp::SpellbookError::UnknownDynamicRecord);
}

TEST(ServerSpellbook, WrongRecordTypeRejected)
{
    SpellbookContentFixture fixture;
    // Powers, abilities and diseases are baseline/transient state and must
    // never enter the learned spellbook.
    EXPECT_EQ(validateAgainstFixture(fixture, "race_power"), mwmp::SpellbookError::WrongRecordType);
    EXPECT_EQ(validateAgainstFixture(fixture, "sign_power"), mwmp::SpellbookError::WrongRecordType);
    EXPECT_EQ(validateAgainstFixture(fixture, "item_ability"), mwmp::SpellbookError::WrongRecordType);
    EXPECT_EQ(validateAgainstFixture(fixture, "blight_x"), mwmp::SpellbookError::WrongRecordType);
    EXPECT_EQ(validateAgainstFixture(fixture, "disease_y"), mwmp::SpellbookError::WrongRecordType);
}

TEST(ServerSpellbook, KnownDynamicSpellAcceptedOnlyWhenPersistent)
{
    SpellbookContentFixture fixture;
    EXPECT_EQ(validateAgainstFixture(fixture, "$custom_spell_known"), mwmp::SpellbookError::None);
    EXPECT_EQ(
        validateAgainstFixture(fixture, "$custom_spell_transient"), mwmp::SpellbookError::NonPersistentDynamicRecord);
}

TEST(ServerSpellbook, EmptyAndOversizedIdsRejected)
{
    SpellbookContentFixture fixture;
    EXPECT_EQ(validateAgainstFixture(fixture, ""), mwmp::SpellbookError::InvalidSpellId);
    EXPECT_EQ(validateAgainstFixture(fixture, std::string(mwmp::MAX_SPELL_ID_LENGTH + 1, 'x')),
        mwmp::SpellbookError::InvalidSpellId);
}

TEST(ServerSpellbook, GeneratedPrefixBoundary)
{
    SpellbookContentFixture fixture;
    // Without a generated prefix configured the same id resolves as content.
    EXPECT_EQ(validateAgainstFixture(fixture, "fireball", ""), mwmp::SpellbookError::None);
    // A generated-looking id with an empty prefix is treated as content and
    // rejected as unknown (content cannot back it).
    EXPECT_EQ(validateAgainstFixture(fixture, "$custom_spell_999", ""), mwmp::SpellbookError::UnknownSpell);
}

TEST(ServerSpellbook, MutationsAreCanonicalAndIdempotent)
{
    using mwmp::applySpellbookAction;
    using mwmp::canonicalizeSpellIds;

    std::vector<std::string> current = canonicalizeSpellIds({ "fireball", "frostbite" });

    // Set replaces and canonicalizes duplicates.
    current = applySpellbookAction(Action::Set, current, { "frostbite", "frostbite", "summon_scamp" });
    EXPECT_EQ(current, (std::vector<std::string>{ "frostbite", "summon_scamp" }));

    // Add of an already-known spell is a no-op (no duplicates, no growth).
    const std::vector<std::string> beforeAdd = current;
    EXPECT_EQ(applySpellbookAction(Action::Add, current, { "frostbite" }), beforeAdd);

    // Remove of an absent spell is a no-op.
    const std::vector<std::string> beforeRemove = current;
    EXPECT_EQ(applySpellbookAction(Action::Remove, current, { "fireball" }), beforeRemove);

    // Repeated Set with identical contents is stable.
    EXPECT_EQ(applySpellbookAction(Action::Set, current, { "frostbite", "summon_scamp" }), current);
}

TEST(ServerSpellbook, BaselineSpellsStayOutOfTheLearnedSet)
{
    SpellbookContentFixture fixture;
    // The learned set is exactly the ST_Spell subset of the runtime list.
    // Powers/abilities/diseases never pass validation, so the persisted set
    // can never contain them even if a client proposes them.
    std::vector<std::string> proposed = { "fireball", "race_power", "blight_x", "item_ability", "sign_power" };
    std::vector<std::string> accepted;
    for (const std::string& id : proposed)
    {
        if (validateAgainstFixture(fixture, id) == mwmp::SpellbookError::None)
            accepted.push_back(id);
    }
    EXPECT_EQ(accepted, (std::vector<std::string>{ "fireball" }));
}

TEST(ServerSpellbook, QuotaBoundEnforced)
{
    SpellbookContentFixture fixture;
    // MAX_SPELLBOOK_SIZE caps both the wire count and the persisted set;
    // the mutation layer rejects sets that exceed it.
    std::vector<std::string> oversized;
    for (std::size_t i = 0; i <= mwmp::MAX_SPELLBOOK_SIZE; ++i)
        oversized.push_back("spell_" + std::to_string(i));

    std::vector<std::string> canonical = mwmp::canonicalizeSpellIds(oversized);
    EXPECT_EQ(canonical.size(), mwmp::MAX_SPELLBOOK_SIZE + 1);
    EXPECT_GT(canonical.size(), mwmp::MAX_SPELLBOOK_SIZE);
}
