#ifndef OPENMW_MP_DYNAMIC_RECORD_CODEC_HPP
#define OPENMW_MP_DYNAMIC_RECORD_CODEC_HPP

#include <string>
#include <string_view>

#include "DynamicRecordTypes.hpp"

namespace mwmp::records
{
    std::string encodeDefinition(const DynamicRecordDefinition& definition);
    DynamicRecordDefinition decodeDefinition(std::string_view bytes);

    std::string encodeBundle(const DynamicRecordBundle& bundle);
    DynamicRecordBundle decodeBundle(std::string_view bytes);
}

#endif
