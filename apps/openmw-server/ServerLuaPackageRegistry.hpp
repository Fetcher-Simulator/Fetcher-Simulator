#ifndef OPENMW_SERVER_SERVERLUAPACKAGEREGISTRY_HPP
#define OPENMW_SERVER_SERVERLUAPACKAGEREGISTRY_HPP

#include <components/openmw-mp/ServerLuaPackage.hpp>

#include <filesystem>

namespace mwmp
{
    /// Immutable server-startup snapshot of OpenMW Lua packages selected for
    /// distribution to multiplayer clients.
    class ServerLuaPackageRegistry
    {
    public:
        ServerLuaPackageRegistry(std::filesystem::path root, std::uint32_t openMWLuaApiVersion);

        const std::filesystem::path& root() const { return mRoot; }
        const serverlua::PackageSet& packageSet() const { return mPackageSet; }

    private:
        static serverlua::Package loadPackage(
            const std::filesystem::path& directory, std::uint32_t openMWLuaApiVersion);

        std::filesystem::path mRoot;
        serverlua::PackageSet mPackageSet;
    };
}

#endif
