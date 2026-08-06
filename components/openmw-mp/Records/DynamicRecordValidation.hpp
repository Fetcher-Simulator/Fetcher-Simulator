#ifndef OPENMW_MP_DYNAMIC_RECORD_VALIDATION_HPP
#define OPENMW_MP_DYNAMIC_RECORD_VALIDATION_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "DynamicRecordTypes.hpp"

namespace mwmp::records
{
    struct ValidationLimits
    {
        std::size_t maxRecordsPerBundle = 8;
        std::size_t maxDependenciesPerBundle = 16;
        std::size_t maxEffectsPerRecord = 64;
        std::size_t maxBodyPartsPerRecord = 64;
        std::size_t maxIdLength = 255;
        std::size_t maxNameLength = 1024;
        std::size_t maxTextLength = 1 << 20;
        std::size_t maxPathLength = 1024;
    };

    struct ValidationError
    {
        std::string code;
        std::string path;
        std::string message;
        bool operator==(const ValidationError&) const = default;
    };

    std::vector<ValidationError> validate(
        const DynamicRecordDefinition& definition, const ValidationLimits& limits = {});
    std::vector<ValidationError> validate(const DynamicRecordBundle& bundle, const ValidationLimits& limits = {});

    DynamicRecordDefinition canonicalize(DynamicRecordDefinition definition);
    DynamicRecordBundle canonicalize(DynamicRecordBundle bundle);
}

#endif
