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
#include <map>
#include <memory>
#include <string_view>

#include <MyGUI_LanguageManager.h>

#include <components/debug/debuglog.hpp>
#include <components/misc/strings/format.hpp>
#include <components/esm/refid.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm/position.hpp>
#include <components/openmw-mp/Packets/Object/PacketObjectPlace.hpp>
#include <components/openmw-mp/Packets/Object/PacketObjectDelete.hpp>
#include <components/openmw-mp/Packets/Object/PacketObjectMove.hpp>
#include <components/openmw-mp/Packets/Object/PacketContainer.hpp>
#include <components/openmw-mp/Packets/Object/PacketWorldItemTake.hpp>
#include <components/openmw-mp/Packets/Object/PacketInventoryTake.hpp>
#include <components/openmw-mp/Packets/Object/PacketInventoryPut.hpp>
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
#include "../../mwrender/animation.hpp"
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
    mPendingHarvests.clear();
    mPendingInventoryTakes.clear();
    mInventoryTakeSources.clear();
    mInventoryTakesAwaitingSource.clear();
    mInventoryTakeCallbacks.clear();
    mPendingInventoryPuts.clear();
    mInventoryPutDestinations.clear();
    mInventoryPutsAwaitingDestination.clear();
    mInventoryPutCallbacks.clear();
    mOpenContainerTargets.clear();
    mContainerRevisions.clear();
    mNextContainerRevision = 1;
    mSuppressPickpocketFinish = false;
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

    std::string makeContainerRevisionKey(
        std::string_view cellId, std::string_view refId, std::uint32_t refNum, std::uint32_t mpNum)
    {
        return std::string(cellId) + '\0' + std::string(refId) + '\0'
            + std::to_string(refNum) + '\0' + std::to_string(mpNum);
    }

    bool isContainerTarget(const MWWorld::Ptr& ptr)
    {
        return !ptr.isEmpty()
            && (ptr.getType() == ESM::Container::sRecordId || ptr.getClass().isActor());
    }

    bool isRemotePlayerInventorySource(const MWWorld::Ptr& ptr)
    {
        return !ptr.isEmpty() && ptr.getClass().isActor()
            && ptr.getClass().getCreatureStats(ptr).getMovementFlag(
                MWMechanics::CreatureStats::Flag_NetworkPlayerNpc);
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

    bool reconcileContainerStoreInPlace(MWWorld::ContainerStore& store,
        const std::vector<ContainerItem>& expected, const MWWorld::ESMStore& esmStore)
    {
        struct DesiredItem
        {
            std::string refId;
            int charge = -1;
            int remaining = 0;
        };

        using Identity = std::pair<std::string, int>;
        std::map<Identity, DesiredItem> desired;
        for (const ContainerItem& item : expected)
        {
            if (item.refId.empty() || item.count <= 0)
                continue;
            const Identity identity { lowerAscii(item.refId), item.charge };
            auto& entry = desired[identity];
            if (entry.refId.empty())
            {
                entry.refId = item.refId;
                entry.charge = item.charge;
            }
            entry.remaining += item.count;
        }

        std::vector<MWWorld::Ptr> currentStacks;
        for (auto it = store.begin(); it != store.end(); ++it)
            currentStacks.push_back(*it);

        for (const MWWorld::Ptr& ptr : currentStacks)
        {
            if (ptr.isEmpty())
                continue;
            const int currentCount = ptr.getCellRef().getCount();
            if (currentCount <= 0)
                continue;

            const Identity identity { lowerAscii(ptr.getCellRef().getRefId().toString()),
                static_cast<int>(ptr.getCellRef().getCharge()) };
            auto desiredIt = desired.find(identity);
            const int keep = desiredIt == desired.end()
                ? 0 : std::min(currentCount, desiredIt->second.remaining);
            if (desiredIt != desired.end())
                desiredIt->second.remaining -= keep;

            const int removeCount = currentCount - keep;
            if (removeCount > 0)
                store.remove(ptr, removeCount, false, false);
        }

        for (auto& [identity, item] : desired)
        {
            (void)identity;
            if (item.remaining <= 0)
                continue;

            MWWorld::ManualRef ref(esmStore, ESM::RefId::stringRefId(item.refId), item.remaining);
            MWWorld::Ptr ptr = ref.getPtr();
            if (ptr.isEmpty())
                return false;
            ptr.getCellRef().setCharge(item.charge);
            store.add(ptr, item.remaining, false, false);
        }

        return containerStoreMatchesRecord(store, expected);
    }

    struct HarvestFeedback
    {
        std::size_t remaining = 0;
        std::map<std::string, int> taken;
    };

    void showHarvestFeedback(const std::map<std::string, int>& taken)
    {
        if (taken.empty())
            return;

        std::ostringstream stream;
        int lineCount = 0;
        constexpr int maxLines = 10;
        for (const auto& [itemName, itemCount] : taken)
        {
            ++lineCount;
            if (lineCount == maxLines)
                stream << "\n...";
            else if (lineCount > maxLines)
                break;

            std::string message;
            if (itemCount == 1)
            {
                message = MyGUI::LanguageManager::getInstance().replaceTags("\n#{sNotifyMessage60}");
                message = Misc::StringUtils::format(message, itemName);
            }
            else
            {
                message = MyGUI::LanguageManager::getInstance().replaceTags("\n#{sNotifyMessage61}");
                message = Misc::StringUtils::format(message, itemCount, itemName);
            }
            stream << message;
        }

        std::string tooltip = stream.str();
        if (!tooltip.empty() && tooltip.front() == '\n')
            tooltip.erase(0, 1);
        if (!tooltip.empty())
            MWBase::Environment::get().getWindowManager()->messageBox(tooltip);
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
// update — retry queued operations that failed because the cell wasn't loaded
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
                const bool applied = tryApplyContainer(p.record, p.action);
                if (applied && p.action == ContainerAction::Set)
                    processPendingHarvest(p.record);
                return applied;
            }),
        mPendingContainer.end());
}

// ---------------------------------------------------------------------------
// Outbound — local player places an object
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
// Outbound — local player opens a container
// ---------------------------------------------------------------------------
void WorldObjectSync::onLocalContainerOpened(const MWWorld::Ptr& container)
{
    if (container.isEmpty() || container.getCell() == nullptr || !isContainerTarget(container))
        return;

    ContainerRecord record;
    record.cellId = makeCellId(container);
    record.refId = container.getCellRef().getRefId().serializeText();
    record.refNum = container.getCellRef().getRefNum().mIndex;
    record.mpNum = getMpNumForObject(container);
    mOpenContainerTargets[makeContainerRevisionKey(
        record.cellId, record.refId, record.refNum, record.mpNum)] = container;
    sendLocalContainerSnapshot(record, container);
}

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

    MWWorld::Ptr target = findContainerTarget(pkt.container);

    if (target.isEmpty())
    {
        Log(Debug::Warning) << "[MP] WorldObjectSync: cannot answer container bootstrap; target not found refId="
                            << refId << " refNum=" << refNum << " mpNum=" << mpNum << " cell=" << cellId;
        return;
    }
    if (!isContainerTarget(target))
    {
        Log(Debug::Warning) << "[MP] WorldObjectSync: ignoring invalid local container snapshot target refId="
                            << refId << " mpNum=" << mpNum << " refNum=" << refNum << " cell=" << cellId;
        return;
    }

    sendLocalContainerSnapshot(pkt.container, target);
}

void WorldObjectSync::sendLocalContainerSnapshot(const ContainerRecord& record, const MWWorld::Ptr& target)
{
    if (target.isEmpty() || !isContainerTarget(target))
        return;

    PacketContainer pkt;
    pkt.container.cellId = record.cellId;
    pkt.container.refId = record.refId;
    pkt.container.refNum = record.refNum;
    pkt.container.mpNum = record.mpNum;
    pkt.mAction = static_cast<uint8_t>(ContainerAction::Set);

    auto& cstore = target.getClass().getContainerStore(target);
    // Authority bootstrap must snapshot concrete container contents. Organic/leveled
    // containers may still be unresolved here; iterating an unresolved store can
    // serialize an empty Set and incorrectly make the server authoritative for an
    // empty container before the activating client can request the harvest.
    cstore.resolve();
    for (auto it = cstore.begin(); it != cstore.end(); ++it)
    {
        ContainerItem ci;
        ci.refId = it->getCellRef().getRefId().toString();
        ci.count = it->getCellRef().getCount();
        ci.charge = static_cast<int>(it->getCellRef().getCharge());
        appendOrMerge(pkt.container.items, ci);
    }

    mClient.sendReliable(pkt.encode());
    Log(Debug::Verbose) << "[MP] WorldObjectSync: sent Container(Set) refId=" << record.refId
                        << " items=" << pkt.container.items.size();
}

// ---------------------------------------------------------------------------
// Outbound — local player modifies container contents
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
// Inbound — server tells us to place an object
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
    int count, InventoryTakeKind kind, InventoryTakeCallback callback)
{
    if (!Main::isInitialised() || source.isEmpty() || item.isEmpty() || count <= 0)
        return false;
    if (isRemotePlayerInventorySource(source))
    {
        Log(Debug::Warning) << "[MP] Inventory take rejected: remote player inventories are not stealable";
        return false;
    }

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

    if (callback)
        mInventoryTakeCallbacks.emplace(request.requestId, std::move(callback));
    mInventoryTakeSources[request.requestId] = source;
    mPendingInventoryTakes.push_back(request);
    sendInventoryTakeRequest(request);
    return true;
}

bool WorldObjectSync::requestInventoryPut(const MWWorld::Ptr& destination, const MWWorld::Ptr& item,
    int count, InventoryPutCallback callback)
{
    if (!Main::isInitialised() || destination.isEmpty() || item.isEmpty() || count <= 0
        || destination.getType() != ESM::Container::sRecordId)
        return false;

    const MWWorld::Ptr sourceOwner
        = item.getContainerStore() ? item.getContainerStore()->getPtr() : MWWorld::Ptr{};
    const MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
    if (sourceOwner.isEmpty() || sourceOwner != player)
        return false;

    InventoryPutRequest request;
    request.requestId = mTakeRequestPrefix + "-put-" + std::to_string(mNextInventoryPutRequestId++);
    request.destination.cellId = makeCellId(destination);
    request.destination.refId = destination.getCellRef().getRefId().serializeText();
    request.destination.refNum = destination.getCellRef().getRefNum().mIndex;
    request.destination.mpNum = getMpNumForObject(destination);
    request.itemRefId = item.getCellRef().getRefId().serializeText();
    request.itemInstanceId = inventoryInstanceId(item.getCellRef().getRefNum());
    request.itemCharge = static_cast<std::int32_t>(item.getCellRef().getCharge());
    request.requestedCount = count;
    request.expectedInventoryRevision = Main::get().getPlayerSync().localPlayer().inventoryChanges.revision;
    if (validateInventoryPutRequest(request) != InventoryPutError::None)
    {
        Log(Debug::Warning) << "[MP] Cannot build canonical inventory put request destination="
                            << request.destination.refId << " item=" << request.itemRefId
                            << " instanceId=" << request.itemInstanceId;
        return false;
    }

    if (callback)
        mInventoryPutCallbacks.emplace(request.requestId, std::move(callback));
    mInventoryPutDestinations[request.requestId] = destination;
    mPendingInventoryPuts.push_back(request);
    sendInventoryPutRequest(request);
    return true;
}

bool WorldObjectSync::requestHarvest(const MWWorld::Ptr& source)
{
    if (!Main::isInitialised() || source.isEmpty() || source.getType() != ESM::Container::sRecordId)
        return false;

    ContainerRecord record;
    record.cellId = makeCellId(source);
    record.refId = source.getCellRef().getRefId().serializeText();
    record.refNum = source.getCellRef().getRefNum().mIndex;
    record.mpNum = getMpNumForObject(source);
    if (record.cellId.empty() || record.refId.empty())
        return false;

    const auto sameSource = [&](const PendingHarvest& pending) {
        return pending.source.cellId == record.cellId && pending.source.refId == record.refId
            && pending.source.refNum == record.refNum && pending.source.mpNum == record.mpNum;
    };
    if (std::find_if(mPendingHarvests.begin(), mPendingHarvests.end(), sameSource) == mPendingHarvests.end())
        mPendingHarvests.push_back({ record });

    PacketContainer packet;
    packet.container = record;
    packet.mAction = static_cast<std::uint8_t>(ContainerAction::BootstrapRequest);
    mClient.sendReliable(packet.encode());
    Log(Debug::Info) << "[MP] WorldObjectSync: requested authoritative harvest bootstrap refId=" << record.refId
                     << " refNum=" << record.refNum << " mpNum=" << record.mpNum;
    return true;
}

bool WorldObjectSync::requestPickpocketFinish(const MWWorld::Ptr& source)
{
    if (!Main::isInitialised() || source.isEmpty() || !source.getClass().isActor())
    {
        Log(Debug::Warning) << "[MP] PickpocketFinish build rejected before identity sourceEmpty="
                            << source.isEmpty()
                            << " initialized=" << Main::isInitialised();
        return false;
    }
    if (isRemotePlayerInventorySource(source))
    {
        Log(Debug::Warning) << "[MP] PickpocketFinish rejected: remote player inventories are not stealable";
        return false;
    }
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
    const InventoryTakeError validation = validateInventoryTakeRequest(request);
    Log(validation == InventoryTakeError::None ? Debug::Info : Debug::Warning)
        << "[MP] PickpocketFinish build request=" << request.requestId
        << " source=" << request.source.refId
        << " cell=" << request.source.cellId
        << " actorInstanceId=" << request.source.actorInstanceId
        << " migration=" << request.source.migrationGeneration
        << " refNum=" << request.source.refNum
        << " mpNum=" << request.source.mpNum
        << " inventoryRevision=" << request.expectedInventoryRevision
        << " validation=" << getInventoryTakeErrorCode(validation);
    if (validation != InventoryTakeError::None)
        return false;
    mInventoryTakeSources[request.requestId] = source;
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

void WorldObjectSync::sendInventoryPutRequest(const InventoryPutRequest& request)
{
    PacketInventoryPutRequest packet;
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

    const bool terminal = result.error != InventoryTakeError::SourceUnavailable
        && result.error != InventoryTakeError::StaleInventoryRevision;
    if (result.error == InventoryTakeError::SourceUnavailable)
        mInventoryTakesAwaitingSource.insert(result.requestId);
    else
        mInventoryTakesAwaitingSource.erase(result.requestId);
    if (terminal)
    {
        std::erase_if(mPendingInventoryTakes,
            [&](const InventoryTakeRequest& request) { return request.requestId == result.requestId; });
        mInventoryTakeSources.erase(result.requestId);
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
        mSuppressPickpocketFinish = true;
        MWBase::Environment::get().getWindowManager()->removeGuiMode(MWGui::GM_Container);
        mSuppressPickpocketFinish = false;
    }

    if (terminal)
    {
        const auto callback = mInventoryTakeCallbacks.find(result.requestId);
        if (callback != mInventoryTakeCallbacks.end())
        {
            InventoryTakeCallback fn = std::move(callback->second);
            mInventoryTakeCallbacks.erase(callback);
            try
            {
                fn(result);
            }
            catch (const std::exception& e)
            {
                Log(Debug::Error) << "[MP] Inventory take callback failed request=" << result.requestId
                                  << " error=" << e.what();
            }
        }
    }
}

void WorldObjectSync::onServerInventoryPutResult(const InventoryPutResult& result)
{
    Log(result.accepted ? Debug::Info : Debug::Warning)
        << "[MP] Authoritative inventory put result request=" << result.requestId
        << " accepted=" << result.accepted << " replayed=" << result.replayed
        << " error=" << getInventoryPutErrorCode(result.error)
        << " destination=" << result.destination.refId
        << " item=" << result.itemRefId << " instanceId=" << result.itemInstanceId
        << " count=" << result.itemCount << " revision=" << result.inventoryRevision;

    const bool terminal = result.error != InventoryPutError::DestinationUnavailable
        && result.error != InventoryPutError::StaleInventoryRevision;
    if (result.error == InventoryPutError::DestinationUnavailable)
        mInventoryPutsAwaitingDestination.insert(result.requestId);
    else
        mInventoryPutsAwaitingDestination.erase(result.requestId);

    if (terminal)
    {
        std::erase_if(mPendingInventoryPuts,
            [&](const InventoryPutRequest& request) { return request.requestId == result.requestId; });
        mInventoryPutDestinations.erase(result.requestId);
    }
    else if (result.error == InventoryPutError::StaleInventoryRevision)
    {
        const auto pending = std::find_if(mPendingInventoryPuts.begin(), mPendingInventoryPuts.end(),
            [&](const InventoryPutRequest& request) { return request.requestId == result.requestId; });
        if (pending != mPendingInventoryPuts.end())
        {
            pending->expectedInventoryRevision = result.inventoryRevision;
            sendInventoryPutRequest(*pending);
        }
    }

    if (terminal)
    {
        const auto callback = mInventoryPutCallbacks.find(result.requestId);
        if (callback != mInventoryPutCallbacks.end())
        {
            InventoryPutCallback fn = std::move(callback->second);
            mInventoryPutCallbacks.erase(callback);
            try
            {
                fn(result);
            }
            catch (const std::exception& e)
            {
                Log(Debug::Error) << "[MP] Inventory put callback failed request=" << result.requestId
                                  << " error=" << e.what();
            }
        }
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
    const bool applied = tryApplyContainer(record, action);
    if (!applied)
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
            if ((sameStatic || sameActor)
                && mInventoryTakesAwaitingSource.erase(request.requestId) != 0)
            {
                request.expectedInventoryRevision
                    = Main::get().getPlayerSync().localPlayer().inventoryChanges.revision;
                sendInventoryTakeRequest(request);
            }
        }
        for (InventoryPutRequest& request : mPendingInventoryPuts)
        {
            const bool sameDestination = request.destination.cellId == record.cellId
                && request.destination.refId == record.refId
                && request.destination.refNum == record.refNum
                && request.destination.mpNum == record.mpNum;
            if (sameDestination
                && mInventoryPutsAwaitingDestination.erase(request.requestId) != 0)
            {
                request.expectedInventoryRevision
                    = Main::get().getPlayerSync().localPlayer().inventoryChanges.revision;
                sendInventoryPutRequest(request);
            }
        }
        if (applied)
            processPendingHarvest(record);
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

std::uint64_t WorldObjectSync::getContainerRevision(const MWWorld::Ptr& ptr) const
{
    if (ptr.isEmpty())
        return 0;
    const std::string key = makeContainerRevisionKey(makeCellId(ptr),
        ptr.getCellRef().getRefId().serializeText(), ptr.getCellRef().getRefNum().mIndex,
        getMpNumForObject(ptr));
    const auto it = mContainerRevisions.find(key);
    return it == mContainerRevisions.end() ? 0 : it->second;
}

bool WorldObjectSync::getObjectLastKnownPosition(uint32_t mpNum, Position& position) const
{
    const auto live = mObjects.find(mpNum);
    if (live != mObjects.end() && !live->second.isEmpty())
    {
        const ESM::Position& esmPosition = live->second.getRefData().getPosition();
        for (int index = 0; index < 3; ++index)
        {
            position.pos[index] = esmPosition.pos[index];
            position.rot[index] = esmPosition.rot[index];
        }
        return true;
    }

    const auto cached = mLastKnownObjectPositions.find(mpNum);
    if (cached == mLastKnownObjectPositions.end())
        return false;

    position = cached->second;
    return true;
}

void WorldObjectSync::registerObject(uint32_t mpNum, const MWWorld::Ptr& ptr)
{
    if (ptr.isEmpty())
        return;

    unregisterObject(mpNum);
    mLastKnownObjectPositions.erase(mpNum);
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
// tryPlaceObject — attempt to spawn the object in the world right now.
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
MWWorld::Ptr WorldObjectSync::findContainerTarget(const ContainerRecord& record) const
{
    MWBase::World* world = MWBase::Environment::get().getWorld();
    if (!world)
        return {};

    // An unresolved authoritative take already owns the exact source Ptr that the
    // local UI/Lua object used to build its canonical request. Prefer that binding
    // over rediscovering the actor through CellStore/ActorSync: migrated or
    // reconciled actors can remain valid UI objects without being reverse-bound in
    // either registry at the instant the server requests a bootstrap snapshot.
    for (const InventoryTakeRequest& request : mPendingInventoryTakes)
    {
        if (request.source.cellId != record.cellId || request.source.refId != record.refId
            || request.source.refNum != record.refNum || request.source.mpNum != record.mpNum)
            continue;

        const auto sourceIt = mInventoryTakeSources.find(request.requestId);
        if (sourceIt == mInventoryTakeSources.end())
            continue;

        const MWWorld::Ptr& source = sourceIt->second;
        if (source.isEmpty() || source.getCell() == nullptr || !isContainerTarget(source)
            || makeCellId(source) != record.cellId
            || source.getCellRef().getRefId().serializeText() != record.refId)
            continue;

        Log(Debug::Verbose) << "[MP] WorldObjectSync: resolved container from pending inventory take"
                            << " request=" << request.requestId
                            << " refId=" << record.refId
                            << " refNum=" << record.refNum
                            << " mpNum=" << record.mpNum
                            << " cell=" << record.cellId;
        return source;
    }

    for (const InventoryPutRequest& request : mPendingInventoryPuts)
    {
        if (request.destination.cellId != record.cellId || request.destination.refId != record.refId
            || request.destination.refNum != record.refNum || request.destination.mpNum != record.mpNum)
            continue;
        const auto destinationIt = mInventoryPutDestinations.find(request.requestId);
        if (destinationIt == mInventoryPutDestinations.end())
            continue;
        const MWWorld::Ptr& destination = destinationIt->second;
        if (!destination.isEmpty() && destination.getCell() != nullptr && isContainerTarget(destination)
            && makeCellId(destination) == record.cellId
            && destination.getCellRef().getRefId().serializeText() == record.refId)
            return destination;
    }

    const auto openIt = mOpenContainerTargets.find(makeContainerRevisionKey(
        record.cellId, record.refId, record.refNum, record.mpNum));
    if (openIt != mOpenContainerTargets.end())
    {
        const MWWorld::Ptr& openTarget = openIt->second;
        if (!openTarget.isEmpty() && openTarget.getCell() != nullptr && isContainerTarget(openTarget)
            && makeCellId(openTarget) == record.cellId
            && openTarget.getCellRef().getRefId().serializeText() == record.refId)
            return openTarget;
    }

    MWWorld::Ptr target;
    if (record.mpNum != 0)
    {
        target = getObjectByMpNum(record.mpNum);
        if (!isContainerTarget(target))
            target = MWWorld::Ptr();
        if (target.isEmpty() && Main::isInitialised())
            target = Main::get().getActorSync().getActorByMpNum(record.mpNum);
        return target;
    }

    // Vanilla placed containers/NPCs must resolve to the active-cell Ptr first.
    // UI models are bound to that concrete world object. ActorSync may maintain a
    // separate bound runtime Ptr for the same canonical refNum during handoff or
    // reconciliation, and mutating that copy would leave an open container UI stale.
    auto& scene = static_cast<MWWorld::World*>(world)->getWorldScene();
    for (MWWorld::CellStore* store : scene.getActiveCells())
    {
        store->forEach([&](MWWorld::Ptr ptr) -> bool {
            if (ptr.getType() != ESM::Container::sRecordId && !ptr.getClass().isActor())
                return true;
            if (ptr.getCellRef().getRefId().toString() != record.refId)
                return true;
            if (record.refNum != 0)
            {
                uint32_t candidateRefNum = ptr.getCellRef().getRefNum().mIndex;
                if (ptr.getClass().isActor() && Main::isInitialised())
                {
                    const uint32_t canonicalRefNum
                        = Main::get().getActorSync().getActorCanonicalRefNum(ptr);
                    if (canonicalRefNum != 0)
                        candidateRefNum = canonicalRefNum;
                }
                if (candidateRefNum != record.refNum)
                    return true;
            }
            target = ptr;
            return false;
        });
        if (!target.isEmpty())
            return target;
    }

    if (record.refNum != 0 && Main::isInitialised())
        target = Main::get().getActorSync().getActorByCanonicalRefNum(record.refNum);
    return target;
}

void WorldObjectSync::processPendingHarvest(const ContainerRecord& record)
{
    auto pending = std::find_if(mPendingHarvests.begin(), mPendingHarvests.end(), [&](const PendingHarvest& value) {
        return value.source.cellId == record.cellId && value.source.refId == record.refId
            && value.source.refNum == record.refNum && value.source.mpNum == record.mpNum;
    });
    if (pending == mPendingHarvests.end())
        return;

    MWWorld::Ptr target = findContainerTarget(record);
    if (target.isEmpty() || target.getType() != ESM::Container::sRecordId)
        return;

    struct HarvestCandidate
    {
        MWWorld::Ptr item;
        int count = 0;
        std::string name;
    };

    auto& store = target.getClass().getContainerStore(target);
    std::vector<HarvestCandidate> candidates;
    for (auto it = store.begin(); it != store.end(); ++it)
    {
        if (!it->getClass().showsInInventory(*it) || it->getCellRef().getCount() <= 0)
            continue;
        candidates.push_back({ *it, it->getCellRef().getCount(), std::string(it->getClass().getName(*it)) });
    }

    auto feedback = std::make_shared<HarvestFeedback>();
    feedback->remaining = candidates.size();
    std::size_t queued = 0;
    for (const HarvestCandidate& candidate : candidates)
    {
        const bool requested = requestInventoryTake(target, candidate.item, candidate.count,
            InventoryTakeKind::Container,
            [feedback, itemName = candidate.name](const InventoryTakeResult& result) {
                if (result.accepted && result.itemCount > 0)
                    feedback->taken[itemName] += result.itemCount;
                if (feedback->remaining > 0)
                    --feedback->remaining;
                if (feedback->remaining == 0)
                    showHarvestFeedback(feedback->taken);
            });
        if (requested)
            ++queued;
        else if (feedback->remaining > 0)
            --feedback->remaining;
    }

    Log(Debug::Info) << "[MP] WorldObjectSync: authoritative harvest snapshot refId=" << record.refId
                     << " items=" << record.items.size() << " requests=" << queued;
    mPendingHarvests.erase(pending);
}

// ---------------------------------------------------------------------------
bool WorldObjectSync::tryApplyContainer(const ContainerRecord& record, ContainerAction action)
{
    MWBase::World* world = MWBase::Environment::get().getWorld();
    if (!world) return false;

    MWWorld::Ptr target = findContainerTarget(record);
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
    bool reconciledSetHandles = false;
    if (action == ContainerAction::Set)
    {
        preservedSetHandles = containerStoreMatchesRecord(cstore, record.items);
        if (!preservedSetHandles)
        {
            // Reconcile authoritative Set snapshots in place first. An open
            // pickpocket/container UI holds Ptrs into this store, so clearing and
            // rebuilding the whole store invalidates every unaffected stack even
            // when the server changed only one count. Preserve those handles and
            // fall back to a full rebuild only if the coarse authoritative record
            // cannot be reproduced safely in place.
            reconciledSetHandles = reconcileContainerStoreInPlace(cstore, record.items, esmStore);
            if (!reconciledSetHandles)
            {
                if (record.items.empty())
                    clearDeadActorEquipmentVisuals(*world, target);
                cstore.clearResolved();
                for (const auto& ci : record.items)
                {
                    MWWorld::ManualRef ref(esmStore, ESM::RefId::stringRefId(ci.refId), ci.count);
                    if (!ref.getPtr().isEmpty())
                    {
                        MWWorld::Ptr ptr = ref.getPtr();
                        ptr.getCellRef().setCharge(ci.charge);
                        cstore.add(ptr, ci.count, true, false);
                    }
                }
            }
        }
        if (record.items.empty())
            clearDeadActorEquipmentVisuals(*world, target);

        if (target.getType() == ESM::Container::sRecordId && !cstore.hasVisibleItems())
        {
            if (MWRender::Animation* animation = world->getAnimation(target))
                animation->harvest(target);
        }
    }
    else if (action == ContainerAction::Add)
    {
        for (const auto& ci : record.items)
        {
            MWWorld::ManualRef ref(esmStore, ESM::RefId::stringRefId(ci.refId), ci.count);
            if (!ref.getPtr().isEmpty())
            {
                MWWorld::Ptr ptr = ref.getPtr();
                ptr.getCellRef().setCharge(ci.charge);
                cstore.add(ptr, ci.count);
            }
        }
    }
    else if (action == ContainerAction::Remove)
    {
        for (const auto& ci : record.items)
        {
            int remaining = ci.count;
            std::vector<MWWorld::Ptr> matches;
            for (auto it = cstore.begin(); it != cstore.end(); ++it)
            {
                if (lowerAscii(it->getCellRef().getRefId().serializeText()) == lowerAscii(ci.refId)
                    && static_cast<int>(it->getCellRef().getCharge()) == ci.charge)
                    matches.push_back(*it);
            }
            for (const MWWorld::Ptr& match : matches)
            {
                if (remaining <= 0)
                    break;
                const int removeCount = std::min(remaining, match.getCellRef().getCount());
                cstore.remove(match, removeCount, false, false);
                remaining -= removeCount;
            }
        }
        if (containerStoreEmpty(cstore))
            clearDeadActorEquipmentVisuals(*world, target);
    }

    Log(Debug::Info) << "[MP] WorldObjectSync: applied Container action="
                     << static_cast<int>(action)
                     << " refId=" << record.refId
                     << " mpNum=" << record.mpNum
                     << " preservedSetHandles=" << preservedSetHandles
                     << " reconciledSetHandles=" << reconciledSetHandles;
    mContainerRevisions[makeContainerRevisionKey(
        record.cellId, record.refId, record.refNum, record.mpNum)] = mNextContainerRevision++;
    return true;
}

} // namespace mwmp
