#include "ServerLuaPackage.hpp"

#include "Sha256.hpp"

#include <algorithm>
#include <charconv>
#include <functional>
#include <map>
#include <set>
#include <unordered_map>

namespace mwmp::serverlua
{
    namespace
    {
        bool isValidUtf8(std::string_view value)
        {
            for (std::size_t i = 0; i < value.size();)
            {
                const auto first = static_cast<unsigned char>(value[i]);
                std::size_t count = 0;
                std::uint32_t codepoint = 0;
                if (first <= 0x7f)
                {
                    ++i;
                    continue;
                }
                if ((first & 0xe0) == 0xc0)
                {
                    count = 1;
                    codepoint = first & 0x1f;
                }
                else if ((first & 0xf0) == 0xe0)
                {
                    count = 2;
                    codepoint = first & 0x0f;
                }
                else if ((first & 0xf8) == 0xf0)
                {
                    count = 3;
                    codepoint = first & 0x07;
                }
                else
                    return false;
                if (i + count >= value.size())
                    return false;
                for (std::size_t offset = 1; offset <= count; ++offset)
                {
                    const auto next = static_cast<unsigned char>(value[i + offset]);
                    if ((next & 0xc0) != 0x80)
                        return false;
                    codepoint = (codepoint << 6) | (next & 0x3f);
                }
                if ((count == 1 && codepoint < 0x80) || (count == 2 && codepoint < 0x800)
                    || (count == 3 && codepoint < 0x10000) || codepoint > 0x10ffff
                    || (codepoint >= 0xd800 && codepoint <= 0xdfff))
                    return false;
                i += count + 1;
            }
            return true;
        }

        bool isHexHash(std::string_view value)
        {
            return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            });
        }

        void appendU16(std::string& out, std::uint16_t value)
        {
            out.push_back(static_cast<char>(value));
            out.push_back(static_cast<char>(value >> 8));
        }

        void appendU32(std::string& out, std::uint32_t value)
        {
            for (int shift = 0; shift < 32; shift += 8)
                out.push_back(static_cast<char>(value >> shift));
        }

        void appendU64(std::string& out, std::uint64_t value)
        {
            for (int shift = 0; shift < 64; shift += 8)
                out.push_back(static_cast<char>(value >> shift));
        }

        void appendString(std::string& out, std::string_view value)
        {
            appendU32(out, static_cast<std::uint32_t>(value.size()));
            out.append(value);
        }

        void add(std::vector<ValidationError>& errors, std::string code, std::string path, std::string message)
        {
            errors.push_back({ std::move(code), std::move(path), std::move(message) });
        }

        bool hasTraversalComponent(std::string_view path)
        {
            std::size_t start = 0;
            while (start <= path.size())
            {
                const std::size_t end = path.find('/', start);
                const std::string_view component = path.substr(start, end - start);
                if (component.empty() || component == "." || component == "..")
                    return true;
                if (end == std::string_view::npos)
                    break;
                start = end + 1;
            }
            return false;
        }

        bool isSafeOverrideTarget(std::string_view path)
        {
            return !path.empty() && path.size() <= MaxRelativePathLength && isValidUtf8(path)
                && path.starts_with("scripts/") && !path.starts_with("scripts/omw/")
                && !path.starts_with("scripts/multiplayer/") && !path.starts_with('/')
                && path.find(':') == std::string_view::npos && !hasTraversalComponent(path)
                && path.ends_with(".lua");
        }

        bool isPackageId(std::string_view value)
        {
            if (value.empty() || value.front() == '.' || value.back() == '.' || value.find("..") != std::string_view::npos)
                return false;
            return std::all_of(value.begin(), value.end(), [](unsigned char c) {
                return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
            });
        }

        Package canonicalCopy(const Package& input)
        {
            Package result = input;
            canonicalize(result);
            return result;
        }
    }

    std::string normalizePackageId(std::string_view value)
    {
        std::string result(value);
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c);
        });
        return result;
    }

    std::string normalizeRelativePath(std::string_view value)
    {
        std::string result(value);
        std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
            if (c == '\\')
                return '/';
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c);
        });
        return result;
    }

    std::string virtualPath(std::string_view packageId, std::string_view relativePath)
    {
        std::string packagePath = normalizePackageId(packageId);
        std::replace(packagePath.begin(), packagePath.end(), '.', '/');
        return "scripts/multiplayer/" + packagePath + "/" + normalizeRelativePath(relativePath);
    }

    void canonicalize(Package& package)
    {
        package.packageId = normalizePackageId(package.packageId);
        for (std::string& dependency : package.dependencies)
            dependency = normalizePackageId(dependency);
        std::sort(package.dependencies.begin(), package.dependencies.end());
        for (File& file : package.files)
        {
            file.path = normalizeRelativePath(file.path);
            if (file.sourceSize == 0 && !file.source.empty())
                file.sourceSize = static_cast<std::uint32_t>(file.source.size());
            if (file.sourceHash.empty())
                file.sourceHash = crypto::sha256hex(file.source);
            else
                file.sourceHash = normalizePackageId(file.sourceHash);
        }
        std::sort(package.files.begin(), package.files.end(), [](const File& left, const File& right) {
            return left.path < right.path;
        });
        for (ScriptRegistration& registration : package.registrations)
            registration.path = normalizeRelativePath(registration.path);
        std::sort(package.registrations.begin(), package.registrations.end(),
            [](const ScriptRegistration& left, const ScriptRegistration& right) {
                return std::tie(left.path, left.flags) < std::tie(right.path, right.flags);
            });
        for (CompatibilityOverride& override : package.overrides)
        {
            override.target = normalizeRelativePath(override.target);
            override.source = normalizeRelativePath(override.source);
            for (std::string& hash : override.acceptedBaseHashes)
                hash = normalizePackageId(hash);
            std::sort(override.acceptedBaseHashes.begin(), override.acceptedBaseHashes.end());
        }
        std::sort(package.overrides.begin(), package.overrides.end(),
            [](const CompatibilityOverride& left, const CompatibilityOverride& right) {
                return std::tie(left.target, left.source, left.basePolicy, left.acceptedBaseHashes)
                    < std::tie(right.target, right.source, right.basePolicy, right.acceptedBaseHashes);
            });
        package.packageHash = normalizePackageId(package.packageHash);
    }

    std::string hashPackage(const Package& input)
    {
        Package package = canonicalCopy(input);
        std::string canonical("OMLP", 4);
        appendU16(canonical, package.manifestVersion);
        appendString(canonical, package.packageId);
        appendU64(canonical, package.packageVersion);
        appendU32(canonical, package.requiredOpenMWLuaApi);
        appendU16(canonical, package.requiredMultiplayerLuaApi);
        appendU32(canonical, static_cast<std::uint32_t>(package.dependencies.size()));
        for (const std::string& dependency : package.dependencies)
            appendString(canonical, dependency);
        appendU32(canonical, static_cast<std::uint32_t>(package.files.size()));
        for (const File& file : package.files)
        {
            appendString(canonical, file.path);
            appendU32(canonical, file.sourceSize);
            appendString(canonical, file.sourceHash);
            appendString(canonical, file.source);
        }
        appendU32(canonical, static_cast<std::uint32_t>(package.registrations.size()));
        for (const ScriptRegistration& registration : package.registrations)
        {
            appendString(canonical, registration.path);
            appendU32(canonical, registration.flags);
        }
        appendU32(canonical, static_cast<std::uint32_t>(package.overrides.size()));
        for (const CompatibilityOverride& override : package.overrides)
        {
            appendString(canonical, override.target);
            appendString(canonical, override.source);
            canonical.push_back(static_cast<char>(override.basePolicy));
            appendU32(canonical, static_cast<std::uint32_t>(override.acceptedBaseHashes.size()));
            for (const std::string& hash : override.acceptedBaseHashes)
                appendString(canonical, hash);
        }
        return crypto::sha256hex(canonical);
    }

    std::string hashPackageSet(const PackageSet& input)
    {
        std::vector<Package> packages = input.packages;
        for (Package& package : packages)
        {
            canonicalize(package);
            if (package.packageHash.empty())
                package.packageHash = hashPackage(package);
        }
        std::sort(packages.begin(), packages.end(), [](const Package& left, const Package& right) {
            return left.packageId < right.packageId;
        });
        std::string canonical("OMLS", 4);
        appendU16(canonical, input.manifestVersion);
        appendU32(canonical, static_cast<std::uint32_t>(packages.size()));
        for (const Package& package : packages)
        {
            appendString(canonical, package.packageId);
            appendU64(canonical, package.packageVersion);
            appendString(canonical, package.packageHash);
        }
        return crypto::sha256hex(canonical);
    }

    std::uint64_t generationFromHash(std::string_view hash)
    {
        if (!isHexHash(hash))
            return 0;
        std::uint64_t result = 0;
        const auto conversion = std::from_chars(hash.data(), hash.data() + 16, result, 16);
        return conversion.ec == std::errc{} ? result : 0;
    }

    std::vector<ValidationError> validatePackage(const Package& input, std::uint32_t supportedOpenMWLuaApi,
        std::uint16_t supportedMultiplayerLuaApi, bool requireSources)
    {
        Package package = canonicalCopy(input);
        std::vector<ValidationError> errors;
        if (package.manifestVersion != ServerLuaPackageManifestVersion)
            add(errors, "unsupported_manifest_version", "manifestVersion", "Unsupported server Lua manifest version");
        if (package.packageId.size() > MaxPackageIdLength || !isValidUtf8(package.packageId)
            || !isPackageId(package.packageId))
            add(errors, "invalid_package_id", "packageId", "Package ID is not canonical or exceeds its limit");
        if (package.packageVersion == 0)
            add(errors, "invalid_package_version", "packageVersion", "Package version must be non-zero");
        if (package.requiredOpenMWLuaApi != supportedOpenMWLuaApi)
            add(errors, "unsupported_openmw_lua_api", "requiredOpenMWLuaApi", "Required OpenMW Lua API is unsupported");
        if (package.requiredMultiplayerLuaApi > supportedMultiplayerLuaApi)
            add(errors, "unsupported_multiplayer_lua_api", "requiredMultiplayerLuaApi",
                "Required multiplayer Lua API is unsupported");
        if (package.dependencies.size() > MaxDependenciesPerPackage)
            add(errors, "too_many_dependencies", "dependencies", "Package dependency count exceeds its limit");
        std::set<std::string> dependencies;
        for (std::size_t i = 0; i < package.dependencies.size(); ++i)
        {
            const std::string& dependency = package.dependencies[i];
            const std::string path = "dependencies[" + std::to_string(i) + "]";
            if (dependency.size() > MaxPackageIdLength || !isPackageId(dependency))
                add(errors, "invalid_dependency", path, "Dependency package ID is invalid");
            if (dependency == package.packageId)
                add(errors, "self_dependency", path, "Package cannot depend on itself");
            if (!dependencies.insert(dependency).second)
                add(errors, "duplicate_dependency", path, "Duplicate package dependency");
        }

        if (package.files.empty() || package.files.size() > MaxFilesPerPackage)
            add(errors, "invalid_file_count", "files", "Package must contain a bounded non-empty file list");
        std::set<std::string> paths;
        std::size_t packageSize = 0;
        for (std::size_t i = 0; i < package.files.size(); ++i)
        {
            const File& file = package.files[i];
            const std::string path = "files[" + std::to_string(i) + "]";
            if (file.path.empty() || file.path.size() > MaxRelativePathLength || !isValidUtf8(file.path)
                || file.path.starts_with('/') || file.path.find(':') != std::string::npos
                || hasTraversalComponent(file.path) || !file.path.ends_with(".lua"))
                add(errors, "invalid_path", path + ".path", "Lua path is invalid, unsafe, or outside v1 scope");
            if (!paths.insert(file.path).second)
                add(errors, "duplicate_path", path + ".path", "Duplicate normalized Lua path");
            if (file.source.size() > MaxFileSize)
                add(errors, "file_too_large", path + ".source", "Lua source exceeds the per-file limit");
            if (file.sourceSize > MaxFileSize)
                add(errors, "file_too_large", path + ".sourceSize", "Declared Lua source size exceeds the limit");
            packageSize += requireSources ? file.source.size() : file.sourceSize;
            if (requireSources && (file.source.empty() || !isValidUtf8(file.source)
                    || file.source.find('\0') != std::string::npos))
                add(errors, "invalid_source", path + ".source", "Lua source must be non-empty valid UTF-8 without NUL");
            if (requireSources && file.sourceSize != file.source.size())
                add(errors, "wrong_file_size", path + ".sourceSize", "Declared Lua source size does not match source");
            if (!isHexHash(file.sourceHash))
                add(errors, "invalid_file_hash", path + ".sourceHash", "File SHA-256 is malformed");
            else if (requireSources && crypto::sha256hex(file.source) != file.sourceHash)
                add(errors, "wrong_file_hash", path + ".sourceHash", "File SHA-256 does not match its source");
        }
        if (packageSize > MaxPackageSize)
            add(errors, "package_too_large", "files", "Package source bytes exceed the package limit");

        if ((package.registrations.empty() && package.overrides.empty())
            || package.registrations.size() > MaxRegistrationsPerPackage)
            add(errors, "invalid_registration_count", "registrations",
                "Package must contain at least one script registration or compatibility override");
        std::set<std::string> registeredPaths;
        constexpr std::uint32_t allowedFlags = ScriptGlobal | ScriptCustom | ScriptPlayer;
        for (std::size_t i = 0; i < package.registrations.size(); ++i)
        {
            const ScriptRegistration& registration = package.registrations[i];
            const std::string path = "registrations[" + std::to_string(i) + "]";
            if (!paths.contains(registration.path))
                add(errors, "unknown_registration_path", path + ".path", "Registered script is absent from files");
            if (!registeredPaths.insert(registration.path).second)
                add(errors, "duplicate_registration", path + ".path", "Script path is registered more than once");
            if (registration.flags == 0 || (registration.flags & ~allowedFlags) != 0
                || ((registration.flags & ScriptGlobal) && registration.flags != ScriptGlobal))
                add(errors, "invalid_script_flags", path + ".flags", "Script registration flags are unsupported");
        }

        if (package.overrides.size() > MaxOverridesPerPackage)
            add(errors, "too_many_overrides", "overrides", "Compatibility override count exceeds its limit");
        std::set<std::string> overrideTargets;
        for (std::size_t i = 0; i < package.overrides.size(); ++i)
        {
            const CompatibilityOverride& override = package.overrides[i];
            const std::string path = "overrides[" + std::to_string(i) + "]";
            if (!isSafeOverrideTarget(override.target))
                add(errors, "override_target_forbidden", path + ".target",
                    "Override target must be an existing third-party scripts/*.lua VFS path");
            if (!overrideTargets.insert(override.target).second)
                add(errors, "duplicate_override_target", path + ".target",
                    "Compatibility override target is declared more than once");
            if (!paths.contains(override.source))
                add(errors, "override_source_missing", path + ".source",
                    "Compatibility override source is absent from package files");
            if (override.acceptedBaseHashes.size() > MaxAcceptedBaseHashesPerOverride)
                add(errors, "too_many_base_hashes", path + ".acceptedBaseHashes",
                    "Accepted base hash count exceeds its limit");
            std::set<std::string> acceptedHashes;
            for (std::size_t hashIndex = 0; hashIndex < override.acceptedBaseHashes.size(); ++hashIndex)
            {
                const std::string& hash = override.acceptedBaseHashes[hashIndex];
                if (!isHexHash(hash))
                    add(errors, "invalid_base_hash",
                        path + ".acceptedBaseHashes[" + std::to_string(hashIndex) + "]",
                        "Accepted base SHA-256 is malformed");
                if (!acceptedHashes.insert(hash).second)
                    add(errors, "duplicate_base_hash", path + ".acceptedBaseHashes",
                        "Accepted base SHA-256 is declared more than once");
            }
            if (override.basePolicy == OverrideBasePolicy::AcceptedHashes
                && override.acceptedBaseHashes.empty())
                add(errors, "missing_base_hash_policy", path,
                    "Compatibility override must declare acceptedBaseHashes or explicit basePolicy:any");
            else if (override.basePolicy == OverrideBasePolicy::Any
                && !override.acceptedBaseHashes.empty())
                add(errors, "conflicting_base_hash_policy", path,
                    "basePolicy:any cannot be combined with acceptedBaseHashes");
            else if (override.basePolicy != OverrideBasePolicy::AcceptedHashes
                && override.basePolicy != OverrideBasePolicy::Any)
                add(errors, "invalid_base_policy", path + ".basePolicy", "Compatibility override base policy is invalid");
        }

        if (!input.packageHash.empty())
        {
            if (!isHexHash(package.packageHash))
                add(errors, "invalid_package_hash", "packageHash", "Package SHA-256 is malformed");
            else if (requireSources && hashPackage(package) != package.packageHash)
                add(errors, "wrong_package_hash", "packageHash", "Package SHA-256 does not match canonical content");
        }
        return errors;
    }

    std::vector<ValidationError> validatePackageSet(const PackageSet& input,
        std::uint32_t supportedOpenMWLuaApi, std::uint16_t supportedMultiplayerLuaApi, bool requireSources)
    {
        std::vector<ValidationError> errors;
        if (input.manifestVersion != ServerLuaPackageManifestVersion)
            add(errors, "unsupported_manifest_version", "manifestVersion", "Unsupported package-set version");
        if (input.packages.size() > MaxPackages)
            add(errors, "too_many_packages", "packages", "Package count exceeds its limit");
        std::unordered_map<std::string, std::size_t> indexes;
        std::size_t totalSize = 0;
        for (std::size_t i = 0; i < input.packages.size(); ++i)
        {
            Package package = canonicalCopy(input.packages[i]);
            const std::string prefix = "packages[" + std::to_string(i) + "].";
            for (ValidationError error : validatePackage(
                     package, supportedOpenMWLuaApi, supportedMultiplayerLuaApi, requireSources))
            {
                error.path = prefix + error.path;
                errors.push_back(std::move(error));
            }
            if (!indexes.emplace(package.packageId, i).second)
                add(errors, "duplicate_package", prefix + "packageId", "Duplicate canonical package ID");
            for (const File& file : package.files)
                totalSize += requireSources ? file.source.size() : file.sourceSize;
        }

        std::map<std::string, std::size_t, std::less<>> overrideOwners;
        for (std::size_t i = 0; i < input.packages.size(); ++i)
        {
            Package package = canonicalCopy(input.packages[i]);
            for (const CompatibilityOverride& override : package.overrides)
            {
                if (!overrideOwners.emplace(override.target, i).second)
                    add(errors, "duplicate_override_target", "packages[" + std::to_string(i) + "].overrides",
                        "Only one package may own a compatibility override target");
            }
        }
        if (totalSize > MaxPackageSetSize)
            add(errors, "package_set_too_large", "packages", "Package-set source bytes exceed the session limit");

        for (std::size_t i = 0; i < input.packages.size(); ++i)
        {
            Package package = canonicalCopy(input.packages[i]);
            for (const std::string& dependency : package.dependencies)
            {
                if (!indexes.contains(dependency))
                    add(errors, "missing_dependency", "packages[" + std::to_string(i) + "].dependencies",
                        "Required package is absent from the package set");
            }
        }

        enum class Visit : std::uint8_t { New, Active, Done };
        std::vector<Visit> visits(input.packages.size());
        std::function<bool(std::size_t)> visit = [&](std::size_t index) {
            if (visits[index] == Visit::Active)
                return false;
            if (visits[index] == Visit::Done)
                return true;
            visits[index] = Visit::Active;
            Package package = canonicalCopy(input.packages[index]);
            for (const std::string& dependency : package.dependencies)
            {
                const auto found = indexes.find(dependency);
                if (found != indexes.end() && !visit(found->second))
                    return false;
            }
            visits[index] = Visit::Done;
            return true;
        };
        for (std::size_t i = 0; i < input.packages.size(); ++i)
        {
            if (!visit(i))
            {
                add(errors, "dependency_cycle", "packages", "Package dependency graph contains a cycle");
                break;
            }
        }

        const std::string computedHash = hashPackageSet(input);
        if (!input.packageSetHash.empty() && input.packageSetHash != computedHash)
            add(errors, "wrong_package_set_hash", "packageSetHash", "Package-set SHA-256 is not canonical");
        const std::uint64_t computedGeneration = generationFromHash(computedHash);
        if (input.generation != 0 && input.generation != computedGeneration)
            add(errors, "wrong_generation", "generation", "Package-set generation does not match its hash");
        return errors;
    }

    OverrideBaseValidation validateOverrideBase(const CompatibilityOverride& override,
        std::optional<std::string_view> baseSource, bool targetAlreadyLoaded)
    {
        OverrideBaseValidation result;
        if (!baseSource)
        {
            result.error = ValidationError{ "override_target_missing", override.target,
                "Compatibility override target is absent from the base VFS" };
            return result;
        }

        result.baseHash = crypto::sha256hex(*baseSource);
        if (targetAlreadyLoaded)
        {
            result.error = ValidationError{ "override_target_already_loaded", override.target,
                "Compatibility override target was loaded before package activation" };
            return result;
        }
        if (override.basePolicy == OverrideBasePolicy::AcceptedHashes)
        {
            if (std::find(override.acceptedBaseHashes.begin(), override.acceptedBaseHashes.end(), result.baseHash)
                == override.acceptedBaseHashes.end())
            {
                result.error = ValidationError{ "override_base_hash_mismatch", override.target,
                    "Installed base source hash is not accepted by this compatibility override" };
            }
        }
        else if (override.basePolicy != OverrideBasePolicy::Any)
        {
            result.error = ValidationError{ "invalid_base_policy", override.target,
                "Compatibility override base policy is invalid" };
        }
        return result;
    }
}
