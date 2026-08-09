#ifndef OPENMW_MP_DYNAMIC_RECORD_DEPENDENCIES_HPP
#define OPENMW_MP_DYNAMIC_RECORD_DEPENDENCIES_HPP

#include <string>
#include <vector>

#include "DynamicRecordTypes.hpp"

namespace mwmp::records
{
    /// Extract references whose semantics are known to the typed DTO layer.
    /// Arbitrary references embedded in MWScript source/result scripts are not
    /// guessed; callers may add explicit dependency edges for those.
    std::vector<std::string> extractContentDependencies(const DynamicRecordDefinition& definition);
}

#endif
