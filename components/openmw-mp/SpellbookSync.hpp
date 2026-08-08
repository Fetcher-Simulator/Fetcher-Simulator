#ifndef OPENMW_COMPONENTS_OPENMW_MP_SPELLBOOKSYNC_HPP
#define OPENMW_COMPONENTS_OPENMW_MP_SPELLBOOKSYNC_HPP

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <components/esm3/loadspel.hpp>

#include <components/openmw-mp/Base/BasePlayer.hpp>

namespace mwmp
{
    // -----------------------------------------------------------------------
    // Spellbook sync shared constants and pure helpers.
    //
    // The learned spellbook is the set of ESM::Spell records with type
    // ST_Spell known by a character. Content-derived baseline spells (race /
    // birthsign powers, abilities, diseases, starter spells) are deliberately
    // excluded: they are reconstructed from authoritative content on login.
    // -----------------------------------------------------------------------

    inline constexpr std::size_t MAX_SPELLBOOK_SIZE = 1024;
    inline constexpr std::size_t MAX_SPELL_ID_LENGTH = 128;

    // Stable machine-readable rejection reasons. These mirror the naming
    // conventions used by the existing player sync handlers.
    enum class SpellbookError
    {
        None = 0,
        MalformedRequest,
        InvalidAction,
        TooManySpells,
        InvalidSpellId,
        UnknownSpell,
        WrongRecordType,
        UnknownDynamicRecord,
        NonPersistentDynamicRecord,
        StaleSpellbookRevision,
        ServerError,
    };

    inline std::string_view spellbookErrorName(SpellbookError error)
    {
        switch (error)
        {
            case SpellbookError::None: return "none";
            case SpellbookError::MalformedRequest: return "malformed_request";
            case SpellbookError::InvalidAction: return "invalid_action";
            case SpellbookError::TooManySpells: return "too_many_spells";
            case SpellbookError::InvalidSpellId: return "invalid_spell_id";
            case SpellbookError::UnknownSpell: return "unknown_spell";
            case SpellbookError::WrongRecordType: return "wrong_record_type";
            case SpellbookError::UnknownDynamicRecord: return "unknown_dynamic_record";
            case SpellbookError::NonPersistentDynamicRecord: return "nonpersistent_dynamic_record";
            case SpellbookError::StaleSpellbookRevision: return "stale_spellbook_revision";
            case SpellbookError::ServerError: return "server_error";
        }
        return "unknown";
    }

    /// Is this record part of the learned spellbook?
    /// Powers / abilities / diseases / curses are baseline or transient state
    /// and are never synchronised or persisted as learned spells.
    inline bool isLearnedSpell(const ESM::Spell* spell)
    {
        return spell != nullptr && spell->mData.mType == ESM::Spell::ST_Spell;
    }

    /// Deduplicate and sort. Deterministic canonical form for persistence and
    /// comparison (the set is unordered semantically; ordering is only for
    /// stable wire/DB output).
    inline std::vector<std::string> canonicalizeSpellIds(std::vector<std::string> ids)
    {
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        return ids;
    }

    /// Apply a client-proposed mutation to a canonical set. Always returns a
    /// canonical set. Adding an already-known spell is a no-op; removing an
    /// absent spell is a no-op; Set replaces wholesale.
    inline std::vector<std::string> applySpellbookAction(
        BasePlayer::SpellbookChanges::Action action, std::vector<std::string> current,
        const std::vector<std::string>& incoming)
    {
        using Action = BasePlayer::SpellbookChanges::Action;
        if (action == Action::Set)
            return canonicalizeSpellIds(incoming);

        if (action == Action::Add)
        {
            current.insert(current.end(), incoming.begin(), incoming.end());
            return canonicalizeSpellIds(std::move(current));
        }

        if (action == Action::Remove)
        {
            current.erase(std::remove_if(current.begin(), current.end(), [&](const std::string& id) {
                return std::find(incoming.begin(), incoming.end(), id) != incoming.end();
            }), current.end());
            return current;
        }

        return current;
    }

    /// Validate one proposed spell ID against the authoritative content
    /// registry and the dynamic record catalog.
    ///
    /// contentLookup(id) must return the content ESM::Spell record or nullptr.
    /// dynamicLookup(id) must return std::nullopt when the id is not a known
    /// dynamic record of type "spell", otherwise the record's persistent flag.
    ///
    /// Generated-looking IDs are only ever accepted through the dynamic
    /// catalog; content records never back a generated-prefix ID.
    template <typename ContentLookup, typename DynamicLookup>
    inline SpellbookError validateSpellbookSpellId(
        const std::string& id, std::string_view generatedPrefix, ContentLookup&& contentLookup,
        DynamicLookup&& dynamicLookup)
    {
        if (id.empty())
            return SpellbookError::InvalidSpellId;
        if (id.size() > MAX_SPELL_ID_LENGTH)
            return SpellbookError::InvalidSpellId;

        const bool generatedLookup = !generatedPrefix.empty() && id.starts_with(generatedPrefix);
        if (generatedLookup)
        {
            const std::optional<bool> dynamic = dynamicLookup(id);
            if (!dynamic)
                return SpellbookError::UnknownDynamicRecord;
            if (!*dynamic)
                return SpellbookError::NonPersistentDynamicRecord;
            return SpellbookError::None;
        }

        const ESM::Spell* spell = contentLookup(id);
        if (spell == nullptr)
            return SpellbookError::UnknownSpell;
        if (!isLearnedSpell(spell))
            return SpellbookError::WrongRecordType;
        return SpellbookError::None;
    }

    /// Client-side optimistic concurrency gate for spellbook mutations,
    /// mirroring InventoryRevisionGate.
    class SpellbookRevisionGate
    {
    public:
        bool canSend() const { return !mInFlight; }
        void markSent() { mInFlight = true; }

        bool observeAuthoritative()
        {
            if (!mInFlight)
                return false;
            mInFlight = false;
            return true;
        }

        void reset() { mInFlight = false; }

    private:
        bool mInFlight = false;
    };
}

#endif // OPENMW_COMPONENTS_OPENMW_MP_SPELLBOOKSYNC_HPP
