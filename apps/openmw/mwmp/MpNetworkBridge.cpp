#include "MpNetworkBridge.hpp"

#include <components/debug/debuglog.hpp>
#include <components/openmw-mp/Packets/Lua/PacketLuaEvent.hpp>
#include <components/openmw-mp/Packets/Lua/PacketLuaStorage.hpp>
#include <components/openmw-mp/Records/EsmDynamicRecordConversion.hpp>
#include <components/openmw-mp/ServerLuaPackage.hpp>
#include <components/lua/scriptscontainer.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/luamanager.hpp"
#include "../mwlua/context.hpp"
#include "../mwlua/object.hpp"
#include "../mwlua/magictypebindings.hpp"
#include "../mwlua/types/types.hpp"
#include "../mwworld/class.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "Main.hpp"
#include "network/Client.hpp"
#include "records/RecordCreationManager.hpp"
#include "sync/ActorSync.hpp"
#include "sync/WorldObjectSync.hpp"

namespace mwmp
{
    namespace
    {
        template <class ObjectT>
        sol::optional<uint32_t> getObjectMpNum(const ObjectT& object)
        {
            if (!Main::isInitialised())
                return sol::nullopt;

            const MWWorld::Ptr& ptr = object.ptrOrEmpty();
            if (ptr.isEmpty())
                return sol::nullopt;

            const uint32_t mpNum = Main::get().getWorldObjectSync().getMpNumForObject(ptr);
            if (mpNum == 0)
                return sol::nullopt;

            return mpNum;
        }

        records::CreateOperation parseCreateOperation(std::string_view value)
        {
            if (value.empty() || value == "custom")
                return records::CreateOperation::CustomRecord;
            if (value == "alchemy")
                return records::CreateOperation::Alchemy;
            if (value == "enchanting")
                return records::CreateOperation::Enchanting;
            throw std::invalid_argument("Unsupported multiplayer record operation: " + std::string(value));
        }

        records::DynamicRecordDefinition parseRecordDefinition(
            std::string_view type, const sol::object& value)
        {
            if (type == "potion")
            {
                if (value.is<ESM::Potion>()) return records::fromEsmRecord(value.as<ESM::Potion>());
                if (value.is<sol::table>()) return records::fromEsmRecord(MWLua::tableToPotion(value.as<sol::table>()));
            }
            else if (type == "enchantment")
            {
                if (value.is<ESM::Enchantment>()) return records::fromEsmRecord(value.as<ESM::Enchantment>());
                if (value.is<sol::table>())
                    return records::fromEsmRecord(MWLua::tableToEnchantment(value.as<sol::table>()));
            }
            else if (type == "weapon")
            {
                if (value.is<ESM::Weapon>()) return records::fromEsmRecord(value.as<ESM::Weapon>());
                if (value.is<sol::table>()) return records::fromEsmRecord(MWLua::tableToWeapon(value.as<sol::table>()));
            }
            else if (type == "armor")
            {
                if (value.is<ESM::Armor>()) return records::fromEsmRecord(value.as<ESM::Armor>());
                if (value.is<sol::table>()) return records::fromEsmRecord(MWLua::tableToArmor(value.as<sol::table>()));
            }
            else if (type == "clothing")
            {
                if (value.is<ESM::Clothing>()) return records::fromEsmRecord(value.as<ESM::Clothing>());
                if (value.is<sol::table>())
                    return records::fromEsmRecord(MWLua::tableToClothing(value.as<sol::table>()));
            }
            else if (type == "book")
            {
                if (value.is<ESM::Book>()) return records::fromEsmRecord(value.as<ESM::Book>());
                if (value.is<sol::table>()) return records::fromEsmRecord(MWLua::tableToBook(value.as<sol::table>()));
            }
            throw std::invalid_argument("Record definition does not match supported type '" + std::string(type) + "'");
        }

        void setTemporaryEnchantmentReference(
            records::DynamicRecordDefinition& definition, const std::string& temporaryKey)
        {
            if (temporaryKey.empty())
                return;
            std::visit(
                [&](auto& record) {
                    using Record = std::decay_t<decltype(record)>;
                    if constexpr (std::is_same_v<Record, records::Weapon> || std::is_same_v<Record, records::Armor>
                        || std::is_same_v<Record, records::Clothing> || std::is_same_v<Record, records::Book>)
                    {
                        record.enchantment.kind = records::ReferenceKind::TemporaryKey;
                        record.enchantment.value = temporaryKey;
                    }
                    else
                        throw std::invalid_argument("enchantmentKey is valid only for enchantable item records");
                },
                definition.data);
        }

        records::RecordCreateRequest parseCreateRequest(
            const sol::table& proposal, const std::string& scriptPackageId)
        {
            records::RecordCreateRequest request;
            request.requestId = proposal.get_or("requestId", std::string{});
            request.operation = parseCreateOperation(proposal.get_or("operation", std::string("custom")));
            request.scriptPackageId = scriptPackageId;
            request.evidence = proposal.get_or("evidence", std::string{});

            sol::object recordsObject = proposal["records"];
            if (!recordsObject.is<sol::table>())
                throw std::invalid_argument("mp.records.request requires an array field named 'records'");
            sol::table draftRecords = recordsObject.as<sol::table>();
            for (std::size_t index = 1; index <= draftRecords.size(); ++index)
            {
                sol::object draftObject = draftRecords[index];
                if (!draftObject.is<sol::table>())
                    throw std::invalid_argument("Each mp.records record draft must be a table");
                const sol::table draftTable = draftObject.as<sol::table>();
                records::RecordDraft draft;
                draft.temporaryKey = draftTable.get_or("key", std::string{});
                const std::string type = draftTable.get_or("type", std::string{});
                sol::object definition = draftTable["definition"];
                if (draft.temporaryKey.empty() || type.empty() || definition == sol::nil)
                    throw std::invalid_argument("Each record draft requires key, type, and definition");
                draft.definition = parseRecordDefinition(type, definition);
                const std::string enchantmentKey = draftTable.get_or("enchantmentKey", std::string{});
                setTemporaryEnchantmentReference(draft.definition, enchantmentKey);
                request.bundle.records.push_back(std::move(draft));
                if (!enchantmentKey.empty())
                    request.bundle.dependencies.push_back(
                        { request.bundle.records.back().temporaryKey, enchantmentKey });
            }

            sol::object dependenciesObject = proposal["dependencies"];
            if (dependenciesObject.is<sol::table>())
            {
                sol::table dependencies = dependenciesObject.as<sol::table>();
                for (std::size_t index = 1; index <= dependencies.size(); ++index)
                {
                    sol::object edgeObject = dependencies[index];
                    if (!edgeObject.is<sol::table>())
                        throw std::invalid_argument("Each record dependency must be a table");
                    const sol::table edge = edgeObject.as<sol::table>();
                    request.bundle.dependencies.push_back(
                        { edge.get_or("owner", std::string{}), edge.get_or("dependency", std::string{}) });
                }
            }
            return request;
        }

        sol::table makeLuaCreateResult(sol::state_view lua, const records::RecordCreateResult& result)
        {
            sol::table value(lua, sol::create);
            value["requestId"] = result.requestId;
            value["accepted"] = result.accepted;
            value["error"] = std::string(records::getCreateErrorCode(result.error));
            value["inventoryRevision"] = result.inventoryRevision;
            value["commitSequence"] = result.commitSequence;
            sol::table created(lua, sol::create);
            for (const records::CreatedRecord& record : result.records)
            {
                sol::table mapping(lua, sol::create);
                mapping["id"] = record.recordId;
                mapping["reused"] = record.reused;
                created[record.temporaryKey] = LuaUtil::makeReadOnly(mapping);
            }
            value["records"] = LuaUtil::makeReadOnly(created);
            return LuaUtil::makeReadOnly(value);
        }
    }

    void MpNetworkBridge::queueInbound(LuaEvent event)
    {
        std::lock_guard<std::mutex> lock(mInboundMutex);
        mInboundEvents.push_back(std::move(event));
    }

    void MpNetworkBridge::queueStorage(
        LuaStorageAction action, std::string section, std::vector<LuaStorageEntry> entries)
    {
        std::lock_guard<std::mutex> lock(mStorageMutex);
        mStorageUpdates.push_back({ action, std::move(section), std::move(entries) });
    }

    void MpNetworkBridge::queueOutbound(std::string eventName, LuaUtil::BinaryData eventData)
    {
        std::lock_guard<std::mutex> lock(mOutboundMutex);
        mOutboundEvents.push_back({ 0, std::move(eventName), std::move(eventData) });
    }

    void MpNetworkBridge::processIncoming(MWBase::LuaManager& luaManager)
    {
        std::vector<LuaEvent> events;
        {
            std::lock_guard<std::mutex> lock(mInboundMutex);
            events.swap(mInboundEvents);
        }

        std::vector<LuaStorageUpdate> storageUpdates;
        {
            std::lock_guard<std::mutex> lock(mStorageMutex);
            storageUpdates.swap(mStorageUpdates);
        }

        for (auto& update : storageUpdates)
        {
            std::vector<MWBase::LuaManager::GlobalStorageValue> values;
            values.reserve(update.entries.size());
            for (auto& entry : update.entries)
            {
                values.push_back({
                    std::move(entry.section),
                    std::move(entry.key),
                    std::move(entry.value),
                });
            }

            switch (update.action)
            {
                case LuaStorageAction::Snapshot:
                    luaManager.receiveGlobalStorageSnapshot(std::move(values));
                    break;
                case LuaStorageAction::Delta:
                    for (auto& value : values)
                        luaManager.receiveGlobalStorageDelta(std::move(value));
                    break;
                case LuaStorageAction::ResetSection:
                    luaManager.receiveGlobalStorageSection(std::move(update.section), std::move(values));
                    break;
            }
        }

        for (auto& event : events)
        {
            luaManager.receiveGlobalEvent(std::move(event.eventName), std::move(event.eventData));
        }
    }

    void MpNetworkBridge::drainOutgoing(NetworkClient& client)
    {
        std::vector<LuaEvent> events;
        {
            std::lock_guard<std::mutex> lock(mOutboundMutex);
            events.swap(mOutboundEvents);
        }

        for (auto& event : events)
        {
            PacketLuaEvent pkt;
            pkt.pid = event.pid;
            pkt.eventName = event.eventName;
            pkt.eventData = event.eventData;
            client.sendReliable(pkt.encode());
        }
    }

    sol::object initClientMpPackage(const MWLua::Context& context)
    {
        sol::state_view lua = context.sol();
        return sol::make_object(lua, sol::as_function([context](const sol::table& hiddenData) {
        sol::state_view lua = context.sol();
        sol::table mp(lua, sol::create);
        mp["API_VERSION"] = serverlua::MultiplayerLuaApiVersion;

        std::string scriptPackageId;
        const sol::optional<LuaUtil::ScriptId> scriptId
            = hiddenData.get<sol::optional<LuaUtil::ScriptId>>(LuaUtil::ScriptsContainer::sScriptIdKey);
        if (scriptId && scriptId->mIndex >= 0
            && scriptId->mIndex < static_cast<int>(context.mLua->getConfiguration().size()))
            scriptPackageId = context.mLua->getConfiguration()[scriptId->mIndex].mScriptPath.value();

        mp.set_function("sendToServer", [context](std::string eventName, const sol::object& eventData) {
            if (!Main::isInitialised() || !Main::isConnected())
                return;

            Main::get().getNetworkBridge().queueOutbound(
                std::move(eventName), LuaUtil::serialize(eventData, context.mSerializer));
        });

        mp.set_function("isConnected", []() -> bool {
            return Main::isConnected();
        });

        mp.set_function("isServer", []() -> bool {
            return false;
        });

        sol::table recordApi(lua, sol::create);
        recordApi.set_function("isAvailable", [] { return Main::isInitialised() && Main::isConnected(); });
        recordApi.set_function("request",
            [scriptPackageId, lua](const sol::table& proposal, sol::main_protected_function callback) {
                if (!Main::isInitialised() || !Main::isConnected())
                    throw std::runtime_error("mp.records.request requires an active multiplayer connection");
                records::RecordCreateRequest request = parseCreateRequest(proposal, scriptPackageId);
                if (request.requestId.empty())
                    request.requestId = Main::get().getRecordCreationManager().nextRequestId();
                const std::string requestId = request.requestId;
                Main::get().getRecordCreationManager().request(std::move(request),
                    [lua, callback = std::move(callback), scriptPackageId](
                        const records::RecordCreateResult& result) mutable {
                        try
                        {
                            LuaUtil::call(callback, makeLuaCreateResult(lua, result));
                        }
                        catch (const std::exception& e)
                        {
                            Log(Debug::Error) << "[MP] mp.records callback failed script="
                                              << scriptPackageId << " error=" << e.what();
                        }
                    });
                return requestId;
            });
        mp["records"] = LuaUtil::makeReadOnly(recordApi);

        sol::table inventoryTakeApi(lua, sol::create);
        inventoryTakeApi.set_function("isAvailable", [] { return Main::isInitialised() && Main::isConnected(); });
        inventoryTakeApi.set_function("request",
            [lua](const MWLua::GObject& source, const MWLua::GObject& item, int count, bool pickpocket,
                sol::main_protected_function callback) {
                if (!Main::isInitialised() || !Main::isConnected())
                    throw std::runtime_error("mp.inventoryTake.request requires an active multiplayer connection");
                const MWWorld::Ptr sourcePtr = source.ptrOrEmpty();
                const MWWorld::Ptr itemPtr = item.ptrOrEmpty();
                if (sourcePtr.isEmpty() || itemPtr.isEmpty() || count <= 0)
                    throw std::runtime_error("mp.inventoryTake.request requires a valid source, item, and positive count");

                InventoryTakeKind kind = InventoryTakeKind::Container;
                if (sourcePtr.getClass().isActor())
                {
                    if (sourcePtr.getClass().getCreatureStats(sourcePtr).isDead())
                        kind = InventoryTakeKind::Corpse;
                    else
                        kind = pickpocket ? InventoryTakeKind::Pickpocket : InventoryTakeKind::ActorInventory;
                }
                else if (pickpocket)
                    throw std::runtime_error("mp.inventoryTake.request cannot pickpocket a non-actor source");

                const bool queued = Main::get().getWorldObjectSync().requestInventoryTake(
                    sourcePtr, itemPtr, count, kind,
                    [lua, callback = std::move(callback)](const InventoryTakeResult& result) mutable {
                        sol::table value(lua, sol::create);
                        value["requestId"] = result.requestId;
                        value["accepted"] = result.accepted;
                        value["replayed"] = result.replayed;
                        value["error"] = std::string(getInventoryTakeErrorCode(result.error));
                        value["kind"] = static_cast<unsigned>(result.kind);
                        value["itemRefId"] = result.itemRefId;
                        value["itemCount"] = result.itemCount;
                        value["inventoryRevision"] = result.inventoryRevision;
                        value["detected"] = result.detected;
                        value["detectionRoll"] = result.detectionRoll;
                        value["theft"] = result.theft;
                        value["crimeValue"] = result.crimeValue;
                        LuaUtil::call(callback, value);
                    });
                if (!queued)
                    throw std::runtime_error("mp.inventoryTake.request could not build a canonical request");
            });
        mp["inventoryTake"] = LuaUtil::makeReadOnly(inventoryTakeApi);

        sol::table barterApi(lua, sol::create);
        barterApi.set_function("isAvailable", [] { return Main::isInitialised() && Main::isConnected(); });
        barterApi.set_function("purchase",
            [lua](const MWLua::GObject& merchant, const MWLua::GObject& source,
                const MWLua::GObject& item, int count, int barterPrice, sol::main_protected_function callback) {
                if (!Main::isInitialised() || !Main::isConnected())
                    throw std::runtime_error("mp.barter.purchase requires an active multiplayer connection");
                const MWWorld::Ptr merchantPtr = merchant.ptrOrEmpty();
                const MWWorld::Ptr sourcePtr = source.ptrOrEmpty();
                const MWWorld::Ptr itemPtr = item.ptrOrEmpty();
                if (merchantPtr.isEmpty() || sourcePtr.isEmpty() || itemPtr.isEmpty()
                    || count <= 0 || barterPrice <= 0)
                    throw std::runtime_error(
                        "mp.barter.purchase requires a valid merchant, source, item, positive count, and price");

                const bool queued = Main::get().getWorldObjectSync().requestBarterTake(
                    merchantPtr, sourcePtr, itemPtr, count, barterPrice,
                    [lua, callback = std::move(callback)](const InventoryTakeResult& result) mutable {
                        sol::table value(lua, sol::create);
                        value["requestId"] = result.requestId;
                        value["accepted"] = result.accepted;
                        value["replayed"] = result.replayed;
                        value["error"] = std::string(getInventoryTakeErrorCode(result.error));
                        value["itemRefId"] = result.itemRefId;
                        value["itemCount"] = result.itemCount;
                        value["inventoryRevision"] = result.inventoryRevision;
                        LuaUtil::call(callback, value);
                    });
                if (!queued)
                    throw std::runtime_error("mp.barter.purchase could not build a canonical request");
            });
        mp["barter"] = LuaUtil::makeReadOnly(barterApi);

        sol::table inventoryPutApi(lua, sol::create);
        inventoryPutApi.set_function("isAvailable", [] { return Main::isInitialised() && Main::isConnected(); });
        inventoryPutApi.set_function("request",
            [lua](const MWLua::GObject& destination, const MWLua::GObject& item, int count,
                sol::main_protected_function callback) {
                if (!Main::isInitialised() || !Main::isConnected())
                    throw std::runtime_error("mp.inventoryPut.request requires an active multiplayer connection");
                const MWWorld::Ptr destinationPtr = destination.ptrOrEmpty();
                const MWWorld::Ptr itemPtr = item.ptrOrEmpty();
                if (destinationPtr.isEmpty() || itemPtr.isEmpty() || count <= 0)
                    throw std::runtime_error(
                        "mp.inventoryPut.request requires a valid destination, item, and positive count");
                const bool queued = Main::get().getWorldObjectSync().requestInventoryPut(
                    destinationPtr, itemPtr, count,
                    [lua, callback = std::move(callback)](const InventoryPutResult& result) mutable {
                        sol::table value(lua, sol::create);
                        value["requestId"] = result.requestId;
                        value["accepted"] = result.accepted;
                        value["replayed"] = result.replayed;
                        value["error"] = std::string(getInventoryPutErrorCode(result.error));
                        value["itemRefId"] = result.itemRefId;
                        value["itemInstanceId"] = result.itemInstanceId;
                        value["itemCount"] = result.itemCount;
                        value["inventoryRevision"] = result.inventoryRevision;
                        LuaUtil::call(callback, value);
                    });
                if (!queued)
                    throw std::runtime_error("mp.inventoryPut.request could not build a canonical request");
            });
        mp["inventoryPut"] = LuaUtil::makeReadOnly(inventoryPutApi);

        sol::table containerUpdatesApi(lua, sol::create);
        containerUpdatesApi.set_function("revision", [](const MWLua::GObject& object) -> std::uint64_t {
            if (!Main::isInitialised() || !Main::isConnected())
                return 0;
            return Main::get().getWorldObjectSync().getContainerRevision(object.ptrOrEmpty());
        });
        mp["containerUpdates"] = LuaUtil::makeReadOnly(containerUpdatesApi);

        mp.set_function("hasActorAuthority", [](const std::string& cellId) -> bool {
            return Main::isInitialised() && Main::isConnected()
                && Main::get().getActorSync().hasAuthority(cellId);
        });

        mp.set_function("getActorAuthorityCell", sol::overload(
            [](const MWLua::LObject& object) -> std::string {
                if (!Main::isInitialised() || !Main::isConnected())
                    return {};
                return Main::get().getActorSync().getActorAuthorityCellId(object.ptr());
            },
            [](const MWLua::GObject& object) -> std::string {
                if (!Main::isInitialised() || !Main::isConnected())
                    return {};
                return Main::get().getActorSync().getActorAuthorityCellId(object.ptr());
            }));

        mp.set_function("hasActorAuthorityForObject", sol::overload(
            [](const MWLua::LObject& object) -> bool {
                if (!Main::isInitialised() || !Main::isConnected())
                    return false;
                return Main::get().getActorSync().hasAuthorityForObject(object.ptr());
            },
            [](const MWLua::GObject& object) -> bool {
                if (!Main::isInitialised() || !Main::isConnected())
                    return false;
                return Main::get().getActorSync().hasAuthorityForObject(object.ptr());
            }));

        mp.set_function("hasActorAuthorityForMpNum", [](uint32_t mpNum, const std::string& cellId) -> bool {
            if (!Main::isInitialised() || !Main::isConnected())
                return false;
            return Main::get().getActorSync().hasAuthorityForMpNum(mpNum, cellId);
        });

        mp.set_function("getObjectMpNum", sol::overload(
            [](const MWLua::LObject& object) -> sol::optional<uint32_t> {
                return getObjectMpNum(object);
            },
            [](const MWLua::GObject& object) -> sol::optional<uint32_t> {
                return getObjectMpNum(object);
            }));

        return LuaUtil::makeReadOnly(mp);
        }));
    }
}
