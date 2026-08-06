#ifndef OPENMW_MP_DYNAMIC_RECORD_FINGERPRINT_HPP
#define OPENMW_MP_DYNAMIC_RECORD_FINGERPRINT_HPP

#include <string>

#include "DynamicRecordTypes.hpp"

namespace mwmp::records
{
    std::string fingerprint(const DynamicRecordDefinition& definition);
}

#endif
