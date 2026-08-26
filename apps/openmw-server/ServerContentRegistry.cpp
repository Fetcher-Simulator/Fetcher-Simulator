#include "ServerContentRegistry.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <map>
#include <mutex>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/value_semantic.hpp>
#include <boost/program_options/variables_map.hpp>

#include <components/debug/debuglog.hpp>
#include <components/compiler/extensions.hpp>
#include <components/compiler/extensions0.hpp>
#include <components/fallback/fallback.hpp>
#include <components/fallback/validate.hpp>
#include <components/esm/attr.hpp>
#include <components/esm3/loadalch.hpp>
#include <components/esm3/loadappa.hpp>
#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadbody.hpp>
#include <components/esm3/loadbook.hpp>
#include <components/esm3/loadclot.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadench.hpp>
#include <components/esm3/loadglob.hpp>
#include <components/esm3/loadingr.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/esm3/loadlock.hpp>
#include <components/esm3/loadmisc.hpp>
#include <components/esm3/loadprob.hpp>
#include <components/esm3/loadrepa.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadscpt.hpp>
#include <components/esm3/loadskil.hpp>
#include <components/esm3/loadspel.hpp>
#include <components/esm3/loadweap.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/files/collections.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/openmw-mp/Sha256.hpp>
#include <components/openmw-mp/Records/EsmDynamicRecordConversion.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/settings/parser.hpp>
#include <components/settings/settings.hpp>
#include <components/settings/values.hpp>
#include <components/toutf8/toutf8.hpp>
#include <components/vfs/manager.hpp>
#include <components/vfs/pathutil.hpp>
#include <components/vfs/registerarchives.hpp>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwclass/classes.hpp>
#include <apps/openmw/mwlua/luamanagerimp.hpp>
#include <apps/openmw/mwmp/records/ResolvedContentFingerprint.hpp>
#include <apps/openmw/mwscript/compilercontext.hpp>
#include <apps/openmw/mwscript/extensions.hpp>
#include <apps/openmw/mwscript/scriptmanagerimp.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/containerstore.hpp>
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
    Compiler::Extensions scriptExtensions;
    std::unique_ptr<MWScript::CompilerContext> scriptCompilerContext;
    std::unique_ptr<MWScript::ScriptManager> scriptManager;
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
    MWClass::registerClasses();
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
    runtime->environment->setWorldModel(runtime->world->getWorldModel());
    runtime->environment->setESMStore(runtime->world->getStore());
    runtime->lua = std::make_unique<MWLua::LuaManager>(
        runtime->vfs.get(), config.resources / "lua_libs", true);
    runtime->environment->setLuaManager(*runtime->lua);

    runtime->lua->initPreLoad();
    runtime->contentFiles = parsed.contentFiles;
    runtime->world->loadData(
        *runtime->collections, runtime->contentFiles, {}, runtime->encoder.get(), nullptr, false);
    Compiler::registerExtensions(runtime->scriptExtensions);
    runtime->scriptCompilerContext
        = std::make_unique<MWScript::CompilerContext>(MWScript::CompilerContext::Type_Full);
    runtime->scriptCompilerContext->setExtensions(&runtime->scriptExtensions);
    runtime->scriptManager = std::make_unique<MWScript::ScriptManager>(
        runtime->world->getStore(), *runtime->scriptCompilerContext, 1);
    runtime->environment->setScriptManager(*runtime->scriptManager);
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

Resource::ResourceSystem& mwmp::ServerContentRegistry::resourceSystem() const
{
    return *mRuntime->resources;
}

MWWorld::WorldModel& mwmp::ServerContentRegistry::worldModel() const
{
    return mRuntime->world->getWorldModel();
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
            || content.get<ESM::Dialogue>().search(refId) != nullptr
            || content.get<ESM::Script>().search(refId) != nullptr
            || content.get<ESM::BodyPart>().search(refId) != nullptr
            || content.get<ESM::Spell>().search(refId) != nullptr;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

bool mwmp::ServerContentRegistry::hasStaticNpcRecord(std::string_view id) const
{
    if (id.empty())
        return false;
    try
    {
        ESM::RefId refId = ESM::RefId::deserializeText(id);
        if (refId.empty())
            refId = ESM::RefId::stringRefId(id);
        return store().get<ESM::NPC>().searchStatic(refId) != nullptr;
    }
    catch (const std::exception&)
    {
        return false;
    }
}

bool mwmp::ServerContentRegistry::hasStaticRecord(std::uint8_t recordType, std::string_view id) const
{
    ESM::RefId refId = ESM::RefId::deserializeText(id);
    if (refId.empty())
        refId = ESM::RefId::stringRefId(id);
    switch (static_cast<records::RecordType>(recordType))
    {
        case records::RecordType::Potion:
            return store().get<ESM::Potion>().searchStatic(refId) != nullptr;
        case records::RecordType::Enchantment:
            return store().get<ESM::Enchantment>().searchStatic(refId) != nullptr;
        case records::RecordType::Weapon:
            return store().get<ESM::Weapon>().searchStatic(refId) != nullptr;
        case records::RecordType::Armor:
            return store().get<ESM::Armor>().searchStatic(refId) != nullptr;
        case records::RecordType::Clothing:
            return store().get<ESM::Clothing>().searchStatic(refId) != nullptr;
        case records::RecordType::Book:
            return store().get<ESM::Book>().searchStatic(refId) != nullptr;
        case records::RecordType::Dialogue:
            return store().get<ESM::Dialogue>().searchStatic(refId) != nullptr;
        case records::RecordType::Script:
            return store().get<ESM::Script>().searchStatic(refId) != nullptr;
    }
    return false;
}

bool mwmp::ServerContentRegistry::validateScriptSource(std::string_view id, std::string_view source) const
{
    ESM::RefId refId = ESM::RefId::deserializeText(id);
    if (refId.empty())
        refId = ESM::RefId::stringRefId(id);
    return mRuntime->scriptManager->validateSource(refId, source);
}

std::optional<mwmp::ServerContentRegistry::PlacedItemReference>
mwmp::ServerContentRegistry::findPlacedItemReference(const PlacedObjectIdentity& identity) const
{
    if (!isCanonicalPlacedObjectIdentity(identity)
        || identity.kind != PlacedObjectKind::ContentReference)
        return std::nullopt;

    MWWorld::CellStore* cell = nullptr;
    if (identity.cellId.rfind("EXT:", 0) == 0)
    {
        int x = 0;
        int y = 0;
        if (std::sscanf(identity.cellId.c_str(), "EXT:%d,%d", &x, &y) != 2)
            return std::nullopt;
        cell = &mRuntime->world->getWorldModel().getExterior(
            ESM::ExteriorCellLocation(x, y, ESM::Cell::sDefaultWorldspaceId));
    }
    else
        cell = &mRuntime->world->getWorldModel().getInterior(identity.cellId);

    cell->load();
    std::optional<PlacedItemReference> result;
    const ESM::RefNum expected { identity.refIndex, identity.refContentFile };
    cell->forEachConst([&](const MWWorld::ConstPtr& ptr) {
        if (ptr.getCellRef().getRefNum() != expected
            || ptr.getCellRef().getRefId().serializeText() != identity.refId)
            return true;

        const auto type = ptr.getType();
        const bool itemType = type == ESM::Apparatus::sRecordId || type == ESM::Armor::sRecordId
            || type == ESM::Book::sRecordId || type == ESM::Clothing::sRecordId
            || type == ESM::Ingredient::sRecordId || type == ESM::Light::sRecordId
            || type == ESM::Miscellaneous::sRecordId || type == ESM::Lockpick::sRecordId
            || type == ESM::Probe::sRecordId || type == ESM::Repair::sRecordId
            || type == ESM::Weapon::sRecordId || type == ESM::Potion::sRecordId;
        if (!itemType)
            return false;

        PlacedItemReference item;
        item.identity = identity;
        item.worldCount = ptr.getCellRef().getCount();
        item.gold = ptr.getClass().isGold(ptr);
        item.itemValue = ptr.getClass().getValue(ptr);
        item.inventoryCount = item.gold ? item.worldCount * item.itemValue : item.worldCount;
        item.charge = static_cast<std::int32_t>(ptr.getCellRef().getCharge());
        item.enchantmentCharge = ptr.getCellRef().getEnchantmentCharge();
        item.soul = ptr.getCellRef().getSoul().serializeText();
        item.ownerId = ptr.getCellRef().getOwner().serializeText();
        item.factionId = ptr.getCellRef().getFaction().serializeText();
        item.factionRank = ptr.getCellRef().getFactionRank();
        item.enabled = ptr.getRefData().isEnabled() && item.worldCount > 0;
        const ESM::Position& position = ptr.getRefData().getPosition();
        for (int axis = 0; axis < 3; ++axis)
        {
            item.position.pos[axis] = position.pos[axis];
            item.position.rot[axis] = position.rot[axis];
        }
        const std::string& globalName = ptr.getCellRef().getGlobalVariable();
        if (!globalName.empty())
        {
            const ESM::Global* global
                = store().get<ESM::Global>().search(ESM::RefId::stringRefId(globalName));
            item.ownershipGlobalAllowsUse = global && global->mValue.getInteger() != 0;
        }
        result = std::move(item);
        return false;
    });
    return result;
}

std::optional<mwmp::ServerContentRegistry::ContainerReference>
mwmp::ServerContentRegistry::findContainerReference(
    std::string_view cellId, std::string_view refId, std::uint32_t refIndex) const
{
    if (cellId.empty() || refId.empty() || refIndex == 0)
        return std::nullopt;

    MWWorld::CellStore* cell = nullptr;
    const std::string cellName(cellId);
    if (cellId.starts_with("EXT:"))
    {
        int x = 0;
        int y = 0;
        if (std::sscanf(cellName.c_str(), "EXT:%d,%d", &x, &y) != 2)
            return std::nullopt;
        cell = &mRuntime->world->getWorldModel().getExterior(
            ESM::ExteriorCellLocation(x, y, ESM::Cell::sDefaultWorldspaceId));
    }
    else
        cell = &mRuntime->world->getWorldModel().getInterior(cellName);

    cell->load();
    std::optional<ContainerReference> result;
    bool ambiguous = false;
    cell->forEachConst([&](const MWWorld::ConstPtr& ptr) {
        if (ptr.getType() != ESM::Container::sRecordId
            || ptr.getCellRef().getRefNum().mIndex != refIndex
            || ptr.getCellRef().getRefId().serializeText() != refId)
            return true;
        if (result)
        {
            ambiguous = true;
            return false;
        }
        ContainerReference reference;
        reference.ownerId = ptr.getCellRef().getOwner().serializeText();
        reference.factionId = ptr.getCellRef().getFaction().serializeText();
        reference.factionRank = ptr.getCellRef().getFactionRank();
        reference.enabled = ptr.getRefData().isEnabled();
        const ESM::Position& position = ptr.getRefData().getPosition();
        for (int axis = 0; axis < 3; ++axis)
        {
            reference.position.pos[axis] = position.pos[axis];
            reference.position.rot[axis] = position.rot[axis];
        }
        const std::string& globalName = ptr.getCellRef().getGlobalVariable();
        if (!globalName.empty())
        {
            const ESM::Global* global
                = store().get<ESM::Global>().search(ESM::RefId::stringRefId(globalName));
            reference.ownershipGlobalAllowsUse = global && global->mValue.getInteger() != 0;
        }
        result = std::move(reference);
        return true;
    });
    return ambiguous ? std::nullopt : result;
}

std::optional<mwmp::ContainerRecord> mwmp::ServerContentRegistry::resolveJailEvidenceContainer(
    const CellId& playerCell, const Position& playerPosition) const
{
    try
    {
        MWWorld::WorldModel& model = mRuntime->world->getWorldModel();
        const ESM::RefId prisonMarkerId = ESM::RefId::stringRefId("prisonmarker");

        auto closestExterior = [&](float worldX, float worldY) -> MWWorld::Ptr {
            const ESM::ExteriorCellLocation origin
                = ESM::positionToExteriorCellLocation(worldX, worldY);
            const MWWorld::Store<ESM::Cell>& cells = store().get<ESM::Cell>();

            int maximumRadius = 0;
            for (auto it = cells.extBegin(); it != cells.extEnd(); ++it)
            {
                maximumRadius = std::max(maximumRadius,
                    std::max(std::abs(it->getGridX() - origin.mX), std::abs(it->getGridY() - origin.mY)));
            }

            auto markerAt = [&](int x, int y) -> MWWorld::Ptr {
                if (!cells.search(x, y))
                    return {};
                MWWorld::CellStore& cell = model.getExterior(
                    ESM::ExteriorCellLocation(x, y, ESM::Cell::sDefaultWorldspaceId));
                return cell.search(prisonMarkerId);
            };

            // Preserve vanilla's tie-breaking order without materializing every exterior
            // cell in the world model: SW -> SE -> NE -> NW -> SW on each nearest ring.
            if (MWWorld::Ptr local = markerAt(origin.mX, origin.mY); !local.isEmpty())
                return local;

            for (int radius = 1; radius <= maximumRadius; ++radius)
            {
                const int west = origin.mX - radius;
                const int east = origin.mX + radius;
                const int south = origin.mY - radius;
                const int north = origin.mY + radius;

                for (int x = west; x <= east; ++x)
                {
                    if (MWWorld::Ptr marker = markerAt(x, south); !marker.isEmpty())
                        return marker;
                }
                for (int y = south + 1; y <= north; ++y)
                {
                    if (MWWorld::Ptr marker = markerAt(east, y); !marker.isEmpty())
                        return marker;
                }
                for (int x = east - 1; x >= west; --x)
                {
                    if (MWWorld::Ptr marker = markerAt(x, north); !marker.isEmpty())
                        return marker;
                }
                for (int y = north - 1; y > south; --y)
                {
                    if (MWWorld::Ptr marker = markerAt(west, y); !marker.isEmpty())
                        return marker;
                }
            }
            return {};
        };

        MWWorld::Ptr marker;
        if (playerCell.isExterior)
            marker = closestExterior(playerPosition.pos[0], playerPosition.pos[1]);
        else
        {
            std::set<ESM::RefId> checked;
            std::set<ESM::RefId> current;
            std::set<ESM::RefId> next;
            MWWorld::CellStore& startingCell = model.getInterior(playerCell.cellName);
            next.insert(startingCell.getCell()->getId());
            while (!next.empty() && marker.isEmpty())
            {
                current.clear();
                std::swap(current, next);
                for (const ESM::RefId& cellId : current)
                {
                    MWWorld::CellStore& cell = model.getCell(cellId);
                    checked.insert(cellId);
                    marker = cell.search(prisonMarkerId);
                    if (!marker.isEmpty())
                        break;
                    for (const MWWorld::LiveCellRef<ESM::Door>& door : cell.getReadOnlyDoors().mList)
                    {
                        if (!door.mRef.getTeleport())
                            continue;
                        if (door.mRef.getDestCell().is<ESM::ESM3ExteriorCellRefId>())
                        {
                            const osg::Vec3f destination = door.mRef.getDoorDest().asVec3();
                            marker = closestExterior(destination.x(), destination.y());
                            if (marker.isEmpty())
                                return std::nullopt;
                            break;
                        }
                        const ESM::RefId& destination = door.mRef.getDestCell();
                        if (!checked.contains(destination) && !current.contains(destination))
                            next.insert(destination);
                    }
                    if (!marker.isEmpty())
                        break;
                }
            }
        }

        if (marker.isEmpty())
            return std::nullopt;
        const ESM::RefId prisonId = marker.getCellRef().getDestCell();
        if (prisonId.empty() || prisonId.is<ESM::ESM3ExteriorCellRefId>())
            return std::nullopt;

        MWWorld::CellStore& prison = model.getCell(prisonId);
        prison.load();
        MWWorld::Ptr chest;
        bool ambiguous = false;
        prison.forEach([&](const MWWorld::Ptr& ptr) {
            if (ptr.getType() != ESM::Container::sRecordId
                || ptr.getCellRef().getRefId() != ESM::RefId::stringRefId("stolen_goods"))
                return true;
            if (!chest.isEmpty())
            {
                ambiguous = true;
                return false;
            }
            chest = ptr;
            return true;
        });
        if (chest.isEmpty() || ambiguous)
            return std::nullopt;

        ContainerRecord result;
        result.cellId = prisonId.serializeText();
        result.refId = chest.getCellRef().getRefId().serializeText();
        result.refNum = chest.getCellRef().getRefNum().mIndex;
        result.mpNum = 0;
        result.hasAuthority = true;
        MWWorld::ContainerStore& contents = chest.getClass().getContainerStore(chest);
        contents.resolve();
        for (auto it = contents.begin(); it != contents.end(); ++it)
        {
            ContainerItem item;
            item.refId = it->getCellRef().getRefId().serializeText();
            item.count = it->getCellRef().getCount();
            item.charge = static_cast<int>(it->getCellRef().getCharge());
            item.enchantmentCharge = it->getCellRef().getEnchantmentCharge();
            item.soul = it->getCellRef().getSoul().serializeText();
            if (!item.refId.empty() && item.count > 0)
                result.items.push_back(std::move(item));
        }
        return result;
    }
    catch (const std::exception& e)
    {
        Log(Debug::Warning) << "[ContentRegistry] Jail evidence resolution failed: " << e.what();
        return std::nullopt;
    }
}

std::optional<mwmp::ServerContentRegistry::CrimeInteractionReference>
mwmp::ServerContentRegistry::findCrimeInteractionReference(
    std::string_view cellId, std::string_view refId, std::uint32_t refIndex,
    std::int32_t refContentFile) const
{
    if (cellId.empty() || refId.empty() || refIndex == 0 || refContentFile < 0)
        return std::nullopt;
    MWWorld::CellStore* cell = nullptr;
    const std::string cellName(cellId);
    if (cellId.starts_with("EXT:"))
    {
        int x = 0;
        int y = 0;
        if (std::sscanf(cellName.c_str(), "EXT:%d,%d", &x, &y) != 2)
            return std::nullopt;
        cell = &mRuntime->world->getWorldModel().getExterior(
            ESM::ExteriorCellLocation(x, y, ESM::Cell::sDefaultWorldspaceId));
    }
    else
        cell = &mRuntime->world->getWorldModel().getInterior(cellName);

    cell->load();
    std::optional<CrimeInteractionReference> result;
    bool ambiguous = false;
    cell->forEachConst([&](const MWWorld::ConstPtr& ptr) {
        if (ptr.getCellRef().getRefNum() != ESM::RefNum{ refIndex, refContentFile }
            || ptr.getCellRef().getRefId().serializeText() != refId)
            return true;
        const auto type = ptr.getType();
        if (type != ESM::Door::sRecordId && type != ESM::Container::sRecordId)
            return false;
        if (result)
        {
            ambiguous = true;
            return false;
        }
        CrimeInteractionReference reference;
        reference.ownerId = ptr.getCellRef().getOwner().serializeText();
        reference.factionId = ptr.getCellRef().getFaction().serializeText();
        reference.factionRank = ptr.getCellRef().getFactionRank();
        reference.enabled = ptr.getRefData().isEnabled();
        reference.locked = ptr.getCellRef().isLocked();
        reference.lockLevel = ptr.getCellRef().getLockLevel();
        reference.trapped = !ptr.getCellRef().getTrap().empty();
        const ESM::Position& position = ptr.getRefData().getPosition();
        for (int axis = 0; axis < 3; ++axis)
        {
            reference.position.pos[axis] = position.pos[axis];
            reference.position.rot[axis] = position.rot[axis];
        }
        const std::string& globalName = ptr.getCellRef().getGlobalVariable();
        if (!globalName.empty())
        {
            const ESM::Global* global
                = store().get<ESM::Global>().search(ESM::RefId::stringRefId(globalName));
            reference.ownershipGlobalAllowsUse = global && global->mValue.getInteger() != 0;
        }
        result = std::move(reference);
        return true;
    });
    return ambiguous ? std::nullopt : result;
}

void mwmp::ServerContentRegistry::installRuntimeDefinition(
    std::string_view id, const records::DynamicRecordDefinition& definition)
{
    const records::RecordType type = records::getRecordType(definition);
    const bool durableScriptContent
        = type == records::RecordType::Dialogue || type == records::RecordType::Script;
    const bool explicitClothingContent
        = type == records::RecordType::Clothing
        && definition.authoringMode != records::AuthoringMode::Generated;
    if (!durableScriptContent && !explicitClothingContent)
        return;
    if (durableScriptContent && definition.authoringMode == records::AuthoringMode::Generated)
        throw std::runtime_error("Server content definition requires explicit authoring mode");

    const ESM::RefId refId = ESM::RefId::stringRefId(id);
    MWWorld::ESMStore& content = mRuntime->world->getStore();
    records::EsmDynamicRecord converted = records::toEsmRecord(definition);
    std::visit([&](auto& record) {
        using Record = std::decay_t<decltype(record)>;
        if constexpr (std::is_same_v<Record, ESM::Dialogue>
            || std::is_same_v<Record, ESM::Script>
            || std::is_same_v<Record, ESM::Clothing>)
        {
            record.mId = refId;
            if constexpr (std::is_same_v<Record, ESM::Dialogue>)
            {
                if (record.mStringId.empty())
                    record.mStringId = std::string(id);
            }
            const bool overrideOnly = definition.authoringMode == records::AuthoringMode::Override;
            const auto& typedStore = content.get<Record>();
            if (definition.authoringMode == records::AuthoringMode::New
                && typedStore.searchStatic(refId) != nullptr)
                throw std::runtime_error("New server content collides with static content");
            if (content.overrideRecord(record, overrideOnly) == nullptr)
                throw std::runtime_error("Server content override has no static base");
            if constexpr (std::is_same_v<Record, ESM::Script>)
                mRuntime->scriptManager->invalidate(refId);
        }
        else
            throw std::runtime_error("Unexpected runtime server content type");
    }, converted);
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
