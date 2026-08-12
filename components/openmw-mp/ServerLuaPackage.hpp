#ifndef OPENMW_MP_SERVERLUAPACKAGE_HPP
#define OPENMW_MP_SERVERLUAPACKAGE_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mwmp::serverlua
{
    inline constexpr std::uint16_t ServerLuaPackageManifestVersion = 2;
    inline constexpr std::uint16_t MultiplayerLuaApiVersion = 1;

    inline constexpr std::size_t MaxPackages = 64;
    inline constexpr std::size_t MaxFilesPerPackage = 256;
    inline constexpr std::size_t MaxRegistrationsPerPackage = 128;
    inline constexpr std::size_t MaxOverridesPerPackage = 128;
    inline constexpr std::size_t MaxAcceptedBaseHashesPerOverride = 32;
    inline constexpr std::size_t MaxDependenciesPerPackage = 64;
    inline constexpr std::size_t MaxPackageIdLength = 96;
    inline constexpr std::size_t MaxRelativePathLength = 192;
    inline constexpr std::size_t MaxFileSize = 4 * 1024 * 1024;
    inline constexpr std::size_t MaxPackageSize = 16 * 1024 * 1024;
    inline constexpr std::size_t MaxPackageSetSize = 64 * 1024 * 1024;
    inline constexpr std::size_t MaxChunkSize = 48 * 1024;

    enum ScriptFlags : std::uint32_t
    {
        ScriptGlobal = 1u << 0,
        ScriptCustom = 1u << 1,
        ScriptPlayer = 1u << 2,
    };

    struct File
    {
        std::string path;
        std::uint32_t sourceSize = 0;
        std::string sourceHash;
        std::string source;

        bool operator==(const File&) const = default;
    };

    struct ScriptRegistration
    {
        std::string path;
        std::uint32_t flags = 0;

        bool operator==(const ScriptRegistration&) const = default;
    };

    enum class OverrideBasePolicy : std::uint8_t
    {
        AcceptedHashes = 0,
        Any = 1,
    };

    struct CompatibilityOverride
    {
        std::string target;
        std::string source;
        OverrideBasePolicy basePolicy = OverrideBasePolicy::AcceptedHashes;
        std::vector<std::string> acceptedBaseHashes;

        bool operator==(const CompatibilityOverride&) const = default;
    };

    struct Package
    {
        std::uint16_t manifestVersion = ServerLuaPackageManifestVersion;
        std::string packageId;
        std::uint64_t packageVersion = 0;
        std::uint32_t requiredOpenMWLuaApi = 0;
        std::uint16_t requiredMultiplayerLuaApi = MultiplayerLuaApiVersion;
        std::vector<std::string> dependencies;
        std::vector<File> files;
        std::vector<ScriptRegistration> registrations;
        std::vector<CompatibilityOverride> overrides;
        std::string packageHash;

        bool operator==(const Package&) const = default;
    };

    struct PackageSet
    {
        std::uint16_t manifestVersion = ServerLuaPackageManifestVersion;
        std::uint64_t generation = 0;
        std::string packageSetHash;
        std::vector<Package> packages;

        bool operator==(const PackageSet&) const = default;
    };

    struct ValidationError
    {
        std::string code;
        std::string path;
        std::string message;

        bool operator==(const ValidationError&) const = default;
    };

    struct OverrideBaseValidation
    {
        std::string baseHash;
        std::optional<ValidationError> error;

        explicit operator bool() const { return !error.has_value(); }
    };

    std::string normalizePackageId(std::string_view value);
    std::string normalizeRelativePath(std::string_view value);
    std::string virtualPath(std::string_view packageId, std::string_view relativePath);

    /// Canonicalizes identity, paths, ordering and source hashes. Existing
    /// non-empty hashes are retained so validation can diagnose substitutions.
    void canonicalize(Package& package);

    std::string hashPackage(const Package& package);
    std::string hashPackageSet(const PackageSet& packageSet);
    std::uint64_t generationFromHash(std::string_view packageSetHash);

    std::vector<ValidationError> validatePackage(const Package& package,
        std::uint32_t supportedOpenMWLuaApi, std::uint16_t supportedMultiplayerLuaApi,
        bool requireSources = true);
    std::vector<ValidationError> validatePackageSet(const PackageSet& packageSet,
        std::uint32_t supportedOpenMWLuaApi, std::uint16_t supportedMultiplayerLuaApi,
        bool requireSources = true);
    OverrideBaseValidation validateOverrideBase(const CompatibilityOverride& override,
        std::optional<std::string_view> baseSource, bool targetAlreadyLoaded);
}

#endif
