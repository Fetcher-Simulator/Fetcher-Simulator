#ifndef OPENMW_MWMP_TYPEDDYNAMICRECORDINSTALLER_HPP
#define OPENMW_MWMP_TYPEDDYNAMICRECORDINSTALLER_HPP

#include <functional>
#include <string_view>

#include <components/esm/refid.hpp>
#include <components/openmw-mp/Records/DynamicRecordTypes.hpp>

namespace MWWorld
{
    class ESMStore;
}

namespace mwmp
{
    using ScriptCacheInvalidator = std::function<void(const ESM::RefId&)>;

    void installTypedDynamicRecord(MWWorld::ESMStore& store, std::string_view recordId,
        const records::DynamicRecordDefinition& definition, bool gameplayRunning,
        const ScriptCacheInvalidator& invalidateScript);
}

#endif
