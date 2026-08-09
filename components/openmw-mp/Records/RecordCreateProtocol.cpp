#include "RecordCreateProtocol.hpp"

namespace mwmp::records
{
    std::string_view getCreateErrorCode(CreateError error)
    {
        switch (error)
        {
            case CreateError::None:
                return "none";
            case CreateError::InvalidRequest:
                return "invalid_request";
            case CreateError::UnsupportedProtocol:
                return "unsupported_protocol";
            case CreateError::UnsupportedSchema:
                return "unsupported_schema";
            case CreateError::Unauthorized:
                return "unauthorized";
            case CreateError::RecordTypeNotPermitted:
                return "record_type_not_permitted";
            case CreateError::InvalidScriptIdentity:
                return "invalid_script_identity";
            case CreateError::InvalidDefinition:
                return "invalid_definition";
            case CreateError::InvalidDependency:
                return "invalid_dependency";
            case CreateError::InvalidAsset:
                return "invalid_asset";
            case CreateError::ContentMismatch:
                return "content_mismatch";
            case CreateError::StaleInventoryRevision:
                return "stale_inventory_revision";
            case CreateError::MissingSourceItem:
                return "missing_source_item";
            case CreateError::InsufficientIngredients:
                return "insufficient_ingredients";
            case CreateError::InsufficientGold:
                return "insufficient_gold";
            case CreateError::InvalidEffect:
                return "invalid_effect";
            case CreateError::EnchantCapacityExceeded:
                return "enchant_capacity_exceeded";
            case CreateError::InvalidSoul:
                return "invalid_soul";
            case CreateError::CraftFailed:
                return "craft_failed";
            case CreateError::RateLimited:
                return "rate_limited";
            case CreateError::QuotaExceeded:
                return "quota_exceeded";
            case CreateError::DuplicateRequestConflict:
                return "duplicate_request_conflict";
            case CreateError::RequestPending:
                return "request_pending";
            case CreateError::ServerError:
                return "server_error";
            case CreateError::InvalidAuthoringMode:
                return "invalid_authoring_mode";
            case CreateError::NewRecordStaticCollision:
                return "new_record_static_collision";
            case CreateError::OverrideMissingStatic:
                return "override_missing_static";
            case CreateError::FixedRecordConflict:
                return "fixed_record_conflict";
            case CreateError::OverrideNotBootstrap:
                return "override_not_bootstrap";
            case CreateError::DurableReferenceConflict:
                return "durable_reference_conflict";
            case CreateError::ScriptCompileFailed:
                return "script_compile_failed";
        }
        return "unknown_error";
    }
}
