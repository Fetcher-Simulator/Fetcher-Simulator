#include "DynamicRecordFingerprint.hpp"

#include <components/openmw-mp/Sha256.hpp>

#include "DynamicRecordCodec.hpp"
#include "DynamicRecordValidation.hpp"

namespace mwmp::records
{
    std::string fingerprint(const DynamicRecordDefinition& definition)
    {
        return crypto::sha256hex(encodeDefinition(canonicalize(definition)));
    }
}
