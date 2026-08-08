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
    inline constexpr std::uint16_t ContentManifestVersion = 3;
    inline constexpr std::uint16_t ContentApiVersion = 1;
    inline constexpr std::uint16_t RuntimeRecordCapabilityManifestVersion = 1;
}

#endif
