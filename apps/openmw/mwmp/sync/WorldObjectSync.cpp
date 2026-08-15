#include "WorldObjectSync.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <exception>
#include <optional>
#include <random>
#include <sstream>
#include <cstdio>
#include <string_view>

#include <components/debug/debuglog.hpp>
#include <components/esm/refid.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm/position.hpp>
#include <components/openmw-mp/Packets/Object/PacketObjectPlace.hpp>
#include <components/openmw-mp/Packets/Object/PacketObjectDelete.hpp>
#include <components/openmw-mp/Packets/Object/PacketObjectMove.hpp>
#include <components/openmw-mp/Packets/Object/PacketContainer.hpp>
#include <components/openmw-mp/Packets/Object/PacketWorldItemTake.hpp>
#include <components/openmw-mp/Packets/Object/PacketInventoryTake.hpp>
#include <components/openmw-mp/Packets/Actor/PacketCorpseDispose.hpp>

#include "../../mwbase/environment.hpp"
#include "../../mwbase/world.hpp"
#include "../../mwbase/windowmanager.hpp"
#include "../../mwgui/mode.hpp"
#include "../../mwworld/class.hpp"
#include "../../mwworld/ptr.hpp"
#include "../../mwworld/manualref.hpp"
#include "../../mwworld/esmstore.hpp"
#include "../../mwworld/cellstore.hpp"
#include "../../mwworld/scene.hpp"
#include "../../mwworld/worldimp.hpp"
#include "../../mwworld/inventorystore.hpp"
#include "../../mwworld/containerstore.hpp"
#include "../../mwmechanics/creaturestats.hpp"
#include "../../mwrender/npcanimation.hpp"

#include "../network/Client.hpp"
#include "../Main.hpp"
#include "ActorSync.hpp"
#include "PlayerSync.hpp"
#include "InventoryIdentity.hpp"

namespace mwmp
{

void WorldObjectSync::resetSessionState()
{
    mPendingInventoryTakes.clear();
}

namespace
{
    std::string makeCellId(const MWWorld::Ptr& ptr)
    {
        const MWWorld::CellStore* store = ptr.getCell();
        if (!store || !store->getCell())
            return {};
        const MWWorld::Cell* cell = store->getCell();
        if (cell->isExterior())
        {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "EXT:%d,%d", cell->getGridX(), cell->getGridY());
            return buffer;
        }
        return std::string(cell->getNameId());
    }

    bool isContainerTarget(const MWWorld::Ptr& ptr)
    {
        return !ptr.isEmpty()
            && (ptr.getType() == ESM::Container::sRecordId || ptr.getClass().isActor());
    }

    bool samePosition(const Position& left, const Position& right)
    {
        constexpr float epsilon = 0.01f;
        for (int i = 0; i < 3; ++i)
        {
            if (std::abs(left.pos[i] - right.pos[i]) > epsilon)
                return false;
            if (std::abs(left.rot[i] - right.rot[i]) > epsilon)
                return false;
        }
        return true;
    }

    bool containerStoreEmpty(const MWWorld::ContainerStore& store)
    {
        return store.begin() == store.end();
    }

    void clearDeadActorEquipmentVisuals(MWBase::World& world, const MWWorld::Ptr& target)
    {
        if (target.isEmpty() || !target.getClass().isActor() || !target.getClass().hasInventoryStore(target))
            return;
        if (!target.getClass().getCreatureStats(target).isDead())
            return;

        MWWorld::InventoryStore& inv = target.getClass().getInventoryStore(target);
        for (int slot = 0; slot < MWWorld::InventoryStore::Slots; ++slot)
        {
            if (inv.getSlot(slot) != inv.end())
                inv.unequipSlot(slot);
        }

        if (auto* anim = dynamic_cast<MWRender::NpcAnimation*>(world.getAnimation(target)))
            anim->equipmentChanged();
    }

    MWWorld::CellStore* findActiveCellById(MWWorld::World& world, const std::string& cellId)
    {
        auto& scene = world.getWorldScene();
        for (MWWorld::CellStore* store : scene.getActiveCells())
        {
            if (store == nullptr)
                continue;

            const MWWorld::Cell* cell = store->getCell();
            if (cell == nullptr)
                continue;

            if (cellId.rfind("EXT:", 0) == 0)
            {
                int gridX = 0;
                int gridY = 0;
                if (std::sscanf(cellId.c_str(), "EXT:%d,%d", &gridX, &gridY) != 2)
                    return nullptr;

                if (cell->isExterior() && cell->getGridX() == gridX && cell->getGridY() == gridY)
                    return store;
            }
            else if (!cell->isExterior() && std::string(cell->getNameId()) == cellId)
            {
                return store;
            }
        }

        return nullptr;
    }

    std::string cellIdForPtr(const MWWorld::Ptr& ptr)
    {
        if (ptr.isEmpty() || !ptr.isInCell() || ptr.getCell()->getCell() == nullptr)
            return {};
        const MWWorld::Cell* cell = ptr.getCell()->getCell();
        if (!cell->isExterior())
            return std::string(cell->getNameId());
        return "EXT:" + std::to_string(cell->getGridX()) + "," + std::to_string(cell->getGridY());
    }

    void appendOrMerge(std::vector<ContainerItem>& items, const ContainerItem& item)
    {
        if (item.refId.empty() || item.count <= 0)
            return;

        auto it = std::find_if(items.begin(), items.end(),
            [&](const ContainerItem& current)
            {
                return current.refId == item.refId && current.charge == item.charge;
            });

        if (it == items.end())
            items.push_back(item);
        else
            it->count += item.count;
    }

    std::string lowerAscii(std::string_view value)
    {
        std::string result(value);
        std::transform(result.begin(), result.end(), result.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return result;
    }

    void appendOrMergeComparable(std::vector<ContainerItem>& items, ContainerItem item)
    {
        if (item.refId.empty() || item.count <= 0)
            return;

        item.refId = lowerAscii(item.refId);
        auto it = std::find_if(items.begin(), items.end(),
            [&](const ContainerItem& current)
            {
                return current.refId == item.refId && current.charge == item.charge;
            });

        if (it == items.end())
            items.push_back(std::move(item));
        else
            it->count += item.count;
    }

    bool containerStoreMatchesRecord(
        const MWWorld::ContainerStore& store, const std::vector<ContainerItem>& expected)
    {
        std::vector<ContainerItem> currentItems;
        std::vector<ContainerItem> expectedItems;

        for (auto it = store.begin(); it != store.end(); ++it)
        {
            ContainerItem item;
            item.refId = it->getCellRef().getRefId().toString();
            item.count = it->getCellRef().getCount();
            item.charge = static_cast<int>(it->getCellRef().getCharge());
            appendOrMergeComparable(currentItems, std::move(item));
        }

        for (const ContainerItem& item : expected)
            appendOrMergeComparable(expectedItems, item);

        auto less = [](const ContainerItem& left, const ContainerItem& right)
        {
            if (left.refId != right.refId)
                return left.refId < right.refId;
            return left.charge < right.charge;
        };
        std::sort(currentItems.begin(), currentItems.end(), less);
        std::sort(expectedItems.begin(), expectedItems.end(), less);

        if (currentItems.size() != expectedItems.size())
            return false;

        for (std::size_t i = 0; i < currentItems.size(); ++i)
        {
            if (currentItems[i].refId != expectedItems[i].refId
                || currentItems[i].charge != expectedItems[i].charge
                || currentItems[i].count != expectedItems[i].count)
            {
                return false;
            }
        }

        return true;
    }
}

WorldObjectSync::WorldObjectSync(NetworkClient& client)
    : mClient(client)
{
    std::random_device random;
    const auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
    std::ostringstream prefix;
    prefix << "client-take-" << timestamp << '-' << random() << random();
    mTakeRequestPrefix = prefix.str();
}

// ---------------------------------------------------------------------------
// update â€” retry queued operations that failed because the cell wasn't loaded
// ---------------------------------------------------------------------------
void WorldObjectSync::update(float dt)
{
    // --- pending places ---
    mPendingPlace.erase(
        std::remove_if(mPendingPlace.begin(), mPendingPlace.end(),
            [&](PendingPlace& p) -> bool {
                p.timer += dt;
                if (p.timer < RETRY_RATE) return false;
                p.timer = 0.f;
                return tryPlaceObject(p.mpNum, p.refId, p.count, p.pos, p.cellId);
            }),
        mPendingPlace.end());

    // --- pending deletes ---
    mPendingDelete.erase(
        std::remove_if(mPendingDelete.begin(), mPendingDelete.end(),
            [&](PendingDelete& p) -> bool {
                p.timer += dt;
                if (p.timer < RETRY_RATE) return false;
                p.timer = 0.f;
                return tryDeleteObject(p.identity);
            }),
        mPendingDelete.end());

    // --- pending moves ---
    mPendingMove.erase(
        std::remove_if(mPendingMove.begin(), mPendingMove.end(),
            [&](PendingMove& p) -> bool {
                p.timer += dt;
                if (p.timer < RETRY_RATE) return false;
                p.timer = 0.f;
                return tryMoveObject(p.mpNum, p.pos);
            }),
        mPendingMove.end());

    // --- pending container updates ---
    mPendingContainer.erase(
        std::remove_if(mPendingContainer.begin(), mPendingContainer.end(),
            [&](PendingContainer& p) -> bool {
                p.timer += dt;
                if (p.timer < RETRY_RATE) return false;
                p.timer = 0.f;
                return tryApplyContainer(p.record, p.action);
            }),
        mPendingContainer.end());
}

// ---------------------------------------------------------------------------
// Outbound â€” local player places an object
// ---------------------------------------------------------------------------
void WorldObjectSync::onLocalObjectPlaced(const MWWorld::Ptr& ptr, const std::string& refId, int count,
                                          const Position& pos,
                                          const std::string& cellId)
{
    PacketObjectPlace pkt;
    // A whole-stack drop may retain its inventory identity. The server checks
    // that this ID belongs to the sender and allocates a new one for splits.
    pkt.object.mpNum   = inventoryInstanceId(ptr.getCellRef().getRefNum());
    pkt.object.refId   = refId;
    pkt.object.count   = count;
    pkt.object.position= pos;
    pkt.object.cellId  = cellId;
    mPendingLocalPlace.push_back({ptr, refId, count, pos, cellId});
    mClient.sendReliable(pkt.encode());
    Log(Debug::Info) << "[MP] WorldObjectSync: sent ObjectPlace refId=" << refId
                     << " cell=" << cellId
                     << " count=" << count;
}

void WorldObjectSync::onLocalObjectTaken(
    const MWWorld::Ptr& worldObject, const MWWorld::Ptr& inventoryObject)
{
    const uint32_t mpNum = getMpNumForObject(worldObject);
    if (mpNum == 0 || inventoryObject.isEmpty())
        return;

    mPendingTakenMpNums.insert(mpNum);
    // Preserve the Lua-visible RefNum of the newly inserted stack. Network
    // capture resolves this alias to the authoritative world mpNum, so the
    // transfer keeps its server identity without invalidating live script/UI
    // handles that were created during moveInto().
    setInventoryInstanceAlias(inventoryObject.getCellRef().getRefNum(), mpNum);
}

void WorldObjectSync::requestLocalObjectTake(const MWWorld::Ptr& worldObject)
{
    if (worldObject.isEmpty() || !worldObject.isInCell())
        return;

    WorldItemTakeRequest request;
    request.requestId = mTakeRequestPrefix + "-world-" + std::to_string(mNextTakeRequestId++);
    request.object.cellId = cellIdForPtr(worldObject);
    request.object.refId = worldObject.getCellRef().getRefId().serializeText();
    request.requestedCount = worldObject.getCellRef().getCount();
    request.expectedInventoryRevision
        = Main::get().getPlayerSync().localPlayer().inventoryChanges.revision;

    const std::uint32_t mpNum = getMpNumForObject(worldObject);
    if (mpNum != 0)
    {
        request.object.kind = PlacedObjectKind::ServerPlaced;
        request.object.mpNum = mpNum;
    }
    else
    {
        const ESM::RefNum refNum = worldObject.getCellRef().getRefNum();
        request.object.kind = PlacedObjectKind::ContentReference;
        request.object.refIndex = refNum.mIndex;
        request.object.refContentFile = refNum.mContentFile;
    }

    if (validateWorldItemTakeRequest(request) != WorldItemTakeError::None)
    {
        Log(Debug::Warning) << "[MP] WorldObjectSync: cannot request take without canonical identity"
                            << " refId=" << request.object.refId
                            << " cell=" << request.object.cellId;
        return;
    }

    PacketWorldItemTakeRequest packet;
    packet.request = request;
    mClient.sendReliable(packet.encode());
    Log(Debug::Verbose) << "[MP] WorldObjectSync: requested authoritative take"
                        << " request=" << request.requestId
                        << " refId=" << request.object.refId
                        << " cell=" << request.object.cellId;
}

void WorldObjectSync::markLocalPlayerInventoryDetached(const MWWorld::Ptr& ptr)
{
    if (ptr.isEmpty())
        return;
    mLocalPlayerInventoryDetached.insert(ptr.getCellRef().getRefNum());
}

bool WorldObjectSync::consumeLocalPlayerInventoryDetached(const MWWorld::Ptr& ptr)
{
    if (ptr.isEmpty())
        return false;
    return mLocalPlayerInventoryDetached.erase(ptr.getCellRef().getRefNum()) != 0;
}

void WorldObjectSync::forgetLocalPlayerInventoryDetached(const MWWorld::Ptr& ptr)
{
    if (ptr.isEmpty())
        return;
    mLocalPlayerInventoryDetached.erase(ptr.getCellRef().getRefNum());
}

void WorldObjectSync::onLocalObjectDeleted(const MWWorld::Ptr& ptr)
{
    if (mSuppressLocalDelete || ptr.isEmpty() || !ptr.isInCell())
        return;

    if (ptr.getClass().isActor())
    {
        if (ptr.getClass().getCreatureStats(ptr).isDead())
            onLocalCorpseDisposed(ptr);
        return;
    }

    const uint32_t mpNum = getMpNumForObject(ptr);

    if (mpNum == 0)
        return;

    std::string cellId;
    if (const MWWorld::Cell* cell = ptr.getCell()->getCell())
    {
        if (cell->isExterior())
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "EXT:%d,%d", cell->getGridX(), cell->getGridY());
            cellId = buf;
        }
        else
            cellId = std::string(cell->getNameId());
    }

    PacketObjectDelete pkt;
    pkt.mpNum = mpNum;
    pkt.cellId = cellId;
    pkt.takenIntoInventory = mPendingTakenMpNums.erase(mpNum) != 0;
    mClient.sendReliable(pkt.encode());
    Log(Debug::Verbose) << "[MP] WorldObjectSync: sent ObjectDelete mpNum=" << mpNum;
}

void WorldObjectSync::onLocalCorpseDisposed(const MWWorld::Ptr& ptr)
{
    if (mSuppressLocalDelete || ptr.isEmpty() || !ptr.isInCell() || !ptr.getClass().isActor()
        || !ptr.getClass().getCreatureStats(ptr).isDead())
        return;

    const uint32_t mpNum = getMpNumForObject(ptr);
    const std::string refId = ptr.getCellRef().getRefId().serializeText();
    uint32_t canonicalRefNum = ptr.getCellRef().getRefNum().mIndex;
    if (mpNum == 0 && Main::isInitialised())
    {
        const uint32_t resolvedRefNum = Main::get().getActorSync().getActorCanonicalRefNum(ptr);
        if (resolvedRefNum != 0)
            canonicalRefNum = resolvedRefNum;
    }

    std::string cellId;
    if (const MWWorld::Cell* cell = ptr.getCell()->getCell())
    {
        if (cell->isExterior())
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "EXT:%d,%d", cell->getGridX(), cell->getGridY());
            cellId = buf;
        }
        else
            cellId = std::string(cell->getNameId());
    }

    const ActorInstanceId actorNetId = Main::isInitialised()
        ? Main::get().getActorSync().actorNetIdForPtr(cellId, ptr) : 0;

    if (actorNetId == 0 && mpNum == 0 && (refId.empty() || canonicalRefNum == 0))
    {
        Log(Debug::Warning) << "[MP] WorldObjectSync: cannot dispose corpse without canonical identity"
                            << " refId=" << refId
                            << " refNum=" << canonicalRefNum
                            << " cell=" << cellId;
        return;
    }

    PacketCorpseDispose pkt;
    pkt.actorNetId = actorNetId;
    pkt.mpNum = mpNum;
    pkt.refId = refId;
    pkt.refNum = canonicalRefNum;
    pkt.cellId = cellId;

    const std::vector<uint8_t> encoded = pkt.encode();
    PacketHeader header;
    const bool hasHeader = BasePacket::peekHeader(encoded.data(), encoded.size(), header);
    mClient.sendReliable(encoded);

    Log(Debug::Info) << "[MP] WorldObjectSync: sent CorpseDispose"
                     << " actorNetId=" << pkt.actorNetId
                     << " mpNum=" << pkt.mpNum
                     << " refId=" << pkt.refId
                     << " refNum=" << pkt.refNum
                     << " cell=" << pkt.cellId
                     << " bytes=" << encoded.size()
                     << " headerValid=" << hasHeader
                     << " headerType=" << (hasHeader ? header.type : 0)
                     << " payloadSize=" << (hasHeader ? header.payloadSize : 0);
}

// ---------------------------------------------------------------------------
// Outbound â€” local player opens a container
// ---------------------------------------------------------------------------
void WorldObjectSync::onLocalContainerOpened(const std::string& cellId,
                                              const std::string& refId,
                                              uint32_t refNum, uint32_t mpNum)
{
    MWBase::World* world = MWBase::Environment::get().getWorld();
    if (!world) return;

    // Build a Set packet with the container's current full contents.
    PacketContainer pkt;
    pkt.container.cellId = cellId;
    pkt.container.refId  = refId;
    pkt.container.refNum = refNum;
    pkt.container.mpNum  = mpNum;
    pkt.mAction = static_cast<uint8_t>(ContainerAction::Set);

    MWWorld::Ptr target;

    if (mpNum != 0)
    {
        target = getObjectByMpNum(mpNum);
        if (!isContainerTarget(target))
            target = MWWorld::Ptr();
        if (target.isEmpty() && Main::isInitialised())
            target = Main::get().getActorSync().getActorByMpNum(mpNum);
    }

    if (target.isEmpty() && refNum != 0 && Main::isInitialised())
        target = Main::get().getActorSync().getActorByCanonicalRefNum(refNum);

    if (target.isEmpty())
    {
        // Walk active cells to find the container or actor corpse and snapshot its inventory.
        auto& scene = static_cast<MWWorld::World*>(world)->getWorldScene();
        for (MWWorld::CellStore* store : scene.getActiveCells())
        {
            store->forEach([&](MWWorld::Ptr ptr) -> bool
            {
                if (ptr.getType() != ESM::Container::sRecordId && !ptr.getClass().isActor())
                    return true;
                if (ptr.getCellRef().getRefId().toString() != refId)
                    return true;
                if (refNum != 0 && ptr.getCellRef().getRefNum().mIndex != refNum)
                    return true;

                target = ptr;
                return false;
            });

            if (!target.isEmpty())
                break;
        }
    }

    if (!target.isEmpty())
    {
        if (!isContainerTarget(target))
        {
            Log(Debug::Warning) << "[MP] WorldObjectSync: ignoring invalid local container snapshot target refId="
                                << refId
                                << " mpNum=" << mpNum
                                << " refNum=" << refNum
                                << " cell=" << cellId;
        }
        else
        {
        auto& cstore = target.getClass().getContainerStore(target);
        for (auto it = cstore.begin(); it != cstore.end(); ++it)
        {
            ContainerItem ci;
            ci.refId  = it->getCellRef().getRefId().toString();
            ci.count  = it->getCellRef().getCount();
            ci.charge = static_cast<int>(it->getCellRef().getCharge());
            appendOrMerge(pkt.container.items, ci);
        }
        }
    }

    mClient.sendReliable(pkt.encode());
    Log(Debug::Verbose) << "[MP] WorldObjectSync: sent Container(Set) refId=" << refId
                        << " items=" << pkt.container.items.size();
}

// ---------------------------------------------------------------------------
// Outbound â€” local player modifies container contents
// ---------------------------------------------------------------------------
void WorldObjectSync::onLocalContainerChanged(const std::string& cellId,
                                               const std::string& refId,
                                               uint32_t refNum,
                                               uint32_t mpNum,
                                               ContainerAction action,
                                               const std::vector<ContainerItem>& items)
{
    PacketContainer pkt;
    pkt.container.cellId  = cellId;
    pkt.container.refId   = refId;
    pkt.container.refNum  = refNum;
    pkt.container.mpNum   = mpNum;
    pkt.container.items   = items;
    pkt.mAction = static_cast<uint8_t>(action);
    mClient.sendReliable(pkt.encode());
    Log(Debug::Verbose) << "[MP] WorldObjectSync: sent Container(" << static_cast<int>(action)
                        << ") refId=" << refId
                        << " refNum=" << refNum
                        << " items=" << items.size();
}

// ---------------------------------------------------------------------------
// Inbound â€” server tells us to place an object
// ---------------------------------------------------------------------------
void WorldObjectSync::onServerObjectPlace(uint32_t mpNum, const std::string& refId,
                                           int count, const Position& pos,
                                           const std::string& cellId)
{
    auto localIt = std::find_if(
        mPendingLocalPlace.begin(), mPendingLocalPlace.end(),
        [&](const PendingLocalPlace& pending)
        {
            return !pending.ptr.isEmpty()
                && pending.refId == refId
                && pending.count == count
                && pending.cellId == cellId
                && samePosition(pending.pos, pos);
        });
    if (localIt != mPendingLocalPlace.end())
    {
        registerObject(mpNum, localIt->ptr);
        mPendingLocalPlace.erase(localIt);
        Log(Debug::Verbose) << "[MP] WorldObjectSync: registered local ObjectPlace mpNum=" << mpNum;
        return;
    }

    if (!tryPlaceObject(mpNum, refId, count, pos, cellId))
    {
        Log(Debug::Verbose) << "[MP] WorldObjectSync: queuing ObjectPlace mpNum=" << mpNum
                            << " refId=" << refId;
        mPendingPlace.push_back({mpNum, refId, count, pos, cellId, 0.f});
    }
}

// ---------------------------------------------------------------------------
void WorldObjectSync::onServerObjectDelete(const PlacedObjectIdentity& identity)
{
    if (!tryDeleteObject(identity))
    {
        Log(Debug::Verbose) << "[MP] WorldObjectSync: queuing ObjectDelete mpNum=" << identity.mpNum
                            << " refId=" << identity.refId;
        mPendingDelete.push_back({identity, 0.f});
    }
}

bool WorldObjectSync::requestInventoryTake(const MWWorld::Ptr& source, const MWWorld::Ptr& item,
    int count, InventoryTakeKind kind)
{
    if (!Main::isInitialised() || source.isEmpty() || item.isEmpty() || count <= 0)
        return false;

    InventoryTakeRequest request;
    request.requestId = mTakeRequestPrefix + "-inventory-"
        + std::to_string(mNextInventoryTakeRequestId++);
    request.kind = kind;
    request.source.cellId = makeCellId(source);
    request.source.refId = source.getCellRef().getRefId().serializeText();
    request.source.refNum = source.getCellRef().getRefNum().mIndex;
    request.source.mpNum = getMpNumForObject(source);
    if (source.getClass().isActor())
    {
        ActorSync& actorSync = Main::get().getActorSync();
        request.source.actorInstanceId = actorSync.actorNetIdForPtr(request.source.cellId, source);
        request.source.migrationGeneration
            = actorSync.actorMigrationGenerationForPtr(request.source.cellId, source);
        request.source.mpNum = actorSync.getActorMpNum(source);
        request.source.refNum = request.source.mpNum == 0
            ? actorSync.getActorCanonicalRefNum(source) : 0;
    }
    request.itemRefId = item.getCellRef().getRefId().serializeText();
    request.itemCharge = static_cast<std::int32_t>(item.getCellRef().getCharge());
    request.requestedCount = count;
    request.expectedInventoryRevision = Main::get().getPlayerSync().localPlayer().inventoryChanges.revision;
    if (validateInventoryTakeRequest(request) != InventoryTakeError::None)
    {
        Log(Debug::Warning) << "[MP] Cannot build canonical inventory take request source="
                            << request.source.refId << " item=" << request.itemRefId;
        return false;
    }

    mPendingInventoryTakes.push_back(request);
    sendInventoryTakeRequest(request);
    return true;
}

bool WorldObjectSync::requestPickpocketFinish(const MWWorld::Ptr& source)
{
    if (!Main::isInitialised() || source.isEmpty() || !source.getClass().isActor())
        return false;
    InventoryTakeRequest request;
    request.requestId = mTakeRequestPrefix + "-inventory-"
        + std::to_string(mNextInventoryTakeRequestId++);
    request.kind = InventoryTakeKind::PickpocketFinish;
    request.source.cellId = makeCellId(source);
    request.source.refId = source.getCellRef().getRefId().serializeText();
    ActorSync& actorSync = Main::get().getActorSync();
    request.source.actorInstanceId = actorSync.actorNetIdForPtr(request.source.cellId, source);
    request.source.migrationGeneration
        = actorSync.actorMigrationGenerationForPtr(request.source.cellId, source);
    request.source.mpNum = actorSync.getActorMpNum(source);
    request.source.refNum = request.source.mpNum == 0 ? actorSync.getActorCanonicalRefNum(source) : 0;
    request.expectedInventoryRevision = Main::get().getPlayerSync().localPlayer().inventoryChanges.revision;
    if (validateInventoryTakeRequest(request) != InventoryTakeError::None)
        return false;
    mPendingInventoryTakes.push_back(request);
    sendInventoryTakeRequest(request);
    return true;
}

void WorldObjectSync::sendInventoryTakeRequest(const InventoryTakeRequest& request)
{
    PacketInventoryTakeRequest packet;
    packet.request = request;
    mClient.sendReliable(packet.encode());
}

void WorldObjectSync::onServerWorldItemTakeResult(const WorldItemTakeResult& result)
{
    Log(result.accepted ? Debug::Info : Debug::Warning)
        << "[MP] WorldObjectSync: authoritative take result"
        << " request=" << result.requestId
        << " accepted=" << result.accepted
        << " replayed=" << result.replayed
        << " error=" << getWorldItemTakeErrorCode(result.error)
        << " refId=" << result.itemRefId
        << " count=" << result.itemCount
        << " inventoryRevision=" << result.inventoryRevision;
}

void WorldObjectSync::onServerInventoryTakeResult(const InventoryTakeResult& result)
{
    Log(result.accepted ? Debug::Info : Debug::Warning)
        << "[MP] Authoritative inventory take result request=" << result.requestId
        << " accepted=" << result.accepted << " replayed=" << result.replayed
        << " error=" << getInventoryTakeErrorCode(result.error)
        << " kind=" << static_cast<unsigned>(result.kind)
        << " item=" << result.itemRefId << " count=" << result.itemCount
        << " detected=" << result.detected << " roll=" << result.detectionRoll
        << " theft=" << result.theft << " revision=" << result.inventoryRevision;
    if (result.error != InventoryTakeError::SourceUnavailable
        && result.error != InventoryTakeError::StaleInventoryRevision)
    {
        std::erase_if(mPendingInventoryTakes,
            [&](const InventoryTakeRequest& request) { return request.requestId == result.requestId; });
    }
    else if (result.error == InventoryTakeError::StaleInventoryRevision)
    {
        const auto pending = std::find_if(mPendingInventoryTakes.begin(), mPendingInventoryTakes.end(),
            [&](const InventoryTakeRequest& request) { return request.requestId == result.requestId; });
        if (pending != mPendingInventoryTakes.end())
        {
            pending->expectedInventoryRevision = result.inventoryRevision;
            sendInventoryTakeRequest(*pending);
        }
    }
    if ((result.kind == InventoryTakeKind::Pickpocket
            || result.kind == InventoryTakeKind::PickpocketFinish) && result.detected)
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(MWGui::GM_Container);
    }
}

// ---------------------------------------------------------------------------
void WorldObjectSync::onServerObjectMove(uint32_t mpNum, const std::string& /*cellId*/,
                                          const Position& pos)
{
    if (!tryMoveObject(mpNum, pos))
        mPendingMove.push_back({mpNum, pos, 0.f});
}

// ---------------------------------------------------------------------------
void WorldObjectSync::onServerContainer(const ContainerRecord& record, ContainerAction action)
{
    if (action == ContainerAction::BootstrapRequest)
    {
        onLocalContainerOpened(record.cellId, record.refId, record.refNum, record.mpNum);
        return;
    }
    if (!tryApplyContainer(record, action))
    {
        Log(Debug::Verbose) << "[MP] WorldObjectSync: queuing Container refId=" << record.refId;
        mPendingContainer.push_back({record, action, 0.f});
    }
    if (action == ContainerAction::Set)
    {
        for (InventoryTakeRequest& request : mPendingInventoryTakes)
        {
            const bool sameStatic = request.source.actorInstanceId == 0
                && request.source.cellId == record.cellId && request.source.refId == record.refId
                && request.source.refNum == record.refNum && request.source.mpNum == record.mpNum;
            const bool sameActor = request.source.actorInstanceId != 0
                && request.source.cellId == record.cellId && request.source.refId == record.refId
                && (request.source.mpNum == 0 || request.source.mpNum == record.mpNum)
                && (request.source.refNum == 0 || request.source.refNum == record.refNum);
            if (sameStatic || sameActor)
            {
                request.expectedInventoryRevision
                    = Main::get().getPlayerSync().localPlayer().inventoryChanges.revision;
                sendInventoryTakeRequest(request);
            }
        }
    }
}

// ---------------------------------------------------------------------------
MWWorld::Ptr WorldObjectSync::getObjectByMpNum(uint32_t mpNum) const
{
    auto it = mObjects.find(mpNum);
    return (it != mObjects.end()) ? it->second : MWWorld::Ptr();
}

uint32_t WorldObjectSync::getMpNumForObject(const MWWorld::Ptr& ptr) const
{
    if (ptr.isEmpty())
        return 0;

    if (ptr.getClass().isActor() && Main::isInitialised())
    {
        const uint32_t actorMpNum = Main::get().getActorSync().getActorMpNum(ptr);
        if (actorMpNum != 0)
            return actorMpNum;
    }

    auto it = mMpNumsByObjectId.find(ptr.getCellRef().getRefNum());
    return it != mMpNumsByObjectId.end() ? it->second : 0;
}

void WorldObjectSync::registerObject(uint32_t mpNum, const MWWorld::Ptr& ptr)
{
    if (ptr.isEmpty())
        return;

    unregisterObject(mpNum);
    mObjects[mpNum] = ptr;
    mMpNumsByObjectId[ptr.getCellRef().getRefNum()] = mpNum;
}

void WorldObjectSync::unregisterObject(uint32_t mpNum)
{
    auto it = mObjects.find(mpNum);
    if (it == mObjects.end())
        return;

    if (!it->second.isEmpty())
        mMpNumsByObjectId.erase(it->second.getCellRef().getRefNum());

    mObjects.erase(it);
}

// ---------------------------------------------------------------------------
// tryPlaceObject â€” attempt to spawn the object in the world right now.
// Returns true on success (object is now registered in mObjects).
// ---------------------------------------------------------------------------
bool WorldObjectSync::tryPlaceObject(uint32_t mpNum, const std::string& refId,
                                      int count, const Position& pos,
                                      const std::string& cellId)
{
    MWBase::World* world = MWBase::Environment::get().getWorld();
    if (!world) return false;

    // Already placed (duplicate packet)
    if (mObjects.count(mpNum)) return true;

    // Find the ESM record
    const MWWorld::ESMStore& store = world->getStore();
    std::optional<MWWorld::ManualRef> ref;
    try
    {
        ref.emplace(store, ESM::RefId::stringRefId(refId), count);
    }
    catch (const std::exception& e)
    {
        Log(Debug::Verbose) << "[MP] WorldObjectSync: delaying place for refId=" << refId
                            << " reason=" << e.what();
        return false;
    }

    if (ref->getPtr().isEmpty())
    {
        Log(Debug::Warning) << "[MP] WorldObjectSync: unknown refId '" << refId << "'";
        return false;
    }

    // Place into the target active cell from the packet. This matters for
    // exterior grids where the observer's current cell may differ from the
    // object's actual destination cell.
    auto* worldImpl = static_cast<MWWorld::World*>(world);
    MWWorld::CellStore* cell = findActiveCellById(*worldImpl, cellId);
    if (!cell) return false;

    ESM::Position esmPos;
    for (int i = 0; i < 3; ++i) esmPos.pos[i] = pos.pos[i];
    for (int i = 0; i < 3; ++i) esmPos.rot[i] = pos.rot[i];

    MWWorld::Ptr placed = world->placeObject(ref->getPtr(), cell, esmPos);
    if (placed.isEmpty())
    {
        Log(Debug::Warning) << "[MP] WorldObjectSync: placeObject failed for " << refId;
        return false;
    }

    registerObject(mpNum, placed);
    Log(Debug::Info) << "[MP] WorldObjectSync: placed refId=" << refId
                     << " mpNum=" << mpNum;
    return true;
}

// ---------------------------------------------------------------------------
bool WorldObjectSync::tryDeleteObject(const PlacedObjectIdentity& identity)
{
    MWBase::World* world = MWBase::Environment::get().getWorld();
    if (!world) return false;

    MWWorld::Ptr object;
    if (identity.kind == PlacedObjectKind::ServerPlaced)
    {
        const auto it = mObjects.find(identity.mpNum);
        if (it == mObjects.end())
            return false;
        object = it->second;
    }
    else
    {
        auto* worldImpl = static_cast<MWWorld::World*>(world);
        MWWorld::CellStore* cell = findActiveCellById(*worldImpl, identity.cellId);
        if (!cell)
            return false;
        const ESM::RefNum requested { identity.refIndex, identity.refContentFile };
        cell->forEach([&](MWWorld::Ptr candidate) {
            if (candidate.getCellRef().getRefNum() == requested
                && candidate.getCellRef().getRefId().serializeText() == identity.refId)
            {
                object = candidate;
                return false;
            }
            return true;
        });
        // A replayed tombstone against an already-loaded cell is complete.
        if (object.isEmpty())
            return true;
    }

    if (!object.isEmpty())
    {
        Position lastKnownPosition;
        const ESM::Position& esmPosition = object.getRefData().getPosition();
        for (int index = 0; index < 3; ++index)
        {
            lastKnownPosition.pos[index] = esmPosition.pos[index];
            lastKnownPosition.rot[index] = esmPosition.rot[index];
        }
        if (identity.mpNum != 0)
            mLastKnownObjectPositions[identity.mpNum] = lastKnownPosition;
    }
    if (identity.mpNum != 0)
        unregisterObject(identity.mpNum);


    if (!object.isEmpty())
    {
        mSuppressLocalDelete = true;
        world->deleteObject(object);
        mSuppressLocalDelete = false;
        Log(Debug::Info) << "[MP] WorldObjectSync: applied authoritative deletion"
                         << " mpNum=" << identity.mpNum
                         << " refId=" << identity.refId
                         << " cell=" << identity.cellId;
    }
    return true;
}

// ---------------------------------------------------------------------------
bool WorldObjectSync::tryMoveObject(uint32_t mpNum, const Position& pos)
{
    auto it = mObjects.find(mpNum);
    if (it == mObjects.end() || it->second.isEmpty()) return false;

    MWBase::World* world = MWBase::Environment::get().getWorld();
    if (!world) return false;

    osg::Vec3f osgPos(pos.pos[0], pos.pos[1], pos.pos[2]);
    osg::Vec3f osgRot(pos.rot[0], pos.rot[1], pos.rot[2]);

    it->second = world->moveObject(it->second, osgPos);
    world->rotateObject(it->second, osgRot);
    return true;
}

// ---------------------------------------------------------------------------
bool WorldObjectSync::tryApplyContainer(const ContainerRecord& record, ContainerAction action)
{
    MWBase::World* world = MWBase::Environment::get().getWorld();
    if (!world) return false;

    MWWorld::Ptr target;

    if (record.mpNum != 0)
    {
        target = getObjectByMpNum(record.mpNum);
        if (!isContainerTarget(target))
            target = MWWorld::Ptr();
        if (target.isEmpty() && Main::isInitialised())
            target = Main::get().getActorSync().getActorByMpNum(record.mpNum);
    }

    if (target.isEmpty() && record.refNum != 0 && Main::isInitialised())
        target = Main::get().getActorSync().getActorByCanonicalRefNum(record.refNum);

    if (target.isEmpty())
    {
        auto& scene = static_cast<MWWorld::World*>(world)->getWorldScene();
        for (MWWorld::CellStore* store : scene.getActiveCells())
        {
            store->forEach([&](MWWorld::Ptr ptr) -> bool
            {
                if (ptr.getType() != ESM::Container::sRecordId && !ptr.getClass().isActor())
                    return true;
                if (ptr.getCellRef().getRefId().toString() != record.refId)
                    return true;
                if (record.refNum != 0 && ptr.getCellRef().getRefNum().mIndex != record.refNum)
                    return true;

                target = ptr;
                return false;
            });

            if (!target.isEmpty())
                break;
        }
    }

    if (target.isEmpty())
        return false;

    if (!isContainerTarget(target))
    {
        Log(Debug::Warning) << "[MP] WorldObjectSync: dropping invalid Container replay for non-container refId="
                            << record.refId
                            << " mpNum=" << record.mpNum
                            << " refNum=" << record.refNum
                            << " cell=" << record.cellId;
        return true;
    }

    auto& cstore = target.getClass().getContainerStore(target);
    const MWWorld::ESMStore& esmStore = world->getStore();
    for (const ContainerItem& item : record.items)
    {
        if (item.refId.empty() || item.count <= 0
            || esmStore.find(ESM::RefId::stringRefId(item.refId)) != 0)
            continue;
        Log(Debug::Verbose) << "[MP] WorldObjectSync: staging container until record is present refId="
                            << item.refId;
        return false;
    }

    bool preservedSetHandles = false;
    if (action == ContainerAction::Set)
    {
        preservedSetHandles = containerStoreMatchesRecord(cstore, record.items);
        if (!preservedSetHandles)
        {
            if (record.items.empty())
                clearDeadActorEquipmentVisuals(*world, target);
            cstore.clear();
            for (const auto& ci : record.items)
            {
                MWWorld::ManualRef ref(esmStore, ESM::RefId::stringRefId(ci.refId), ci.count);
                if (!ref.getPtr().isEmpty())
                    cstore.add(ref.getPtr(), ci.count);
            }
        }
        if (record.items.empty())
            clearDeadActorEquipmentVisuals(*world, target);
    }
    else if (action == ContainerAction::Add)
    {
        for (const auto& ci : record.items)
        {
            MWWorld::ManualRef ref(esmStore, ESM::RefId::stringRefId(ci.refId), ci.count);
            if (!ref.getPtr().isEmpty())
                cstore.add(ref.getPtr(), ci.count);
        }
    }
    else if (action == ContainerAction::Remove)
    {
        for (const auto& ci : record.items)
            cstore.remove(ESM::RefId::stringRefId(ci.refId), ci.count);
        if (containerStoreEmpty(cstore))
            clearDeadActorEquipmentVisuals(*world, target);
    }

    Log(Debug::Info) << "[MP] WorldObjectSync: applied Container action="
                     << static_cast<int>(action)
                     << " refId=" << record.refId
                     << " mpNum=" << record.mpNum
                     << " preservedSetHandles=" << preservedSetHandles;
    return true;
}

} // namespace mwmp
