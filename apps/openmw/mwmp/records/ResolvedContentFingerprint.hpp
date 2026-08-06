#ifndef OPENMW_MWMP_RESOLVED_CONTENT_FINGERPRINT_HPP
#define OPENMW_MWMP_RESOLVED_CONTENT_FINGERPRINT_HPP

#include <string>

namespace MWWorld
{
    class ESMStore;
}

namespace MWMP
{
    // Fingerprints the final, post-load-script view of the record types that
    // can participate in the runtime-record protocol. Record IDs are part of
    // the digest, so load-time insertion, replacement, and deletion differ.
    std::string resolvedContentFingerprint(const MWWorld::ESMStore& store);
}

#endif
