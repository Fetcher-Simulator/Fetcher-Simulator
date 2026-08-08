#include "EnchantingProtocol.hpp"

namespace mwmp::records
{
    std::string_view getEnchantingErrorCode(EnchantingError error)
    {
        switch (error)
        {
            case EnchantingError::None:
                return "none";
            case EnchantingError::InvalidRequest:
                return "enchanting_invalid_request";
            case EnchantingError::RequestPending:
                return "enchanting_request_pending";
            case EnchantingError::DuplicateRequestConflict:
                return "enchanting_duplicate_request_conflict";
            case EnchantingError::StaleInventoryRevision:
                return "enchanting_stale_inventory_revision";
            case EnchantingError::TargetItemNotFound:
                return "enchanting_target_item_not_found";
            case EnchantingError::TargetItemNotOwned:
                return "enchanting_target_item_not_owned";
            case EnchantingError::InvalidTargetItem:
                return "enchanting_invalid_target_item";
            case EnchantingError::SoulGemNotFound:
                return "enchanting_soul_gem_not_found";
            case EnchantingError::SoulGemNotOwned:
                return "enchanting_soul_gem_not_owned";
            case EnchantingError::InvalidSoulGem:
                return "enchanting_invalid_soul_gem";
            case EnchantingError::EmptySoul:
                return "enchanting_empty_soul";
            case EnchantingError::InvalidSoul:
                return "enchanting_invalid_soul";
            case EnchantingError::DuplicateSourceInstance:
                return "enchanting_duplicate_source_instance";
            case EnchantingError::InvalidEffect:
                return "enchanting_invalid_effect";
            case EnchantingError::EffectNotAllowed:
                return "enchanting_effect_not_allowed";
            case EnchantingError::InvalidMagnitude:
                return "enchanting_invalid_magnitude";
            case EnchantingError::InvalidDuration:
                return "enchanting_invalid_duration";
            case EnchantingError::InvalidArea:
                return "enchanting_invalid_area";
            case EnchantingError::CapacityExceeded:
                return "enchanting_capacity_exceeded";
            case EnchantingError::InvalidCastStyle:
                return "enchanting_invalid_cast_style";
            case EnchantingError::InsufficientGold:
                return "enchanting_insufficient_gold";
            case EnchantingError::InvalidEnchanter:
                return "enchanting_invalid_enchanter";
            case EnchantingError::EnchanterUnavailable:
                return "enchanting_enchanter_unavailable";
            case EnchantingError::MechanicsValidationFailed:
                return "enchanting_mechanics_validation_failed";
            case EnchantingError::ContentMismatch:
                return "enchanting_content_mismatch";
            case EnchantingError::RateLimited:
                return "enchanting_rate_limited";
            case EnchantingError::QuotaExceeded:
                return "enchanting_quota_exceeded";
            case EnchantingError::UnsupportedProtocol:
                return "enchanting_unsupported_protocol";
            case EnchantingError::ServerError:
                return "enchanting_server_error";
        }
        return "enchanting_unknown_error";
    }
}
