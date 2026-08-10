#include "ServerLuaPackageRegistry.hpp"

#include <components/debug/debuglog.hpp>

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace mwmp
{
    namespace
    {
        template <class T>
        T requiredScalar(const YAML::Node& root, std::string_view name, const std::filesystem::path& manifest)
        {
            const YAML::Node value = root[std::string(name)];
            if (!value || !value.IsScalar())
                throw std::runtime_error(manifest.string() + ": missing scalar '" + std::string(name) + "'");
            try
            {
                return value.as<T>();
            }
            catch (const YAML::Exception& error)
            {
                throw std::runtime_error(
                    manifest.string() + ": invalid '" + std::string(name) + "': " + error.what());
            }
        }

        std::string readSource(const std::filesystem::path& packageDirectory, const std::string& canonicalPath)
        {
            const std::filesystem::path root = std::filesystem::weakly_canonical(packageDirectory);
            const std::filesystem::path candidate = std::filesystem::weakly_canonical(root / canonicalPath);
            const std::filesystem::path relative = candidate.lexically_relative(root);
            if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
                throw std::runtime_error("Package source escapes its package directory: " + canonicalPath);
            if (!std::filesystem::is_regular_file(candidate))
                throw std::runtime_error("Package source is missing or not a regular file: " + candidate.string());
            std::ifstream stream(candidate, std::ios::binary);
            if (!stream)
                throw std::runtime_error("Could not open package source: " + candidate.string());
            std::string source(std::istreambuf_iterator<char>(stream), {});
            if (stream.bad())
                throw std::runtime_error("Could not completely read package source: " + candidate.string());
            return source;
        }

        std::uint32_t parseFlags(const YAML::Node& value, const std::filesystem::path& manifest)
        {
            if (!value || !value.IsSequence())
                throw std::runtime_error(manifest.string() + ": script flags must be a sequence");
            std::uint32_t flags = 0;
            for (const YAML::Node& entry : value)
            {
                const std::string name = entry.as<std::string>();
                if (name == "global")
                    flags |= serverlua::ScriptGlobal;
                else if (name == "player")
                    flags |= serverlua::ScriptPlayer;
                else if (name == "custom")
                    flags |= serverlua::ScriptCustom;
                else
                    throw std::runtime_error(manifest.string() + ": unsupported script flag '" + name + "'");
            }
            return flags;
        }

        std::string describeErrors(const std::vector<serverlua::ValidationError>& errors)
        {
            std::ostringstream stream;
            for (std::size_t i = 0; i < errors.size(); ++i)
            {
                if (i != 0)
                    stream << "; ";
                stream << errors[i].path << " [" << errors[i].code << "]: " << errors[i].message;
            }
            return stream.str();
        }

        std::vector<serverlua::Package> dependencyOrder(std::vector<serverlua::Package> packages)
        {
            std::map<std::string, std::size_t, std::less<>> indexes;
            for (std::size_t i = 0; i < packages.size(); ++i)
                indexes.emplace(packages[i].packageId, i);
            enum class Visit : std::uint8_t { New, Active, Done };
            std::vector<Visit> visits(packages.size());
            std::vector<serverlua::Package> ordered;
            ordered.reserve(packages.size());
            std::function<void(std::size_t)> visit = [&](std::size_t index) {
                if (visits[index] == Visit::Done)
                    return;
                if (visits[index] == Visit::Active)
                    throw std::runtime_error("Server Lua package dependency cycle");
                visits[index] = Visit::Active;
                for (const std::string& dependency : packages[index].dependencies)
                    visit(indexes.at(dependency));
                visits[index] = Visit::Done;
                ordered.push_back(std::move(packages[index]));
            };
            for (const auto& [id, index] : indexes)
            {
                static_cast<void>(id);
                visit(index);
            }
            return ordered;
        }
    }

    ServerLuaPackageRegistry::ServerLuaPackageRegistry(
        std::filesystem::path root, std::uint32_t openMWLuaApiVersion)
        : mRoot(std::filesystem::absolute(std::move(root)).lexically_normal())
    {
        mPackageSet.manifestVersion = serverlua::ServerLuaPackageManifestVersion;
        if (std::filesystem::exists(mRoot))
        {
            if (!std::filesystem::is_directory(mRoot))
                throw std::runtime_error("Server Lua package root is not a directory: " + mRoot.string());
            std::vector<std::filesystem::path> packageDirectories;
            for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(mRoot))
            {
                if (entry.is_directory())
                    packageDirectories.push_back(entry.path());
            }
            std::sort(packageDirectories.begin(), packageDirectories.end());
            for (const std::filesystem::path& directory : packageDirectories)
                mPackageSet.packages.push_back(loadPackage(directory, openMWLuaApiVersion));
        }
        else
            Log(Debug::Info) << "[ServerLuaPackages] Package root does not exist; using an empty set: " << mRoot;

        const auto errors = serverlua::validatePackageSet(mPackageSet, openMWLuaApiVersion,
            serverlua::MultiplayerLuaApiVersion);
        if (!errors.empty())
            throw std::runtime_error("Invalid server Lua package set: " + describeErrors(errors));
        mPackageSet.packages = dependencyOrder(std::move(mPackageSet.packages));
        mPackageSet.packageSetHash = serverlua::hashPackageSet(mPackageSet);
        mPackageSet.generation = serverlua::generationFromHash(mPackageSet.packageSetHash);
        Log(Debug::Info) << "[ServerLuaPackages] Loaded packages=" << mPackageSet.packages.size()
                         << " generation=" << mPackageSet.generation
                         << " setHash=" << mPackageSet.packageSetHash;
    }

    serverlua::Package ServerLuaPackageRegistry::loadPackage(
        const std::filesystem::path& directory, std::uint32_t openMWLuaApiVersion)
    {
        const std::filesystem::path manifest = directory / "manifest.yaml";
        if (!std::filesystem::is_regular_file(manifest))
            throw std::runtime_error("Server Lua package directory has no manifest.yaml: " + directory.string());
        YAML::Node root;
        try
        {
            root = YAML::LoadFile(manifest.string());
        }
        catch (const YAML::Exception& error)
        {
            throw std::runtime_error("Could not parse " + manifest.string() + ": " + error.what());
        }
        if (!root.IsMap())
            throw std::runtime_error(manifest.string() + ": manifest root must be a map");

        serverlua::Package package;
        package.manifestVersion = requiredScalar<std::uint16_t>(root, "manifestVersion", manifest);
        package.packageId = requiredScalar<std::string>(root, "packageId", manifest);
        package.packageVersion = requiredScalar<std::uint64_t>(root, "packageVersion", manifest);
        package.requiredOpenMWLuaApi = requiredScalar<std::uint32_t>(root, "requiredOpenMWLuaApi", manifest);
        package.requiredMultiplayerLuaApi
            = requiredScalar<std::uint16_t>(root, "requiredMultiplayerLuaApi", manifest);

        if (const YAML::Node dependencies = root["dependencies"])
        {
            if (!dependencies.IsSequence())
                throw std::runtime_error(manifest.string() + ": dependencies must be a sequence");
            for (const YAML::Node& dependency : dependencies)
                package.dependencies.push_back(dependency.as<std::string>());
        }

        const YAML::Node files = root["files"];
        if (!files || !files.IsSequence())
            throw std::runtime_error(manifest.string() + ": files must be a sequence");
        for (const YAML::Node& entry : files)
        {
            const std::string rawPath = entry.as<std::string>();
            const std::string canonicalPath = serverlua::normalizeRelativePath(rawPath);
            if (rawPath != canonicalPath)
                throw std::runtime_error(manifest.string() + ": file paths must already be canonical: " + rawPath);
            serverlua::File file;
            file.path = canonicalPath;
            file.source = readSource(directory, canonicalPath);
            package.files.push_back(std::move(file));
        }

        const YAML::Node scripts = root["scripts"];
        if (!scripts || !scripts.IsSequence())
            throw std::runtime_error(manifest.string() + ": scripts must be a sequence");
        for (const YAML::Node& entry : scripts)
        {
            if (!entry.IsMap())
                throw std::runtime_error(manifest.string() + ": each script registration must be a map");
            serverlua::ScriptRegistration registration;
            registration.path = requiredScalar<std::string>(entry, "path", manifest);
            registration.flags = parseFlags(entry["flags"], manifest);
            package.registrations.push_back(std::move(registration));
        }

        serverlua::canonicalize(package);
        const auto errors = serverlua::validatePackage(
            package, openMWLuaApiVersion, serverlua::MultiplayerLuaApiVersion);
        if (!errors.empty())
            throw std::runtime_error("Invalid server Lua package '" + package.packageId + "': " + describeErrors(errors));
        package.packageHash = serverlua::hashPackage(package);
        return package;
    }
}
