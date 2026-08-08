#ifndef OPENMW_MP_CONTENT_MANIFEST_HPP
#define OPENMW_MP_CONTENT_MANIFEST_HPP

#include <cstdint>

namespace mwmp
{
    // Bump these independently when their canonical inputs or negotiation
    // semantics change. They are carried explicitly in the handshake so a
    // mismatch is diagnosable without relying on the broad transport version.
    // ContentManifestVersion 2 adds Ingredient, Apparatus, MagicEffect, and
    // GameSetting to the canonical resolved-content identity.
    // ContentManifestVersion 3 adds Skill and Class, which feed the
    // server-authoritative alchemy skill progression.
    // ContentManifestVersion 4 adds Creature and NPC, which feed the
    // server-authoritative enchanting charge and paid-enchanter pricing.
    inline constexpr std::uint16_t ContentManifestVersion = 4;
    inline constexpr std::uint16_t ContentApiVersion = 1;
    inline constexpr std::uint16_t RuntimeRecordCapabilityManifestVersion = 1;
}

#endif
