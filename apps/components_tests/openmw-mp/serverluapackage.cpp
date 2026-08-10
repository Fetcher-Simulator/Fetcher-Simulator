#include <components/openmw-mp/Packets/System/PacketServerLuaPackage.hpp>
#include <components/openmw-mp/ServerLuaPackage.hpp>
#include <components/openmw-mp/Sha256.hpp>

#include <gtest/gtest.h>

#include <algorithm>

namespace
{
    constexpr std::uint32_t OpenMWLuaApi = 139;

    mwmp::serverlua::Package makePackage(std::string id = "fetcher.gameplay")
    {
        mwmp::serverlua::Package package;
        package.packageId = std::move(id);
        package.packageVersion = 12;
        package.requiredOpenMWLuaApi = OpenMWLuaApi;
        package.files = {
            { "main.lua", 0, {}, "return { engineHandlers = {} }" },
            { "lib/util.lua", 0, {}, "return { value = 1 }" },
        };
        package.registrations = { { "main.lua", mwmp::serverlua::ScriptGlobal } };
        mwmp::serverlua::canonicalize(package);
        package.packageHash = mwmp::serverlua::hashPackage(package);
        return package;
    }

    mwmp::serverlua::PackageSet makeSet()
    {
        mwmp::serverlua::PackageSet set;
        set.packages = { makePackage() };
        set.packageSetHash = mwmp::serverlua::hashPackageSet(set);
        set.generation = mwmp::serverlua::generationFromHash(set.packageSetHash);
        return set;
    }

    bool hasCode(const std::vector<mwmp::serverlua::ValidationError>& errors, std::string_view code)
    {
        return std::any_of(errors.begin(), errors.end(), [&](const auto& error) { return error.code == code; });
    }
}

TEST(ServerLuaPackage, CanonicalHashIsIndependentOfInputOrderAndCase)
{
    auto left = makePackage("Fetcher.Gameplay");
    auto right = left;
    std::reverse(right.files.begin(), right.files.end());
    right.files[0].path = "MAIN.LUA";
    right.files[1].path = "LIB\\UTIL.LUA";
    right.registrations[0].path = "MAIN.LUA";
    right.packageId = "FETCHER.GAMEPLAY";
    right.packageHash.clear();

    EXPECT_EQ(mwmp::serverlua::hashPackage(left), mwmp::serverlua::hashPackage(right));
}

TEST(ServerLuaPackage, PackageSetIdentityIsDeterministic)
{
    auto first = makeSet();
    first.packages.push_back(makePackage("fetcher.ui"));
    first.packageSetHash = mwmp::serverlua::hashPackageSet(first);
    first.generation = mwmp::serverlua::generationFromHash(first.packageSetHash);
    auto second = first;
    std::reverse(second.packages.begin(), second.packages.end());

    EXPECT_EQ(first.packageSetHash, mwmp::serverlua::hashPackageSet(second));
    EXPECT_EQ(first.generation, mwmp::serverlua::generationFromHash(mwmp::serverlua::hashPackageSet(second)));
}

TEST(ServerLuaPackage, RejectsUnsafeAndCollidingPaths)
{
    for (const std::string path : { "../evil.lua", "/evil.lua", "C:/evil.lua", "dir/../evil.lua" })
    {
        auto package = makePackage();
        package.files[0].path = path;
        package.packageHash.clear();
        EXPECT_TRUE(hasCode(mwmp::serverlua::validatePackage(package, OpenMWLuaApi,
                                mwmp::serverlua::MultiplayerLuaApiVersion),
            "invalid_path")) << path;
    }

    auto duplicate = makePackage();
    duplicate.files[0].path = "MAIN.LUA";
    duplicate.packageHash.clear();
    EXPECT_TRUE(hasCode(mwmp::serverlua::validatePackage(duplicate, OpenMWLuaApi,
                            mwmp::serverlua::MultiplayerLuaApiVersion),
        "duplicate_path"));
}

TEST(ServerLuaPackage, RejectsMalformedSourceLimitsAndHashes)
{
    auto package = makePackage();
    package.files[0].source = std::string("\xc0\xaf", 2);
    package.files[0].sourceSize = 2;
    package.files[0].sourceHash = mwmp::crypto::sha256hex(package.files[0].source);
    package.packageHash.clear();
    EXPECT_TRUE(hasCode(mwmp::serverlua::validatePackage(package, OpenMWLuaApi,
                            mwmp::serverlua::MultiplayerLuaApiVersion),
        "invalid_source"));

    package = makePackage();
    package.files[0].sourceHash.assign(64, '0');
    package.packageHash.clear();
    EXPECT_TRUE(hasCode(mwmp::serverlua::validatePackage(package, OpenMWLuaApi,
                            mwmp::serverlua::MultiplayerLuaApiVersion),
        "wrong_file_hash"));

    package = makePackage();
    package.files[0].source.assign(mwmp::serverlua::MaxFileSize + 1, 'x');
    package.files[0].sourceSize = static_cast<std::uint32_t>(package.files[0].source.size());
    package.files[0].sourceHash = mwmp::crypto::sha256hex(package.files[0].source);
    package.packageHash.clear();
    EXPECT_TRUE(hasCode(mwmp::serverlua::validatePackage(package, OpenMWLuaApi,
                            mwmp::serverlua::MultiplayerLuaApiVersion),
        "file_too_large"));
}

TEST(ServerLuaPackage, RejectsUnsupportedVersionsAndDependencies)
{
    auto package = makePackage();
    package.manifestVersion++;
    package.requiredOpenMWLuaApi++;
    package.requiredMultiplayerLuaApi++;
    package.packageHash.clear();
    const auto errors = mwmp::serverlua::validatePackage(
        package, OpenMWLuaApi, mwmp::serverlua::MultiplayerLuaApiVersion);
    EXPECT_TRUE(hasCode(errors, "unsupported_manifest_version"));
    EXPECT_TRUE(hasCode(errors, "unsupported_openmw_lua_api"));
    EXPECT_TRUE(hasCode(errors, "unsupported_multiplayer_lua_api"));

    auto set = makeSet();
    set.packages[0].dependencies = { "missing.package" };
    set.packages[0].packageHash.clear();
    set.packageSetHash.clear();
    set.generation = 0;
    EXPECT_TRUE(hasCode(mwmp::serverlua::validatePackageSet(set, OpenMWLuaApi,
                            mwmp::serverlua::MultiplayerLuaApiVersion),
        "missing_dependency"));
}

TEST(ServerLuaPackage, RejectsWrongPackageAndSetIdentity)
{
    auto package = makePackage();
    package.packageHash.assign(64, '0');
    EXPECT_TRUE(hasCode(mwmp::serverlua::validatePackage(package, OpenMWLuaApi,
                            mwmp::serverlua::MultiplayerLuaApiVersion),
        "wrong_package_hash"));

    auto set = makeSet();
    set.packageSetHash.assign(64, '0');
    EXPECT_TRUE(hasCode(mwmp::serverlua::validatePackageSet(set, OpenMWLuaApi,
                            mwmp::serverlua::MultiplayerLuaApiVersion),
        "wrong_package_set_hash"));
}

TEST(ServerLuaPackage, ManifestAndTransferPacketsRoundTrip)
{
    auto set = makeSet();
    mwmp::PacketServerLuaPackageManifest outgoing;
    outgoing.packageSet = set;
    mwmp::PacketServerLuaPackageManifest incoming;
    ASSERT_TRUE(incoming.decode(outgoing.encode()));
    ASSERT_EQ(incoming.packageSet.packages.size(), 1u);
    EXPECT_EQ(incoming.packageSet.packageSetHash, set.packageSetHash);
    EXPECT_EQ(incoming.packageSet.packages[0].files[0].sourceSize, set.packages[0].files[0].sourceSize);
    EXPECT_TRUE(incoming.packageSet.packages[0].files[0].source.empty());
    EXPECT_TRUE(mwmp::serverlua::validatePackageSet(incoming.packageSet, OpenMWLuaApi,
                    mwmp::serverlua::MultiplayerLuaApiVersion, false)
                    .empty());

    mwmp::PacketServerLuaPackageChunk chunk;
    chunk.generation = set.generation;
    chunk.packageId = set.packages[0].packageId;
    chunk.packageHash = set.packages[0].packageHash;
    chunk.filePath = set.packages[0].files[0].path;
    chunk.offset = 7;
    chunk.bytes = "example";
    mwmp::PacketServerLuaPackageChunk decodedChunk;
    ASSERT_TRUE(decodedChunk.decode(chunk.encode()));
    EXPECT_EQ(decodedChunk.bytes, chunk.bytes);
    EXPECT_EQ(decodedChunk.offset, 7u);

    mwmp::PacketServerLuaPackageBootstrapComplete complete;
    complete.generation = set.generation;
    complete.packageSetHash = set.packageSetHash;
    mwmp::PacketServerLuaPackageBootstrapComplete decodedComplete;
    ASSERT_TRUE(decodedComplete.decode(complete.encode()));
    EXPECT_EQ(decodedComplete.generation, set.generation);
    EXPECT_EQ(decodedComplete.packageSetHash, set.packageSetHash);
}

TEST(ServerLuaPackage, ReservedNamespaceCannotShadowBuiltinScripts)
{
    EXPECT_EQ(mwmp::serverlua::virtualPath("fetcher.crime", "main.lua"),
        "scripts/multiplayer/fetcher/crime/main.lua");
    EXPECT_FALSE(mwmp::serverlua::virtualPath("fetcher.crime", "main.lua").starts_with("scripts/omw/"));
}
