#ifndef OPENMW_SERVER_SERVERCONTENTREGISTRY_HPP
#define OPENMW_SERVER_SERVERCONTENTREGISTRY_HPP

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace MWWorld
{
    class ESMStore;
}

namespace mwmp
{
    /// Immutable content environment used by the dedicated server. It loads
    /// the same ordered content and OpenMW load scripts as a client, but never
    /// initializes a window, renderer, physics scene, sound system, or GUI.
    class ServerContentRegistry
    {
    public:
        struct Config
        {
            std::filesystem::path openmwConfig;
            std::filesystem::path resources;
            std::string encoding = "win1252";
            bool verifyDeterminism = true;
        };

        struct ManifestEntry
        {
            std::string filename;
            std::string sha256;

            bool operator==(const ManifestEntry&) const = default;
        };

        explicit ServerContentRegistry(Config config);
        ~ServerContentRegistry();

        ServerContentRegistry(const ServerContentRegistry&) = delete;
        ServerContentRegistry& operator=(const ServerContentRegistry&) = delete;

        const std::string& resolvedFingerprint() const { return mResolvedFingerprint; }
        const std::vector<ManifestEntry>& contentFiles() const { return mContentFiles; }
        const std::vector<ManifestEntry>& luaScripts() const { return mLuaScripts; }

        const MWWorld::ESMStore& store() const;
        bool hasContentId(std::string_view id) const;
        bool hasAsset(std::string_view path) const;

        static std::string orderedManifestFingerprint(const std::vector<ManifestEntry>& entries);

    private:
        struct Runtime;
        struct Snapshot;

        static std::unique_ptr<Runtime> loadRuntime(const Config& config);
        static Snapshot snapshot(const Runtime& runtime);

        Config mConfig;
        std::unique_ptr<Runtime> mRuntime;
        std::string mResolvedFingerprint;
        std::vector<ManifestEntry> mContentFiles;
        std::vector<ManifestEntry> mLuaScripts;
    };
}

#endif
