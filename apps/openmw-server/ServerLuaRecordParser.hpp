#ifndef OPENMW_SERVER_SERVERLUARECORDPARSER_HPP
#define OPENMW_SERVER_SERVERLUARECORDPARSER_HPP

#include <string_view>

#include <components/openmw-mp/Records/DynamicRecordTypes.hpp>

namespace mwmp
{
    /// Compatibility boundary for the historical server-Lua table format.
    /// New persistence and network traffic must use the returned typed DTO.
    records::DynamicRecordDefinition parseServerLuaRecord(
        std::string_view recordType, std::string_view serializedTable);

    bool isCanonicalServerLuaRecordType(std::string_view recordType);
}

#endif
