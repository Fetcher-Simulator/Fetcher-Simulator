#ifndef OPENMW_MP_CRIMEINTERACTION_HPP
#define OPENMW_MP_CRIMEINTERACTION_HPP

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>

namespace mwmp
{
    inline constexpr std::uint16_t CrimeInteractionProtocolVersion = 1;
    inline constexpr std::size_t MaximumCrimeInteractionStringLength = 255;
    inline constexpr std::size_t MaximumCrimeInteractionRequestIdLength = 128;

    enum class CrimeInteractionKind : std::uint8_t
    {
        UnlockAttempt = 1,
    };

    struct CrimeInteractionRequest
    {
        std::uint16_t protocolVersion = CrimeInteractionProtocolVersion;
        std::string requestId;
        CrimeInteractionKind kind = CrimeInteractionKind::UnlockAttempt;
        std::string cellId;
        std::string refId;
        std::uint32_t refNum = 0;
        std::int32_t refContentFile = -1;

        bool operator==(const CrimeInteractionRequest&) const = default;
    };

    bool validateCrimeInteractionRequest(const CrimeInteractionRequest& request);
    std::string canonicalCrimeInteractionRequest(const CrimeInteractionRequest& request);
}

#endif
