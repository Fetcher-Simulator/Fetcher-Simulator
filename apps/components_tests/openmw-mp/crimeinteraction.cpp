#include <gtest/gtest.h>

#include <components/openmw-mp/CrimeInteraction.hpp>
#include <components/openmw-mp/Packets/Object/PacketCrimeInteraction.hpp>

TEST(CrimeInteractionProtocol, UnlockAttemptRoundTripsCanonicallyAndStrictly)
{
    mwmp::PacketCrimeInteraction outgoing;
    outgoing.request.requestId = "crime-interaction-1";
    outgoing.request.cellId = "Balmora";
    outgoing.request.refId = "crate_01";
    outgoing.request.refNum = 42;
    outgoing.request.refContentFile = 3;
    const auto encoded = outgoing.encode();
    EXPECT_EQ(encoded, outgoing.encode());

    mwmp::PacketCrimeInteraction incoming;
    ASSERT_TRUE(incoming.decode(encoded));
    EXPECT_EQ(incoming.request, outgoing.request);
    EXPECT_EQ(mwmp::canonicalCrimeInteractionRequest(incoming.request),
        mwmp::canonicalCrimeInteractionRequest(outgoing.request));

    auto trailing = encoded;
    trailing.push_back(0);
    EXPECT_FALSE(incoming.decode(trailing));
}

TEST(CrimeInteractionProtocol, RejectsMissingCanonicalReference)
{
    mwmp::CrimeInteractionRequest request;
    request.requestId = "crime-interaction-1";
    request.cellId = "Balmora";
    request.refId = "crate_01";
    EXPECT_FALSE(mwmp::validateCrimeInteractionRequest(request));
    request.refNum = 42;
    EXPECT_FALSE(mwmp::validateCrimeInteractionRequest(request));
    request.refContentFile = 3;
    EXPECT_TRUE(mwmp::validateCrimeInteractionRequest(request));
}

TEST(CrimeInteractionProtocol, RejectsUnknownKindsAndNonCanonicalStrings)
{
    mwmp::CrimeInteractionRequest request;
    request.requestId = "crime-interaction-2";
    request.cellId = "Balmora";
    request.refId = "door_01";
    request.refNum = 7;
    request.refContentFile = 1;
    ASSERT_TRUE(mwmp::validateCrimeInteractionRequest(request));

    request.kind = static_cast<mwmp::CrimeInteractionKind>(99);
    EXPECT_FALSE(mwmp::validateCrimeInteractionRequest(request));
    request.kind = mwmp::CrimeInteractionKind::UnlockAttempt;

    request.requestId = std::string(mwmp::MaximumCrimeInteractionRequestIdLength + 1, 'x');
    EXPECT_FALSE(mwmp::validateCrimeInteractionRequest(request));
    request.requestId = "crime-interaction-2";
    request.cellId = "Bal\nmora";
    EXPECT_FALSE(mwmp::validateCrimeInteractionRequest(request));
    request.cellId = "Balmora";
    request.refId = std::string("door") + static_cast<char>(0x7f);
    EXPECT_FALSE(mwmp::validateCrimeInteractionRequest(request));
}

TEST(CrimeInteractionProtocol, CanonicalEncodingBindsEveryAuthoritativeReferenceField)
{
    mwmp::CrimeInteractionRequest request;
    request.requestId = "crime-interaction-3";
    request.cellId = "Balmora";
    request.refId = "door_01";
    request.refNum = 7;
    request.refContentFile = 1;
    const std::string canonical = mwmp::canonicalCrimeInteractionRequest(request);

    auto changed = request;
    changed.refNum = 8;
    EXPECT_NE(mwmp::canonicalCrimeInteractionRequest(changed), canonical);
    changed = request;
    changed.refContentFile = 2;
    EXPECT_NE(mwmp::canonicalCrimeInteractionRequest(changed), canonical);
    changed = request;
    changed.cellId = "Ald-ruhn";
    EXPECT_NE(mwmp::canonicalCrimeInteractionRequest(changed), canonical);
    changed = request;
    changed.refId = "door_02";
    EXPECT_NE(mwmp::canonicalCrimeInteractionRequest(changed), canonical);
}
