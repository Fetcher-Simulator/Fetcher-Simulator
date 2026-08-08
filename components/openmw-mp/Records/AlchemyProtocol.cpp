#include "AlchemyProtocol.hpp"

namespace mwmp::records
{
    std::string_view getAlchemyErrorCode(AlchemyError error)
    {
        switch (error)
        {
            case AlchemyError::None:
                return "none";
            case AlchemyError::InvalidRequest:
                return "alchemy_invalid_request";
            case AlchemyError::RequestPending:
                return "alchemy_request_pending";
            case AlchemyError::DuplicateRequestConflict:
                return "alchemy_duplicate_request_conflict";
            case AlchemyError::StaleInventoryRevision:
                return "alchemy_stale_inventory_revision";
            case AlchemyError::IngredientNotFound:
                return "alchemy_ingredient_not_found";
            case AlchemyError::IngredientNotOwned:
                return "alchemy_ingredient_not_owned";
            case AlchemyError::InvalidIngredient:
                return "alchemy_invalid_ingredient";
            case AlchemyError::DuplicateSourceInstance:
                return "alchemy_duplicate_source_instance";
            case AlchemyError::ApparatusNotFound:
                return "alchemy_apparatus_not_found";
            case AlchemyError::InvalidApparatus:
                return "alchemy_invalid_apparatus";
            case AlchemyError::ContentMismatch:
                return "alchemy_content_mismatch";
            case AlchemyError::MechanicsValidationFailed:
                return "alchemy_mechanics_validation_failed";
            case AlchemyError::ServerError:
                return "alchemy_server_error";
            case AlchemyError::UnsupportedProtocol:
                return "alchemy_unsupported_protocol";
            case AlchemyError::RateLimited:
                return "alchemy_rate_limited";
            case AlchemyError::QuotaExceeded:
                return "alchemy_quota_exceeded";
        }
        return "alchemy_unknown_error";
    }
}
