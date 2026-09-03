#include <apps/openmw-server/ServerLuaPackageRegistry.hpp>

#include <gtest/gtest.h>

#include <apps/openmw-server/ServerLuaPackageRegistry.hpp>
#include <components/openmw-mp/ServerLuaPackage.hpp>

#include <filesystem>
#include <fstream>

namespace
{
    class TemporaryDirectory
    {
    public:
        TemporaryDirectory()
        {
            mPath = std::filesystem::temp_directory_path()
                / ("openmw-server-lua-package-test-" + std::to_string(++sCounter));
            std::filesystem::remove_all(mPath);
            std::filesystem::create_directories(mPath);
        }

        ~TemporaryDirectory() { std::filesystem::remove_all(mPath); }
        const std::filesystem::path& path() const { return mPath; }

    private:
        inline static std::uint64_t sCounter = 0;
        std::filesystem::path mPath;
    };

    void writeFile(const std::filesystem::path& path, std::string_view value)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary);
        stream.write(value.data(), static_cast<std::streamsize>(value.size()));
    }

    void writePackage(const std::filesystem::path& root, std::string_view directory,
        std::string_view id, std::string_view dependencies = "[]")
    {
        const auto package = root / directory;
        writeFile(package / "main.lua", "return { engineHandlers = {} }");
        writeFile(package / "manifest.yaml",
            "manifestVersion: 3\npackageId: " + std::string(id)
                + "\npackageVersion: 1\nrequiredOpenMWLuaApi: 141\nrequiredMultiplayerLuaApi: 1\n"
                  "dependencies: "
                + std::string(dependencies)
                + "\nfiles:\n  - main.lua\nscripts:\n  - path: main.lua\n    flags: [global]\n");
    }
}

TEST(ServerLuaPackageRegistry, MissingRootProducesExplicitEmptyGeneration)
{
    TemporaryDirectory temp;
    const auto missing = temp.path() / "missing";
    mwmp::ServerLuaPackageRegistry registry(missing, 141);
    EXPECT_TRUE(registry.packageSet().packages.empty());
    EXPECT_FALSE(registry.packageSet().packageSetHash.empty());
    EXPECT_NE(registry.packageSet().generation, 0u);
}

TEST(ServerLuaPackageRegistry, LoadsAndOrdersDependenciesDeterministically)
{
    TemporaryDirectory temp;
    writePackage(temp.path(), "z-policy", "fetcher.policy", "[fetcher.base]");
    writePackage(temp.path(), "a-base", "fetcher.base");

    mwmp::ServerLuaPackageRegistry registry(temp.path(), 141);
    ASSERT_EQ(registry.packageSet().packages.size(), 2u);
    EXPECT_EQ(registry.packageSet().packages[0].packageId, "fetcher.base");
    EXPECT_EQ(registry.packageSet().packages[1].packageId, "fetcher.policy");
    EXPECT_EQ(registry.packageSet().packages[0].files[0].sourceSize,
        registry.packageSet().packages[0].files[0].source.size());
}

TEST(ServerLuaPackageRegistry, FailsStartupForInvalidPackageConfiguration)
{
    TemporaryDirectory temp;
    writePackage(temp.path(), "bad", "fetcher.bad");
    writeFile(temp.path() / "bad" / "manifest.yaml",
        "manifestVersion: 3\npackageId: fetcher.bad\npackageVersion: 1\nrequiredOpenMWLuaApi: 141\n"
        "requiredMultiplayerLuaApi: 1\ndependencies: []\nfiles: [main.lua]\n"
        "scripts:\n  - path: main.lua\n    flags: [global]\n");
    EXPECT_THROW(mwmp::ServerLuaPackageRegistry(temp.path(), 139), std::runtime_error);
}

TEST(ServerLuaPackageRegistry, RejectsNonCanonicalFilesystemPaths)
{
    TemporaryDirectory temp;
    writePackage(temp.path(), "bad", "fetcher.bad");
    writeFile(temp.path() / "bad" / "manifest.yaml",
        "manifestVersion: 3\npackageId: fetcher.bad\npackageVersion: 1\nrequiredOpenMWLuaApi: 141\n"
        "requiredMultiplayerLuaApi: 1\ndependencies: []\nfiles: [MAIN.LUA]\n"
        "scripts:\n  - path: MAIN.LUA\n    flags: [global]\n");
    EXPECT_THROW(mwmp::ServerLuaPackageRegistry(temp.path(), 141), std::runtime_error);
}

TEST(ServerLuaPackageRegistry, LoadsOverrideOnlyPackageWithExplicitBasePolicy)
{
    TemporaryDirectory temp;
    const auto package = temp.path() / "compatibility";
    writeFile(package / "overrides" / "item.lua", "return { engineHandlers = {} }");
    writeFile(package / "manifest.yaml",
        "manifestVersion: 3\npackageId: fetcher.compatibility\npackageVersion: 1\nrequiredOpenMWLuaApi: 141\n"
        "requiredMultiplayerLuaApi: 1\ndependencies: []\nfiles: [overrides/item.lua]\n"
        "overrides:\n  - target: scripts/inventoryextender/item.lua\n"
        "    source: overrides/item.lua\n    basePolicy: any\n    targetPolicy: if-present\n");

    mwmp::ServerLuaPackageRegistry registry(temp.path(), 141);
    ASSERT_EQ(registry.packageSet().packages.size(), 1u);
    const auto& loaded = registry.packageSet().packages[0];
    EXPECT_TRUE(loaded.registrations.empty());
    ASSERT_EQ(loaded.overrides.size(), 1u);
    EXPECT_EQ(loaded.overrides[0].basePolicy, mwmp::serverlua::OverrideBasePolicy::Any);
    EXPECT_EQ(loaded.overrides[0].targetPolicy, mwmp::serverlua::OverrideTargetPolicy::IfPresent);
}

TEST(ServerLuaPackageRegistry, OverrideTargetPolicyDefaultsToRequired)
{
    TemporaryDirectory temp;
    const auto package = temp.path() / "compatibility";
    writeFile(package / "overrides" / "item.lua", "return { engineHandlers = {} }");
    writeFile(package / "manifest.yaml",
        "manifestVersion: 3\npackageId: fetcher.compatibility\npackageVersion: 1\nrequiredOpenMWLuaApi: 141\n"
        "requiredMultiplayerLuaApi: 1\ndependencies: []\nfiles: [overrides/item.lua]\n"
        "overrides:\n  - target: scripts/inventoryextender/item.lua\n"
        "    source: overrides/item.lua\n    basePolicy: any\n");

    mwmp::ServerLuaPackageRegistry registry(temp.path(), 141);
    ASSERT_EQ(registry.packageSet().packages[0].overrides.size(), 1u);
    EXPECT_EQ(registry.packageSet().packages[0].overrides[0].targetPolicy,
        mwmp::serverlua::OverrideTargetPolicy::Required);
}


TEST(ServerLuaPackageRegistry, ShippedInventoryExtenderFixBootstrapsBarterBeforeShowingStock)
{
    const auto root = std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR }
        / "files" / "server" / "server-lua-packages";
    mwmp::ServerLuaPackageRegistry registry(root, 141);
    const auto packageIt = std::find_if(registry.packageSet().packages.begin(), registry.packageSet().packages.end(),
        [](const auto& package) { return package.packageId == "fetcher.inventoryextender-fix"; });
    ASSERT_NE(packageIt, registry.packageSet().packages.end());

    const auto& package = *packageIt;
    EXPECT_EQ(package.packageId, "fetcher.inventoryextender-fix");
    EXPECT_EQ(package.packageVersion, 20u);
    EXPECT_EQ(package.requiredMultiplayerLuaApi, 9u);
    EXPECT_LE(package.requiredMultiplayerLuaApi, mwmp::serverlua::MultiplayerLuaApiVersion);

    const auto findSource = [&](std::string_view path) -> const std::string* {
        const auto it = std::find_if(package.files.begin(), package.files.end(), [&](const auto& file) {
            return file.path == path;
        });
        return it == package.files.end() ? nullptr : &it->source;
    };
    const auto countOccurrences = [](const std::string& source, std::string_view needle) {
        std::size_t count = 0;
        for (std::size_t pos = 0; (pos = source.find(needle, pos)) != std::string::npos; pos += needle.size())
            ++count;
        return count;
    };

    const std::string* global = findSource("overrides/global.lua");
    const std::string* api = findSource("overrides/api.lua");
    const std::string* inventory = findSource("overrides/inventory.lua");
    ASSERT_NE(global, nullptr);
    ASSERT_NE(api, nullptr);
    ASSERT_NE(inventory, nullptr);
    EXPECT_NE(global->find("requestBarterSources"), std::string::npos);
    EXPECT_NE(global->find("IE_BarterAuthorityReady"), std::string::npos);
    EXPECT_NE(global->find("types.Actor.objectIsInstance(props.destination) and types.Actor.isDead(props.destination)"), std::string::npos);
    EXPECT_NE(global->find("authoritativePutDestination"), std::string::npos);
    EXPECT_NE(global->find("resolveCurrentCorpseItem"), std::string::npos);
    EXPECT_NE(global->find("AUTHORITATIVE_CURSOR_RESOLVE_ATTEMPTS = 8"), std::string::npos);
    EXPECT_NE(global->find("local function resolveAuthoritativeCursor"), std::string::npos);
    EXPECT_EQ(countOccurrences(*global, "resolveAuthoritativeCursor({"), 3u);
    EXPECT_EQ(countOccurrences(*global, "requestWithSound"), 3u);
    EXPECT_NE(global->find("local detachedSelfDrag = false"), std::string::npos);
    EXPECT_NE(global->find("cursor-drag detached-self-split"), std::string::npos);
    EXPECT_NE(global->find("cursor-drag direct-self-full-stack"), std::string::npos);
    EXPECT_NE(global->find("Do not refresh the inventory UI before cursor attachment"), std::string::npos);
    EXPECT_NE(global->find("preserveObject = detachedSelfDrag"), std::string::npos);
    EXPECT_NE(api->find("helpers.isGold(props.obj) and not props.preserveObject"), std::string::npos);
    EXPECT_NE(global->find("[MPINVTRACE] InventoryExtender global"), std::string::npos);
    const auto preShowGate = api->find("if windowName == 'Trade'");
    const auto showTradeWindow = api->find("windowManager:show(windowName, arg)");
    EXPECT_NE(preShowGate, std::string::npos);
    EXPECT_NE(showTradeWindow, std::string::npos);
    EXPECT_LT(preShowGate, showTradeWindow);
    EXPECT_NE(api->find("barterAuthorityReady = false"), std::string::npos);
    EXPECT_NE(api->find("barterAuthoritySources = props and props.sources or nil"), std::string::npos);
    EXPECT_NE(api->find("[MPINVTRACE] InventoryExtender cursor-event receive"), std::string::npos);
    EXPECT_NE(api->find("windowManager.ctx.dragAndDrop:setDraggingObject(obj, props.resetMode)"), std::string::npos);
    EXPECT_NE(inventory->find("barterAuthorityReady == false"), std::string::npos);
    EXPECT_NE(inventory->find("getMerchantItemsForTrade"), std::string::npos);
    EXPECT_NE(inventory->find("barterAuthoritySources"), std::string::npos);
    EXPECT_NE(inventory->find("npc.type.hasEquipped"), std::string::npos);
    EXPECT_NE(inventory->find("virtualStack.equipped == equipped"), std::string::npos);
    EXPECT_NE(inventory->find("local multiplayerRemotePickup = mp.isConnected()"), std::string::npos);
    EXPECT_NE(inventory->find("and self.type == mode"), std::string::npos);
    EXPECT_NE(inventory->find("and not multiplayerRemotePickup"), std::string::npos);
    EXPECT_NE(inventory->find("local transfer = input.isAltPressed() or I.UI.getMode() == 'Barter'"), std::string::npos);
    EXPECT_NE(inventory->find("ctx.dragAndDrop:startDrag("), std::string::npos);
    EXPECT_NE(inventory->find("ctx.dragAndDrop:transferInto("), std::string::npos);
    EXPECT_NE(inventory->find("[MPINVTRACE] InventoryExtender UI"), std::string::npos);
}

TEST(ServerLuaPackageRegistry, ShippedArrowStickUsesAuthoritativeProjectileRecovery)
{
    const auto root = std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR }
        / "files" / "server" / "server-lua-packages";
    mwmp::ServerLuaPackageRegistry registry(root, 141);
    const auto packageIt = std::find_if(registry.packageSet().packages.begin(), registry.packageSet().packages.end(),
        [](const auto& package) { return package.packageId == "fetcher.arrowstick-mp-fix"; });
    ASSERT_NE(packageIt, registry.packageSet().packages.end());

    const auto& package = *packageIt;
    EXPECT_EQ(package.packageVersion, 2u);
    EXPECT_EQ(package.requiredMultiplayerLuaApi, 8u);
    const auto sourceIt = std::find_if(package.files.begin(), package.files.end(),
        [](const auto& file) { return file.path == "overrides/global.lua"; });
    ASSERT_NE(sourceIt, package.files.end());
    const std::string& source = sourceIt->source;
    EXPECT_NE(source.find("require('openmw.mp')"), std::string::npos);
    EXPECT_NE(source.find("require('openmw.animation')"), std::string::npos);
    const auto recovery = source.find("mp.worldProjectileRecover.request");
    const auto localFallback = source.find("world.createObject(data.projectile)");
    EXPECT_NE(recovery, std::string::npos);
    EXPECT_NE(localFallback, std::string::npos);
    EXPECT_LT(recovery, localFallback);
    EXPECT_NE(source.find("presentationTransform or data.transform"), std::string::npos);
    EXPECT_NE(source.find("data.presentationRefined and 1 or 0.15"), std::string::npos);
}

TEST(ServerLuaPackageRegistry, ShippedSetBonusFixUsesAuthoritativeSpellRecords)
{
    const auto root = std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR }
        / "files" / "server" / "server-lua-packages";
    mwmp::ServerLuaPackageRegistry registry(root, 141);
    const auto packageIt = std::find_if(registry.packageSet().packages.begin(), registry.packageSet().packages.end(),
        [](const auto& package) { return package.packageId == "fetcher.setbonus-mp-fix"; });
    ASSERT_NE(packageIt, registry.packageSet().packages.end());

    const auto& package = *packageIt;
    EXPECT_EQ(package.packageVersion, 1u);
    EXPECT_LE(package.requiredMultiplayerLuaApi, mwmp::serverlua::MultiplayerLuaApiVersion);
    ASSERT_EQ(package.overrides.size(), 1u);
    EXPECT_EQ(package.overrides.front().target, "scripts/setbonus/global.lua");

    const auto sourceIt = std::find_if(package.files.begin(), package.files.end(),
        [](const auto& file) { return file.path == "overrides/global.lua"; });
    ASSERT_NE(sourceIt, package.files.end());
    EXPECT_NE(sourceIt->source.find("require('openmw.mp')"), std::string::npos);
    EXPECT_NE(sourceIt->source.find("mp.records.request"), std::string::npos);
    EXPECT_NE(sourceIt->source.find("type = 'spell'"), std::string::npos);
    EXPECT_NE(sourceIt->source.find("SCALE_REBUILD_DEBOUNCE"), std::string::npos);
}
