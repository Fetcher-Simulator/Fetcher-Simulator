#include "ServerContentRegistry.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/value_semantic.hpp>
#include <boost/program_options/variables_map.hpp>

#include <components/debug/debuglog.hpp>
#include <components/fallback/fallback.hpp>
#include <components/fallback/validate.hpp>
#include <components/esm/attr.hpp>
#include <components/esm3/loadalch.hpp>
#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadbody.hpp>
#include <components/esm3/loadbook.hpp>
#include <components/esm3/loadclot.hpp>
#include <components/esm3/loadench.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadscpt.hpp>
#include <components/esm3/loadskil.hpp>
#include <components/esm3/loadspel.hpp>
#include <components/esm3/loadweap.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/files/collections.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/openmw-mp/Sha256.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/settings/parser.hpp>
#include <components/settings/settings.hpp>
#include <components/settings/values.hpp>
#include <components/toutf8/toutf8.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>
#include <components/vfs/registerarchives.hpp>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwlua/luamanagerimp.hpp>
#include <apps/openmw/mwmp/records/ResolvedContentFingerprint.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/worldimp.hpp>

namespace bpo = boost::program_options;

namespace
{
    using StringList = std::vector<std::string>;

    struct ParsedConfig
    {
        Files::PathContainer dataDirectories;
        StringList archives;
        StringList contentFiles;
        std::map<std::string, std::string> fallback;
    };

    std::filesystem::path resolvePath(
        const std::filesystem::path& value, const std::filesystem::path& base)
    {
        if (value.empty() || value.is_absolute())
            return value.lexically_normal();
        return (base / value).lexically_normal();
    }

    ParsedConfig parseOpenmwConfig(const mwmp::ServerContentRegistry::Config& config)
    {
        if (config.openmwConfig.empty())
            throw std::runtime_error("Server content configuration is missing content.openmw_cfg");

        std::ifstream stream(config.openmwConfig);
        if (!stream)
            throw std::runtime_error(
                "Could not open server content configuration: " + config.openmwConfig.string());

        bpo::options_description options("headless content options");
        options.add_options()
            ("data", bpo::value<Files::MaybeQuotedPathContainer>()->composing()->default_value({}, ""))
            ("data-local", bpo::value<Files::MaybeQuotedPath>()->default_value({}, ""))
            ("fallback-archive", bpo::value<StringList>()->composing()->default_value({}, ""))
            ("fallback", bpo::value<Fallback::FallbackMap>()->composing()->default_value({}, ""))
            ("content", bpo::value<StringList>()->composing()->default_value({}, ""));

        bpo::variables_map variables;
        Files::parseConfig(stream, variables, options);
        bpo::notify(variables);

        const std::filesystem::path base = config.openmwConfig.parent_path();
        ParsedConfig result;
        for (const auto& data : variables["data"].as<Files::MaybeQuotedPathContainer>())
        {
            std::filesystem::path path = resolvePath(data, base);
            if (std::filesystem::is_directory(path))
                result.dataDirectories.push_back(std::move(path));
            else
                Log(Debug::Warning) << "[ContentRegistry] Ignoring missing data directory: " << path;
        }

        std::filesystem::path local = resolvePath(variables["data-local"].as<Files::MaybeQuotedPath>(), base);
        if (!local.empty())
        {
            if (!std::filesystem::is_directory(local))
                throw std::runtime_error("Configured data-local directory does not exist: " + local.string());
            result.dataDirectories.push_back(std::move(local));
        }

        const std::filesystem::path builtinVfs = config.resources / "vfs";
        if (!std::filesystem::is_directory(builtinVfs))
            throw std::runtime_error("OpenMW resources/vfs directory does not exist: " + builtinVfs.string());
        result.dataDirectories.insert(result.dataDirectories.begin(), builtinVfs);

        result.archives = variables["fallback-archive"].as<StringList>();
        result.fallback = variables["fallback"].as<Fallback::FallbackMap>().mMap;
        result.contentFiles = { "builtin.omwscripts" };
        std::set<std::string> seen{ "builtin.omwscripts" };
        for (const std::string& content : variables["content"].as<StringList>())
        {
            if (!seen.insert(content).second)
                throw std::runtime_error("Content file is configured more than once: " + content);
            result.contentFiles.push_back(content);
        }
        if (result.contentFiles.size() == 1)
            throw std::runtime_error("Server content configuration contains no content files");
        return result;
    }

    void initializeHeadlessSettings(const std::filesystem::path& resources)
    {
        static std::once_flag once;
        std::call_once(once, [&] {
            const std::filesystem::path defaults = resources.parent_path() / "defaults.bin";
            if (!std::filesystem::is_regular_file(defaults))
                throw std::runtime_error("OpenMW defaults.bin is missing: " + defaults.string());
            Settings::SettingsFileParser parser;
            parser.loadSettingsFile(defaults, Settings::Manager::mDefaultSettings, true, false);
            Settings::StaticValues::initDefaults();
            Settings::Manager::mUserSettings = Settings::Manager::mDefaultSettings;
            Settings::StaticValues::init();
        });
    }

    std::string hashFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream)
            throw std::runtime_error("Could not open content file for hashing: " + path.string());
        const std::string result = mwmp::crypto::sha256hex(stream);
        if (!stream.eof())
            throw std::runtime_error("Could not completely read content file: " + path.string());
        return result;
    }

    std::string hashVfsFile(const VFS::Manager& vfs, const VFS::Path::Normalized& path)
    {
        Files::IStreamPtr stream = vfs.get(path);
        const std::string result = mwmp::crypto::sha256hex(*stream);
        if (!stream->eof())
            throw std::runtime_error("Could not completely read VFS file: " + path.value());
        return result;
    }

    void updateU32(mwmp::crypto::Sha256& hash, std::uint32_t value)
    {
        const std::array<std::uint8_t, 4> bytes = { static_cast<std::uint8_t>(value),
            static_cast<std::uint8_t>(value >> 8), static_cast<std::uint8_t>(value >> 16),
            static_cast<std::uint8_t>(value >> 24) };
        hash.update(bytes.data(), bytes.size());
    }

    void updateString(mwmp::crypto::Sha256& hash, std::string_view value)
    {
        updateU32(hash, static_cast<std::uint32_t>(value.size()));
        hash.update(reinterpret_cast<const std::uint8_t*>(value.data()), value.size());
    }
}

struct mwmp::ServerContentRegistry::Runtime
{
    // Environment is declared first so it remains alive until every object
    // holding an Environment pointer has been destroyed.
    std::unique_ptr<MWBase::Environment> environment;
    std::unique_ptr<ToUTF8::Utf8Encoder> encoder;
    std::unique_ptr<Files::Collections> collections;
    std::unique_ptr<VFS::Manager> vfs;
    std::unique_ptr<Resource::ResourceSystem> resources;
    std::unique_ptr<MWWorld::World> world;
    std::unique_ptr<MWLua::LuaManager> lua;
    std::vector<std::string> contentFiles;
};

struct mwmp::ServerContentRegistry::Snapshot
{
    std::string resolvedFingerprint;
    std::vector<ManifestEntry> contentFiles;
    std::vector<ManifestEntry> luaScripts;

    bool operator==(const Snapshot&) const = default;
};

std::unique_ptr<mwmp::ServerContentRegistry::Runtime>
mwmp::ServerContentRegistry::loadRuntime(const Config& config)
{
    const ParsedConfig parsed = parseOpenmwConfig(config);
    Fallback::Map::init(parsed.fallback);
    initializeHeadlessSettings(config.resources);
    auto runtime = std::make_unique<Runtime>();
    runtime->environment = std::make_unique<MWBase::Environment>();
    runtime->encoder = std::make_unique<ToUTF8::Utf8Encoder>(ToUTF8::calculateEncoding(config.encoding));
    runtime->collections = std::make_unique<Files::Collections>(parsed.dataDirectories);
    runtime->vfs = std::make_unique<VFS::Manager>();
    VFS::registerArchives(runtime->vfs.get(), *runtime->collections, parsed.archives, true,
        &runtime->encoder->getStatelessEncoder());

    runtime->resources = std::make_unique<Resource::ResourceSystem>(
        runtime->vfs.get(), 0.0, &runtime->encoder->getStatelessEncoder());
    runtime->environment->setResourceSystem(*runtime->resources);
    runtime->world = std::make_unique<MWWorld::World>(runtime->resources.get(), 0, "", std::filesystem::path{});
    runtime->environment->setWorld(*runtime->world);
    runtime->environment->setESMStore(runtime->world->getStore());
    runtime->lua = std::make_unique<MWLua::LuaManager>(
        runtime->vfs.get(), config.resources / "lua_libs", true);
    runtime->environment->setLuaManager(*runtime->lua);

    runtime->lua->initPreLoad();
    runtime->contentFiles = parsed.contentFiles;
    runtime->world->loadData(
        *runtime->collections, runtime->contentFiles, {}, runtime->encoder.get(), nullptr, false);
    return runtime;
}

mwmp::ServerContentRegistry::Snapshot
mwmp::ServerContentRegistry::snapshot(const Runtime& runtime)
{
    Snapshot result;
    result.resolvedFingerprint = MWMP::resolvedContentFingerprint(runtime.world->getStore());
    result.contentFiles.reserve(runtime.contentFiles.size());
    for (const std::string& filename : runtime.contentFiles)
        result.contentFiles.push_back({ filename, hashFile(runtime.collections->getPath(filename)) });

    const ESM::LuaScriptsCfg scripts = runtime.world->getStore().getLuaScriptsCfg();
    result.luaScripts.reserve(scripts.mScripts.size());
    for (const ESM::LuaScriptCfg& script : scripts.mScripts)
    {
        result.luaScripts.push_back(
            { script.mScriptPath.value(), hashVfsFile(*runtime.vfs, script.mScriptPath) });
    }
    return result;
}

mwmp::ServerContentRegistry::ServerContentRegistry(Config config)
    : mConfig(std::move(config))
{
    mConfig.openmwConfig = std::filesystem::absolute(mConfig.openmwConfig).lexically_normal();
    mConfig.resources = std::filesystem::absolute(mConfig.resources).lexically_normal();

    if (mConfig.verifyDeterminism)
    {
        std::unique_ptr<Runtime> firstRuntime = loadRuntime(mConfig);
        const Snapshot first = snapshot(*firstRuntime);
        firstRuntime.reset();

        mRuntime = loadRuntime(mConfig);
        const Snapshot second = snapshot(*mRuntime);
        if (first != second)
            throw std::runtime_error(
                "Headless content loading is nondeterministic: independent passes produced different manifests");
        mResolvedFingerprint = second.resolvedFingerprint;
        mContentFiles = second.contentFiles;
        mLuaScripts = second.luaScripts;
    }
    else
    {
        mRuntime = loadRuntime(mConfig);
        Snapshot loaded = snapshot(*mRuntime);
        mResolvedFingerprint = std::move(loaded.resolvedFingerprint);
        mContentFiles = std::move(loaded.contentFiles);
        mLuaScripts = std::move(loaded.luaScripts);
    }

    Log(Debug::Info) << "[ContentRegistry] Loaded " << mContentFiles.size() << " ordered content files, "
                     << mLuaScripts.size() << " Lua scripts; resolved fingerprint=" << mResolvedFingerprint;
}

mwmp::ServerContentRegistry::~ServerContentRegistry() = default;

const MWWorld::ESMStore& mwmp::ServerContentRegistry::store() const
{
    return mRuntime->world->getStore();
}

bool mwmp::ServerContentRegistry::hasContentId(std::string_view id) const
{
    if (id.empty())
        return false;
    try
    {
        ESM::RefId refId = ESM::RefId::deserializeText(id);
        if (refId.empty())
            refId = ESM::RefId::stringRefId(id);
        const MWWorld::ESMStore& content = store();
        return content.findStatic(refId) != 0
            || content.get<ESM::Potion>().search(refId) != nullptr
            || content.get<ESM::Enchantment>().search(refId) != nullptr
            || content.get<ESM::Weapon>().search(refId) != nullptr
            || content.get<ESM::Armor>().search(refId) != nullptr
            || content.get<ESM::Clothing>().search(refId) != nullptr
            || content.get<ESM::Book>().search(refId) != nullptr
            || content.get<ESM::MagicEffect>().search(refId) != nullptr
            || content.get<ESM::Skill>().search(refId) != nullptr
            || content.get<ESM::Attribute>().search(refId) != nullptr
            || content.get<ESM::Script>().search(refId) != nullptr
            || content.get<ESM::BodyPart>().search(refId) != nullptr
            || content.get<ESM::Spell>().search(refId) != nullptr;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

bool mwmp::ServerContentRegistry::hasAsset(std::string_view path) const
{
    if (path.empty())
        return true;
    try
    {
        return mRuntime->vfs->exists(VFS::Path::Normalized(path));
    }
    catch (const std::exception&)
    {
        return false;
    }
}

bool mwmp::ServerContentRegistry::hasModel(std::string_view path) const
{
    if (path.empty())
        return true;
    try
    {
        const VFS::Path::Normalized normalized(path);
        return mRuntime->vfs->exists(normalized)
            || mRuntime->vfs->exists(Misc::ResourceHelpers::correctMeshPath(normalized));
    }
    catch (const std::exception&)
    {
        return false;
    }
}

bool mwmp::ServerContentRegistry::hasIcon(std::string_view path) const
{
    if (path.empty())
        return true;
    try
    {
        const VFS::Path::Normalized normalized(path);
        return mRuntime->vfs->exists(normalized)
            || mRuntime->vfs->exists(Misc::ResourceHelpers::correctIconPath(normalized, *mRuntime->vfs));
    }
    catch (const std::exception&)
    {
        return false;
    }
}

std::string mwmp::ServerContentRegistry::orderedManifestFingerprint(
    const std::vector<ManifestEntry>& entries)
{
    mwmp::crypto::Sha256 hash;
    static constexpr std::array<std::uint8_t, 8> header = { 'O', 'M', 'C', 'M', 1, 0, 0, 0 };
    hash.update(header.data(), header.size());
    updateU32(hash, static_cast<std::uint32_t>(entries.size()));
    for (const ManifestEntry& entry : entries)
    {
        updateString(hash, entry.filename);
        updateString(hash, entry.sha256);
    }
    return hash.finish();
}
