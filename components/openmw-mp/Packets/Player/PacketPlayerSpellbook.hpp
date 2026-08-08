#ifndef OPENMW_MP_PACKETPLAYERSPELLBOOK_HPP
#define OPENMW_MP_PACKETPLAYERSPELLBOOK_HPP

#include <components/openmw-mp/SpellbookSync.hpp>

#include "PlayerPacket.hpp"

namespace mwmp
{
    // -----------------------------------------------------------------------
    // PacketPlayerSpellbook — player learned-spellbook sync payload.
    //
    // Carries semantic spell record IDs (content IDs or catalog-known
    // generated IDs). Clients never send ESM::Spell definitions; the server
    // resolves every ID against its authoritative content registry and
    // dynamic record catalog.
    //
    // Action::Set    → replace the canonical learned set (client full-sync /
    //                   server authoritative restore and replies)
    // Action::Add    → add spell IDs (deduplicated server-side)
    // Action::Remove → remove spell IDs (no-op for absent spells)
    //
    // The revision field is an optimistic concurrency token: client
    // mutations carry the last acknowledged revision, the server increments
    // it on every accepted mutation and replies with the authoritative Set.
    //
    // Decoding is bounded and fails closed: spell count is capped, spell IDs
    // have a length cap, and any truncation or malformed field rejects the
    // whole packet.
    // -----------------------------------------------------------------------
    class PacketPlayerSpellbook : public PlayerPacket
    {
    public:
        PacketPlayerSpellbook() : PlayerPacket(PacketType::PlayerSpellbook) {}

    protected:
        void pack(WriteStream& ws) override
        {
            ws.write(mPlayer->guid);
            auto action = static_cast<uint8_t>(mPlayer->spellbookChanges.action);
            ws.write(action);
            ws.write(mPlayer->spellbookChanges.revision);
            auto count = static_cast<uint16_t>(mPlayer->spellbookChanges.spellIds.size());
            ws.write(count);
            for (const auto& spellId : mPlayer->spellbookChanges.spellIds)
                ws.writeString(spellId);
        }

        void unpack(ReadStream& rs) override
        {
            rs.read(mPlayer->guid);
            uint8_t action = 0;
            rs.read(action);
            if (action > static_cast<uint8_t>(BasePlayer::SpellbookChanges::Action::Remove))
                throw std::runtime_error("PacketPlayerSpellbook: invalid action");
            mPlayer->spellbookChanges.action = static_cast<BasePlayer::SpellbookChanges::Action>(action);
            rs.read(mPlayer->spellbookChanges.revision);
            uint16_t count = 0;
            rs.read(count);
            if (count > MAX_SPELLBOOK_SIZE)
                throw std::runtime_error("PacketPlayerSpellbook: oversized spell count");
            mPlayer->spellbookChanges.spellIds.clear();
            mPlayer->spellbookChanges.spellIds.reserve(count);
            for (uint16_t i = 0; i < count; ++i)
            {
                std::string spellId = rs.readString();
                if (spellId.size() > MAX_SPELL_ID_LENGTH)
                    throw std::runtime_error("PacketPlayerSpellbook: oversized spell id");
                mPlayer->spellbookChanges.spellIds.push_back(std::move(spellId));
            }
        }
    };

} // namespace mwmp

#endif // OPENMW_MP_PACKETPLAYERSPELLBOOK_HPP
