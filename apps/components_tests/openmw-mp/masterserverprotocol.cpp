#include <components/openmw-mp/MasterServerProtocol.hpp>
#include <components/openmw-mp/Packets/System/PacketHandshake.hpp>

#include <gtest/gtest.h>

namespace
{
    constexpr std::string_view ValidEntry = R"({
        "id":"3c0a95c9-56b2-4dab-97e4-a06f62b81c46",
        "name":"A \"quoted\" {server} \\ \u03a9",
        "host":"203.0.113.1",
        "port":25565,
        "current_players":7,
        "max_players":32,
        "build_version":"0.51.0",
        "protocol_version":1,
        "game_mode":"Co-op",
        "country":"US"
    })";

    mwmp::PublicServerEntry entry(std::string name, int players)
    {
        mwmp::PublicServerEntry result;
        result.name = std::move(name);
        result.currentPlayers = players;
        return result;
    }
}

TEST(MasterServerProtocol, ParsesEscapedAndUnicodePublicEntry)
{
    const auto parsed = mwmp::parsePublicServerList("[" + std::string(ValidEntry) + "]");
    ASSERT_TRUE(parsed.error.empty()) << parsed.error;
    ASSERT_EQ(parsed.entries.size(), 1);
    EXPECT_EQ(parsed.entries[0].name, "A \"quoted\" {server} \\ \xce\xa9");
    EXPECT_EQ(parsed.entries[0].currentPlayers, 7);
    EXPECT_EQ(parsed.entries[0].protocolVersion, mwmp::MultiplayerProtocolVersion);
}

TEST(MasterServerProtocol, SkipsInvalidEntriesWithoutDiscardingValidOnes)
{
    const auto parsed
        = mwmp::parsePublicServerList("[" + std::string(ValidEntry) + R"(,{"name":"missing fields"})" + "]");
    EXPECT_TRUE(parsed.error.empty());
    EXPECT_EQ(parsed.entries.size(), 1);
    EXPECT_EQ(parsed.skippedEntries, 1);
}

TEST(MasterServerProtocol, RejectsMalformedRoot)
{
    EXPECT_FALSE(mwmp::parsePublicServerList("{broken").error.empty());
    EXPECT_FALSE(mwmp::parsePublicServerList("{}").error.empty());
}

TEST(MasterServerProtocol, RejectsInvalidPublicPlayerCounts)
{
    std::string invalid(ValidEntry);
    const auto position = invalid.find("\"current_players\":7");
    invalid.replace(position, std::string("\"current_players\":7").size(), "\"current_players\":33");
    const auto parsed = mwmp::parsePublicServerList("[" + invalid + "]");
    EXPECT_TRUE(parsed.entries.empty());
    EXPECT_EQ(parsed.skippedEntries, 1);
}

TEST(MasterServerProtocol, EqualValuesNeverCompareLess)
{
    const auto value = entry("same", 3);
    EXPECT_FALSE(mwmp::serverEntryLess(value, value, mwmp::ServerSortColumn::Name, true));
    EXPECT_FALSE(mwmp::serverEntryLess(value, value, mwmp::ServerSortColumn::Name, false));
    EXPECT_FALSE(mwmp::serverEntryLess(value, value, mwmp::ServerSortColumn::Players, true));
    EXPECT_FALSE(mwmp::serverEntryLess(value, value, mwmp::ServerSortColumn::Players, false));
}

TEST(MasterServerProtocol, SortsNamesAscendingAndDescending)
{
    const std::vector entries{ entry("Beta", 1), entry("Alpha", 2), entry("Gamma", 3) };
    EXPECT_EQ(
        mwmp::sortedServerIndices(entries, mwmp::ServerSortColumn::Name, true), (std::vector<std::size_t>{ 1, 0, 2 }));
    EXPECT_EQ(
        mwmp::sortedServerIndices(entries, mwmp::ServerSortColumn::Name, false), (std::vector<std::size_t>{ 2, 0, 1 }));
}

TEST(MasterServerProtocol, SortsPlayersAscendingAndDescendingStably)
{
    const std::vector entries{ entry("first", 2), entry("second", 1), entry("third", 2) };
    EXPECT_EQ(mwmp::sortedServerIndices(entries, mwmp::ServerSortColumn::Players, true),
        (std::vector<std::size_t>{ 1, 0, 2 }));
    EXPECT_EQ(mwmp::sortedServerIndices(entries, mwmp::ServerSortColumn::Players, false),
        (std::vector<std::size_t>{ 0, 2, 1 }));
}

TEST(MasterServerProtocol, DeterminesCompatibility)
{
    auto compatible = entry("compatible", 0);
    compatible.protocolVersion = mwmp::MultiplayerProtocolVersion;
    auto incompatible = compatible;
    incompatible.protocolVersion += 1;
    EXPECT_TRUE(mwmp::isProtocolCompatible(compatible));
    EXPECT_FALSE(mwmp::isProtocolCompatible(incompatible));
}

TEST(MasterServerProtocol, EllipsizesOnlyAtUtf8CodePointBoundaries)
{
    EXPECT_EQ(mwmp::ellipsizeUtf8("ab\xce\xa9"
                                  "cd",
                  4),
        "ab\xce\xa9\xe2\x80\xa6");
    EXPECT_EQ(mwmp::ellipsizeUtf8("ab\xce\xa9", 3), "ab\xce\xa9");
}

TEST(MasterServerProtocol, ParsesOnlyBoundedUuidTokens)
{
    EXPECT_EQ(mwmp::parseRegistrationToken(R"({"id":"public","token":"3c0a95c9-56b2-4dab-97e4-a06f62b81c46"})"),
        "3c0a95c9-56b2-4dab-97e4-a06f62b81c46");
    EXPECT_TRUE(mwmp::parseRegistrationToken(R"({"token":"secret"})").empty());
}

TEST(MasterServerProtocol, BoundsRetryBackoffAndHandlesUnknownTokens)
{
    EXPECT_EQ(mwmp::registrationRetryBackoff(0), std::chrono::seconds(1));
    EXPECT_EQ(mwmp::registrationRetryBackoff(3), std::chrono::seconds(8));
    EXPECT_EQ(mwmp::registrationRetryBackoff(20), std::chrono::seconds(64));
    EXPECT_TRUE(mwmp::heartbeatStatusRequiresRegistration(401));
    EXPECT_TRUE(mwmp::heartbeatStatusRequiresRegistration(404));
    EXPECT_FALSE(mwmp::heartbeatStatusRequiresRegistration(500));
}

TEST(MasterServerProtocol, HandshakeCarriesProtocolSeparatelyFromBuildVersion)
{
    mwmp::PacketHandshake outgoing;
    outgoing.clientVersion = "custom-build";
    outgoing.protocolVersion = mwmp::MultiplayerProtocolVersion;
    outgoing.playerName = "player";

    const std::vector<std::uint8_t> encoded = outgoing.encode();
    mwmp::PacketHandshake incoming;
    ASSERT_TRUE(incoming.decode(encoded.data(), encoded.size()));
    EXPECT_EQ(incoming.clientVersion, "custom-build");
    EXPECT_EQ(incoming.protocolVersion, mwmp::MultiplayerProtocolVersion);
}
