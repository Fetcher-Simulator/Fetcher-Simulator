#ifndef OPENMW_SERVER_SERVERCONTENTREGISTRY_HPP
#define OPENMW_SERVER_SERVERCONTENTREGISTRY_HPP

#include <filesystem>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <components/openmw-mp/Base/BaseStructs.hpp>
#include <components/openmw-mp/WorldItemTake.hpp>

namespace MWWorld
{
    class ESMStore;
    class WorldModel;
}
namespace Resource
{
    class ResourceSystem;
}
namespace mwmp::records
{
    struct DynamicRecordDefinition;
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

        struct PlacedItemReference
        {
            PlacedObjectIdentity identity;
            Position position;
            std::int32_t worldCount = 0;
            std::int32_t inventoryCount = 0;
            std::int32_t charge = -1;
            float enchantmentCharge = -1.f;
            std::string soul;
            std::string ownerId;
            std::string factionId;
            std::int32_t factionRank = -1;
            bool ownershipGlobalAllowsUse = false;
            std::int32_t itemValue = 0;
            bool gold = false;
            bool enabled = false;
        };

        struct ContainerReference
        {
            Position position;
            std::string ownerId;
            std::string factionId;
            std::int32_t factionRank = -1;
            bool ownershipGlobalAllowsUse = false;
            bool enabled = false;
        };

        explicit ServerContentRegistry(Config config);
        ~ServerContentRegistry();

        ServerContentRegistry(const ServerContentRegistry&) = delete;
        ServerContentRegistry& operator=(const ServerContentRegistry&) = delete;

        const std::string& resolvedFingerprint() const { return mResolvedFingerprint; }
        const std::vector<ManifestEntry>& contentFiles() const { return mContentFiles; }
        const std::vector<ManifestEntry>& luaScripts() const { return mLuaScripts; }

        const MWWorld::ESMStore& store() const;
        Resource::ResourceSystem& resourceSystem() const;
        MWWorld::WorldModel& worldModel() const;
        bool hasContentId(std::string_view id) const;
        bool hasStaticNpcRecord(std::string_view id) const;
        bool hasAsset(std::string_view path) const;
        bool hasModel(std::string_view path) const;
        bool hasIcon(std::string_view path) const;
        bool hasStaticRecord(std::uint8_t recordType, std::string_view id) const;
        bool validateScriptSource(std::string_view id, std::string_view source) const;
        std::optional<PlacedItemReference> findPlacedItemReference(
            const PlacedObjectIdentity& identity) const;
        std::optional<ContainerReference> findContainerReference(
            std::string_view cellId, std::string_view refId, std::uint32_t refIndex) const;
        void installRuntimeDefinition(
            std::string_view id, const records::DynamicRecordDefinition& definition);

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
