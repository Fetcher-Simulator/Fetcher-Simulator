#include <components/openmw-mp/Packets/System/PacketServerLuaPackage.hpp>
#include <components/openmw-mp/ServerLuaPackage.hpp>
#include <components/openmw-mp/ServerLuaPackageTransfer.hpp>
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

    mwmp::serverlua::CompatibilityOverride makeOverride(
        std::string target = "scripts/inventoryextender/item.lua")
    {
        return { std::move(target), "main.lua", mwmp::serverlua::OverrideBasePolicy::AcceptedHashes,
            { std::string(64, 'a') } };
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

TEST(ServerLuaPackage, CompatibilityOverrideMeaningIsCanonicalAndChangesIdentity)
{
    auto left = makePackage();
    left.overrides = { makeOverride("Scripts\\InventoryExtender\\ITEM.LUA") };
    left.packageHash.clear();
    auto right = left;
    right.overrides[0].target = "scripts/inventoryextender/item.lua";
    std::reverse(right.overrides[0].acceptedBaseHashes.begin(), right.overrides[0].acceptedBaseHashes.end());
    EXPECT_EQ(mwmp::serverlua::hashPackage(left), mwmp::serverlua::hashPackage(right));

    right.overrides[0].acceptedBaseHashes = { std::string(64, 'b') };
    EXPECT_NE(mwmp::serverlua::hashPackage(left), mwmp::serverlua::hashPackage(right));
    right = left;
    right.overrides[0].target = "scripts/inventoryextender/other.lua";
    EXPECT_NE(mwmp::serverlua::hashPackage(left), mwmp::serverlua::hashPackage(right));
}

TEST(ServerLuaPackage, ValidatesCompatibilityOverrideTargetsSourcesAndBasePolicies)
{
    auto package = makePackage();
    package.registrations.clear();
    package.overrides = { makeOverride() };
    package.packageHash.clear();
    EXPECT_TRUE(mwmp::serverlua::validatePackage(
                    package, OpenMWLuaApi, mwmp::serverlua::MultiplayerLuaApiVersion)
                    .empty());

    for (const std::string target : { "../evil.lua", "/scripts/foo.lua", "C:/scripts/foo.lua",
             "scripts/foo.txt", "scripts/omw/player.lua", "scripts/multiplayer/foo.lua" })
    {
        auto invalid = package;
        invalid.overrides[0].target = target;
        EXPECT_TRUE(hasCode(mwmp::serverlua::validatePackage(invalid, OpenMWLuaApi,
                                mwmp::serverlua::MultiplayerLuaApiVersion),
            "override_target_forbidden")) << target;
    }

    auto missingSource = package;
    missingSource.overrides[0].source = "missing.lua";
    EXPECT_TRUE(hasCode(mwmp::serverlua::validatePackage(missingSource, OpenMWLuaApi,
                            mwmp::serverlua::MultiplayerLuaApiVersion),
        "override_source_missing"));

    auto missingPolicy = package;
    missingPolicy.overrides[0].acceptedBaseHashes.clear();
    EXPECT_TRUE(hasCode(mwmp::serverlua::validatePackage(missingPolicy, OpenMWLuaApi,
                            mwmp::serverlua::MultiplayerLuaApiVersion),
        "missing_base_hash_policy"));

    auto anyPolicy = missingPolicy;
    anyPolicy.overrides[0].basePolicy = mwmp::serverlua::OverrideBasePolicy::Any;
    EXPECT_TRUE(mwmp::serverlua::validatePackage(
                    anyPolicy, OpenMWLuaApi, mwmp::serverlua::MultiplayerLuaApiVersion)
                    .empty());
}

TEST(ServerLuaPackage, ValidatesInstalledBaseBeforeOverrideActivation)
{
    const std::string baseSource = "return { value = 1 }";
    auto override = makeOverride();
    override.acceptedBaseHashes = { mwmp::crypto::sha256hex(baseSource) };

    const auto accepted = mwmp::serverlua::validateOverrideBase(override, baseSource, false);
    EXPECT_TRUE(accepted);
    EXPECT_EQ(accepted.baseHash, override.acceptedBaseHashes[0]);

    override.acceptedBaseHashes = { std::string(64, 'b') };
    const auto mismatch = mwmp::serverlua::validateOverrideBase(override, baseSource, false);
    ASSERT_FALSE(mismatch);
    EXPECT_EQ(mismatch.error->code, "override_base_hash_mismatch");
    EXPECT_EQ(mismatch.baseHash, mwmp::crypto::sha256hex(baseSource));

    const auto missing = mwmp::serverlua::validateOverrideBase(override, std::nullopt, false);
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error->code, "override_target_missing");

    const auto loaded = mwmp::serverlua::validateOverrideBase(override, baseSource, true);
    ASSERT_FALSE(loaded);
    EXPECT_EQ(loaded.error->code, "override_target_already_loaded");

    override.basePolicy = mwmp::serverlua::OverrideBasePolicy::Any;
    override.acceptedBaseHashes.clear();
    EXPECT_TRUE(mwmp::serverlua::validateOverrideBase(override, baseSource, false));
}

TEST(ServerLuaPackage, RejectsDuplicateOverrideTargetAcrossPackageSet)
{
    auto set = makeSet();
    set.packages[0].overrides = { makeOverride() };
    set.packages[0].packageHash.clear();
    auto second = makePackage("fetcher.second");
    second.overrides = { makeOverride("SCRIPTS/INVENTORYEXTENDER/ITEM.LUA") };
    second.packageHash.clear();
    set.packages.push_back(std::move(second));
    set.packageSetHash.clear();
    set.generation = 0;
    EXPECT_TRUE(hasCode(mwmp::serverlua::validatePackageSet(set, OpenMWLuaApi,
                            mwmp::serverlua::MultiplayerLuaApiVersion),
        "duplicate_override_target"));
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
    set.packages[0].overrides = { makeOverride() };
    set.packages[0].packageHash = mwmp::serverlua::hashPackage(set.packages[0]);
    set.packageSetHash = mwmp::serverlua::hashPackageSet(set);
    set.generation = mwmp::serverlua::generationFromHash(set.packageSetHash);
    mwmp::PacketServerLuaPackageManifest outgoing;
    outgoing.packageSet = set;
    mwmp::PacketServerLuaPackageManifest incoming;
    ASSERT_TRUE(incoming.decode(outgoing.encode()));
    ASSERT_EQ(incoming.packageSet.packages.size(), 1u);
    EXPECT_EQ(incoming.packageSet.packageSetHash, set.packageSetHash);
    EXPECT_EQ(incoming.packageSet.packages[0].files[0].sourceSize, set.packages[0].files[0].sourceSize);
    EXPECT_TRUE(incoming.packageSet.packages[0].files[0].source.empty());
    EXPECT_EQ(incoming.packageSet.packages[0].overrides, set.packages[0].overrides);
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

TEST(ServerLuaPackageTransfer, AssemblesOutOfOrderChunksAndAcceptsIdenticalDuplicates)
{
    const auto expected = makeSet();
    auto manifest = expected;
    for (auto& package : manifest.packages)
        for (auto& file : package.files)
            file.source.clear();

    mwmp::serverlua::PackageTransfer transfer;
    ASSERT_TRUE(transfer.begin(manifest, OpenMWLuaApi, mwmp::serverlua::MultiplayerLuaApiVersion));
    for (std::size_t fileIndex = expected.packages[0].files.size(); fileIndex > 0; --fileIndex)
    {
        const auto& file = expected.packages[0].files[fileIndex - 1];
        const std::size_t split = file.source.size() / 2;
        ASSERT_TRUE(transfer.receive(expected.generation, expected.packages[0].packageId,
            expected.packages[0].packageHash, file.path, static_cast<std::uint32_t>(split),
            std::string_view(file.source).substr(split)));
        ASSERT_TRUE(transfer.receive(expected.generation, expected.packages[0].packageId,
            expected.packages[0].packageHash, file.path, 0, std::string_view(file.source).substr(0, split)));
        ASSERT_TRUE(transfer.receive(expected.generation, expected.packages[0].packageId,
            expected.packages[0].packageHash, file.path, 0, std::string_view(file.source).substr(0, split)));
    }
    ASSERT_TRUE(transfer.finish(expected.generation, expected.packageSetHash, OpenMWLuaApi,
        mwmp::serverlua::MultiplayerLuaApiVersion));
    ASSERT_NE(transfer.readyPackageSet(), nullptr);
    EXPECT_EQ(*transfer.readyPackageSet(), expected);
    const auto ready = transfer.takeReadyPackageSet();
    ASSERT_TRUE(ready.has_value());
    EXPECT_EQ(*ready, expected);
    EXPECT_EQ(transfer.state(), mwmp::serverlua::PackageTransfer::State::Empty);
}

TEST(ServerLuaPackageTransfer, RejectsConflictingOutOfRangeAndStaleChunks)
{
    const auto expected = makeSet();
    auto manifest = expected;
    for (auto& package : manifest.packages)
        for (auto& file : package.files)
            file.source.clear();
    const auto& package = expected.packages[0];
    const auto& file = package.files[0];

    mwmp::serverlua::PackageTransfer transfer;
    ASSERT_TRUE(transfer.begin(manifest, OpenMWLuaApi, mwmp::serverlua::MultiplayerLuaApiVersion));
    EXPECT_FALSE(transfer.receive(expected.generation + 1, package.packageId, package.packageHash,
        file.path, 0, "x"));
    EXPECT_EQ(transfer.state(), mwmp::serverlua::PackageTransfer::State::Failed);

    ASSERT_TRUE(transfer.begin(manifest, OpenMWLuaApi, mwmp::serverlua::MultiplayerLuaApiVersion));
    EXPECT_FALSE(transfer.receive(expected.generation, package.packageId, package.packageHash,
        file.path, file.sourceSize, "x"));

    ASSERT_TRUE(transfer.begin(manifest, OpenMWLuaApi, mwmp::serverlua::MultiplayerLuaApiVersion));
    ASSERT_TRUE(transfer.receive(expected.generation, package.packageId, package.packageHash,
        file.path, 0, "a"));
    EXPECT_FALSE(transfer.receive(expected.generation, package.packageId, package.packageHash,
        file.path, 0, "b"));
}

TEST(ServerLuaPackageTransfer, CompletionFailsClosedForIncompleteOrWrongHash)
{
    const auto expected = makeSet();
    auto manifest = expected;
    for (auto& package : manifest.packages)
        for (auto& file : package.files)
            file.source.clear();

    mwmp::serverlua::PackageTransfer transfer;
    ASSERT_TRUE(transfer.begin(manifest, OpenMWLuaApi, mwmp::serverlua::MultiplayerLuaApiVersion));
    EXPECT_FALSE(transfer.finish(expected.generation, expected.packageSetHash, OpenMWLuaApi,
        mwmp::serverlua::MultiplayerLuaApiVersion));

    manifest.packages[0].files[0].sourceHash.assign(64, '0');
    manifest.packages[0].packageHash = mwmp::serverlua::hashPackage(expected.packages[0]);
    manifest.packageSetHash = mwmp::serverlua::hashPackageSet(manifest);
    manifest.generation = mwmp::serverlua::generationFromHash(manifest.packageSetHash);
    ASSERT_TRUE(transfer.begin(manifest, OpenMWLuaApi, mwmp::serverlua::MultiplayerLuaApiVersion));
    for (const auto& file : expected.packages[0].files)
        ASSERT_TRUE(transfer.receive(manifest.generation, manifest.packages[0].packageId,
            manifest.packages[0].packageHash, file.path, 0, file.source));
    EXPECT_FALSE(transfer.finish(manifest.generation, manifest.packageSetHash, OpenMWLuaApi,
        mwmp::serverlua::MultiplayerLuaApiVersion));
}
