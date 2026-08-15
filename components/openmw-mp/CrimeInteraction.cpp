#include "CrimeInteraction.hpp"

namespace
{
    bool validString(std::string_view value, std::size_t maximum)
    {
        if (value.empty() || value.size() > maximum)
            return false;
        for (unsigned char c : value)
        {
            if (c == 0 || c < 0x20 || c == 0x7f)
                return false;
        }
        return true;
    }

    void appendU32(std::string& bytes, std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
            bytes.push_back(static_cast<char>(value >> shift));
    }

    void appendString(std::string& bytes, std::string_view value)
    {
        appendU32(bytes, static_cast<std::uint32_t>(value.size()));
        bytes.append(value);
    }
}

bool mwmp::validateCrimeInteractionRequest(const CrimeInteractionRequest& request)
{
    return request.protocolVersion == CrimeInteractionProtocolVersion
        && request.kind == CrimeInteractionKind::UnlockAttempt
        && validString(request.requestId, MaximumCrimeInteractionRequestIdLength)
        && validString(request.cellId, MaximumCrimeInteractionStringLength)
        && validString(request.refId, MaximumCrimeInteractionStringLength)
        && request.refNum != 0 && request.refContentFile >= 0;
}

std::string mwmp::canonicalCrimeInteractionRequest(const CrimeInteractionRequest& request)
{
    std::string bytes("OMCI", 4);
    bytes.push_back(static_cast<char>(request.protocolVersion));
    bytes.push_back(static_cast<char>(request.protocolVersion >> 8));
    bytes.push_back(static_cast<char>(request.kind));
    appendString(bytes, request.requestId);
    appendString(bytes, request.cellId);
    appendString(bytes, request.refId);
    appendU32(bytes, request.refNum);
    appendU32(bytes, static_cast<std::uint32_t>(request.refContentFile));
    return bytes;
}
