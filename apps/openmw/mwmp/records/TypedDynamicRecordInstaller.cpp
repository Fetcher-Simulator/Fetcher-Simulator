#include "TypedDynamicRecordInstaller.hpp"

#include <stdexcept>
#include <type_traits>
#include <variant>

#include <components/esm3/loaddial.hpp>
#include <components/esm3/loadscpt.hpp>
#include <components/openmw-mp/Records/EsmDynamicRecordConversion.hpp>

#include "../../mwworld/esmstore.hpp"

namespace mwmp
{
    void installTypedDynamicRecord(MWWorld::ESMStore& store, std::string_view recordId,
        const records::DynamicRecordDefinition& definition, bool gameplayRunning,
        const ScriptCacheInvalidator& invalidateScript)
    {
        records::EsmDynamicRecord esm = records::toEsmRecord(definition);
        std::visit(
            [&](auto& record) {
                using Record = std::decay_t<decltype(record)>;
                record.mId = ESM::RefId::stringRefId(recordId);
                if constexpr (std::is_same_v<Record, ESM::Dialogue>)
                {
                    if (record.mStringId.empty())
                        record.mStringId = std::string(recordId);
                }

                const auto& typedStore = store.get<Record>();
                bool overrideOnly = false;
                switch (definition.authoringMode)
                {
                    case records::AuthoringMode::Generated:
                        if constexpr (std::is_same_v<Record, ESM::Dialogue> || std::is_same_v<Record, ESM::Script>)
                            throw std::runtime_error("typed_server_content_requires_explicit_mode");
                        break;
                    case records::AuthoringMode::New:
                        if (typedStore.searchStatic(record.mId) != nullptr)
                            throw std::runtime_error("new_record_collides_with_static_content");
                        break;
                    case records::AuthoringMode::Override:
                        if constexpr (!std::is_same_v<Record, ESM::Dialogue> && !std::is_same_v<Record, ESM::Script>)
                            throw std::runtime_error("static_override_type_not_supported");
                        if (gameplayRunning)
                            throw std::runtime_error("static_override_requires_bootstrap");
                        if (typedStore.searchStatic(record.mId) == nullptr)
                            throw std::runtime_error("override_record_has_no_static_base");
                        overrideOnly = true;
                        break;
                }

                if (store.overrideRecord(record, overrideOnly) == nullptr)
                    throw std::runtime_error("dynamic_record_install_failed");
                if constexpr (std::is_same_v<Record, ESM::Script>)
                {
                    if (!invalidateScript)
                        throw std::runtime_error("script_cache_invalidator_missing");
                    invalidateScript(record.mId);
                }
            },
            esm);
    }
}
