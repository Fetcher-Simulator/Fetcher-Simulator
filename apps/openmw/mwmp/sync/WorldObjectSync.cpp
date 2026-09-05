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
#include <tuple>

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
#include <components/openmw-mp/Packets/Object/PacketBarter.hpp>
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
#include "../../mwworld/worldmodel.hpp"
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
    clearInventoryInstanceAliases();
    mContainerIdentitySnapshots.clear();
    mPendingHarvests.clear();
    mPendingInventoryTakes.clear();
    mPendingInventoryTakeBatches.clear();
    mInventoryTakeBatchSources.clear();
    mInventoryTakeBatchItems.clear();
    mInventoryTakeBatchesAwaitingSource.clear();
    mInventoryTakeBatchCallbacks.clear();
    mDeferredInventoryTakeContainerRemoves.clear();
    mInventoryTakeSources.clear();
    mInventoryTakesAwaitingSource.clear();
    mInventoryTakeCallbacks.clear();
    mWorldItemTakeCallbacks.clear();
    mPendingInventoryPuts.clear();
    mInventoryPutDestinations.clear();
    mInventoryPutsAwaitingDestination.clear();
    mInventoryPutCallbacks.clear();
    mPendingBarters.clear();
    mBarterSources.clear();
    mBarterMerchants.clear();
    mBartersAwaitingSource.clear();
    mBarterMissingSources.clear();
    mBarterCallbacks.clear();
    mBarterRetryStates.clear();
    mOpenContainerTargets.clear();
    mContainerRevisions.clear();
    mPendingContainerBootstrapSets.clear();
    mContainerResetCellsPending.clear();
    mCellResetGenerationFloors.clear();
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

    std::string makeCellId(const MWWorld::CellStore& store)
    {
        const MWWorld::Cell* cell = store.getCell();
        if (!cell)
            return {};
        if (cell->isExterior())
            return "EXT:" + std::to_string(cell->getGridX()) + "," + std::to_string(cell->getGridY());
        return std::string(cell->getNameId());
    }

    std::uint32_t currentCellAuthorityGeneration(const std::string& cellId)
    {
        return Main::isInitialised() ? Main::get().getActorSync().authorityGenerationForCell(cellId) : 0;
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
                if (current.instanceId != 0 || item.instanceId != 0)
                    return current.instanceId != 0 && current.instanceId == item.instanceId;
                return current.refId == item.refId && current.charge == item.charge
                    && std::abs(current.enchantmentCharge - item.enchantmentCharge) < 0.001f
                    && current.soul == item.soul && current.restocking == item.restocking
                    && current.instanceId == item.instanceId;
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
                return current.refId == item.refId && current.charge == item.charge
                    && std::abs(current.enchantmentCharge - item.enchantmentCharge) < 0.001f
                    && current.soul == item.soul && current.restocking == item.restocking
                    && current.instanceId == item.instanceId;
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
            const int rawCount = it->getCellRef().getCount(false);
            item.count = std::abs(rawCount);
            item.restocking = rawCount < 0;
            item.charge = static_cast<int>(it->getCellRef().getCharge());
            item.instanceId = inventoryInstanceId(it->getCellRef().getRefNum());
            item.enchantmentCharge = it->getCellRef().getEnchantmentCharge();
            item.soul = it->getCellRef().getSoul().serializeText();
            appendOrMergeComparable(currentItems, std::move(item));
        }

        for (const ContainerItem& item : expected)
            appendOrMergeComparable(expectedItems, item);

        auto less = [](const ContainerItem& left, const ContainerItem& right)
        {
            if (left.refId != right.refId)
                return left.refId < right.refId;
            return std::tie(left.charge, left.enchantmentCharge, left.soul, left.restocking, left.instanceId)
                < std::tie(right.charge, right.enchantmentCharge, right.soul, right.restocking, right.instanceId);
        };
        std::sort(currentItems.begin(), currentItems.end(), less);
        std::sort(expectedItems.begin(), expectedItems.end(), less);

        if (currentItems.size() != expectedItems.size())
            return false;

        for (std::size_t i = 0; i < currentItems.size(); ++i)
        {
            if (currentItems[i].refId != expectedItems[i].refId
                || currentItems[i].charge != expectedItems[i].charge
                || std::abs(currentItems[i].enchantmentCharge - expectedItems[i].enchantmentCharge) >= 0.001f
                || currentItems[i].soul != expectedItems[i].soul
                || currentItems[i].restocking != expectedItems[i].restocking
                || currentItems[i].instanceId != expectedItems[i].instanceId
                || currentItems[i].count != expectedItems[i].count)
            {
                return false;
            }
        }

        return true;
    }

    bool reconcileContainerStoreInPlaceImpl(MWWorld::ContainerStore& store,
        const std::vector<ContainerItem>& expected, const MWWorld::ESMStore& esmStore)
    {
        struct DesiredItem
        {
            std::string refId;
            int charge = -1;
            float enchantmentCharge = -1.f;
            std::string soul;
            bool restocking = false;
            std::uint32_t instanceId = 0;
            MWWorld::Ptr existingStack;
            int remaining = 0;
        };

        using Identity = std::tuple<std::string, int, float, std::string, bool, std::uint32_t>;
        std::map<Identity, DesiredItem> desired;
        for (const ContainerItem& item : expected)
        {
            if (item.refId.empty() || item.count <= 0)
                continue;
            const Identity identity { lowerAscii(item.refId), item.instanceId ? 0 : item.charge,
                item.instanceId ? 0.f : item.enchantmentCharge,
                item.instanceId ? std::string() : item.soul, item.restocking, item.instanceId };
            auto& entry = desired[identity];
            if (entry.refId.empty())
            {
                entry.refId = item.refId;
                entry.charge = item.charge;
                entry.enchantmentCharge = item.enchantmentCharge;
                entry.soul = item.soul;
                entry.restocking = item.restocking;
                entry.instanceId = item.instanceId;
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
            const int rawCount = ptr.getCellRef().getCount(false);
            if (rawCount == 0)
                continue;
            const int currentCount = std::abs(rawCount);

            const auto id = inventoryInstanceId(ptr.getCellRef().getRefNum());
            const Identity identity { lowerAscii(ptr.getCellRef().getRefId().toString()),
                id ? 0 : static_cast<int>(ptr.getCellRef().getCharge()),
                id ? 0.f : ptr.getCellRef().getEnchantmentCharge(),
                id ? std::string() : ptr.getCellRef().getSoul().serializeText(), rawCount < 0, id };
            auto desiredIt = desired.find(identity);
            const int keep = desiredIt == desired.end()
                ? 0 : std::min(currentCount, desiredIt->second.remaining);
            if (desiredIt != desired.end())
            {
                desiredIt->second.remaining -= keep;
                if (keep > 0 && desiredIt->second.existingStack.isEmpty())
                    desiredIt->second.existingStack = ptr;
                if (keep > 0)
                {
                    const auto& state = desiredIt->second;
                    ptr.getCellRef().setCharge(state.charge);
                    ptr.getCellRef().setEnchantmentCharge(state.enchantmentCharge);
                    ptr.getCellRef().setSoul(ESM::RefId::deserializeText(state.soul));
                }
            }

            const int removeCount = currentCount - keep;
            if (removeCount > 0 && rawCount > 0)
                store.remove(ptr, removeCount, false, false);
        }

        for (auto& [identity, item] : desired)
        {
            (void)identity;
            if (item.remaining <= 0)
                continue;

            if (!item.existingStack.isEmpty())
            {
                const int currentCount = item.existingStack.getCellRef().getCount();
                item.existingStack.getCellRef().setCount(
                    item.restocking ? -(currentCount + item.remaining) : currentCount + item.remaining);
                continue;
            }

            const int signedCount = item.restocking ? -item.remaining : item.remaining;
            MWWorld::ManualRef ref(esmStore, ESM::RefId::stringRefId(item.refId), signedCount);
            MWWorld::Ptr ptr = ref.getPtr();
            if (ptr.isEmpty())
                return false;
            ptr.getCellRef().setCharge(item.charge);
            ptr.getCellRef().setRefNum(inventoryInstanceRefNum(item.instanceId));
            ptr.getCellRef().setEnchantmentCharge(item.enchantmentCharge);
            ptr.getCellRef().setSoul(item.soul.empty() ? ESM::RefId() : ESM::RefId::deserializeText(item.soul));
            const auto added = store.add(ptr, signedCount, false, false, true);
            setInventoryInstanceAlias(added->getCellRef().getRefNum(), item.instanceId);
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

bool WorldObjectSync::bindContainerSnapshotIdentities(const std::vector<ContainerItem>& snapshot,
    const std::vector<MWWorld::Ptr>& handles, const std::vector<ContainerItem>& accepted)
{
    if (snapshot.size() != accepted.size() || handles.size() != accepted.size())
        return false;
    for (std::size_t i = 0; i < accepted.size(); ++i)
    {
        if (accepted[i].instanceId == 0 || handles[i].isEmpty()
            || !handles[i].getCellRef().getRefNum().isSet()
            || snapshot[i].count != accepted[i].count
            || !sameAuthoritativeContainerIdentity(snapshot[i], accepted[i]))
            return false;
    }
    for (std::size_t i = 0; i < accepted.size(); ++i)
    {
        // Preserve the WorldModel/Lua object ID while binding the server row ID.
        // Match the captured snapshot, never the now-recharging live metadata.
        setInventoryInstanceAlias(handles[i].getCellRef().getRefNum(), accepted[i].instanceId);
        Log(Debug::Info) << "[MP] Container bootstrap bound item ptr=" << handles[i].getBase()
            << " refNum=" << handles[i].getCellRef().getRefNum().mIndex
            << " contentFile=" << handles[i].getCellRef().getRefNum().mContentFile
            << " instanceId=" << inventoryInstanceId(handles[i].getCellRef().getRefNum())
            << " refId=" << accepted[i].refId << " enchant=" << handles[i].getCellRef().getEnchantmentCharge()
            << " authoritativeEnchant=" << accepted[i].enchantmentCharge;
    }
    return true;
}

bool WorldObjectSync::reconcileContainerStoreInPlace(MWWorld::ContainerStore& store,
    const std::vector<ContainerItem>& expected, const MWWorld::ESMStore& esmStore)
{
    return reconcileContainerStoreInPlaceImpl(store, expected, esmStore);
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

void WorldObjectSync::prepareCellForInsertion(MWWorld::CellStore& cell)
{
    const std::string cellId = makeCellId(cell);
    if (cellId.empty() || !mContainerResetCellsPending.contains(cellId))
        return;
    applyContainerCellReset(cell, cellId);
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

    // --- pending authoritative world-item counts ---
    mPendingCount.erase(
        std::remove_if(mPendingCount.begin(), mPendingCount.end(),
            [&](PendingCount& p) -> bool {
                p.timer += dt;
                if (p.timer < RETRY_RATE) return false;
                p.timer = 0.f;
                return tryApplyObjectCount(p.identity, p.count);
            }),
        mPendingCount.end());

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
    pkt.authorityGeneration = currentCellAuthorityGeneration(cellId);
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

bool WorldObjectSync::requestLocalObjectTake(
    const MWWorld::Ptr& worldObject, WorldItemTakeCallback callback)
{
    return requestLocalObjectTake(worldObject, InventoryTransferSoundDirection::Up, std::move(callback));
}

bool WorldObjectSync::requestLocalObjectTake(const MWWorld::Ptr& worldObject,
    InventoryTransferSoundDirection soundDirection, WorldItemTakeCallback callback)
{
    if (worldObject.isEmpty() || !worldObject.isInCell())
        return false;

    WorldItemTakeRequest request;
    request.requestId = mTakeRequestPrefix + "-world-" + std::to_string(mNextTakeRequestId++);
    request.object.cellId = cellIdForPtr(worldObject);
    request.object.refId = worldObject.getCellRef().getRefId().serializeText();
    request.requestedCount = worldObject.getCellRef().getCount();
    request.expectedInventoryRevision
        = Main::get().getPlayerSync().localPlayer().inventoryChanges.revision;
    request.soundDirection = soundDirection;
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
        return false;
    }

    if (callback)
        mWorldItemTakeCallbacks.emplace(request.requestId, std::move(callback));

    PacketWorldItemTakeRequest packet;
    packet.request = request;
    mClient.sendReliable(packet.encode());
    Log(Debug::Verbose) << "[MP] WorldObjectSync: requested authoritative take"
                        << " request=" << request.requestId
                        << " refId=" << request.object.refId
                        << " cell=" << request.object.cellId;
    return true;
}

void WorldObjectSync::markLocalPlayerInventoryDetached(const MWWorld::Ptr& ptr)
{
    if (ptr.isEmpty())
        return;
    mLocalPlayerInventoryDetached.insert(ptr.getCellRef().getRefNum());
}

bool WorldObjectSync::isLocalPlayerInventoryDetached(const MWWorld::Ptr& ptr) const
{
    return !ptr.isEmpty()
        && mLocalPlayerInventoryDetached.contains(ptr.getCellRef().getRefNum());
}

bool WorldObjectSync::requiresAuthoritativeWorldItemTake(bool sourceOwnerEmpty, bool itemInCell,
    bool destinationIsLocalPlayer, bool barterOpen, bool detachedFromLocalPlayer)
{
    return sourceOwnerEmpty && itemInCell && destinationIsLocalPlayer && !barterOpen
        && !detachedFromLocalPlayer;
}

bool WorldObjectSync::requiresContainerBootstrapOnOpen(bool isActorContainer,
    bool hasActorAuthority, bool hasCellAuthority, bool hasAuthoritativeRevision)
{
    return isActorContainer ? !hasActorAuthority : !hasCellAuthority && !hasAuthoritativeRevision;
}

bool WorldObjectSync::shouldDeferContainerResolutionOnOpen(bool connected, bool isActorContainer)
{
    return connected && !isActorContainer;
}

void WorldObjectSync::resolveContainerForAuthoritativeSnapshot(MWWorld::ContainerStore& store)
{
    store.resolve();
}

bool WorldObjectSync::shouldDeferContainerResolutionOnOpen(const MWWorld::Ptr& container) const
{
    if (container.isEmpty() || container.getCell() == nullptr)
        return false;

    return shouldDeferContainerResolutionOnOpen(
        Main::isConnected(), container.getClass().isActor());
}

bool WorldObjectSync::requiresProjectileStoredActorBootstrap(
    bool hasAuthoritativeRevision, bool bootstrapAlreadyQueued)
{
    return !hasAuthoritativeRevision && !bootstrapAlreadyQueued;
}

bool WorldObjectSync::inventoryTakeSourceMatchesContainer(
    const InventorySourceIdentity& source, const ContainerRecord& record)
{
    if (source.cellId != record.cellId || source.refId != record.refId)
        return false;
    if (source.actorInstanceId == 0)
        return source.refNum == record.refNum && source.mpNum == record.mpNum;
    return (source.mpNum == 0 || source.mpNum == record.mpNum)
        && (source.refNum == 0 || source.refNum == record.refNum);
}

bool WorldObjectSync::shouldDeferInventoryTakeContainerRemove(
    std::size_t pendingSameSource, bool hasDeferredBatch)
{
    return pendingSameSource > 1 || hasDeferredBatch;
}

bool WorldObjectSync::shouldAcceptContainerAuthorityGeneration(
    std::uint32_t currentGeneration, std::uint32_t incomingGeneration, bool reset)
{
    if (incomingGeneration == 0)
        return false;
    return reset ? incomingGeneration >= currentGeneration : incomingGeneration == currentGeneration;
}

bool WorldObjectSync::shouldAcceptObjectPlaceAuthorityGeneration(
    std::uint32_t currentGeneration, std::uint32_t resetGenerationFloor, std::uint32_t incomingGeneration)
{
    (void)currentGeneration;
    if (incomingGeneration == 0)
        return false;
    return resetGenerationFloor == 0 || incomingGeneration >= resetGenerationFloor;
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
    if (container.getClass().isActor() && Main::isInitialised())
    {
        const ActorSync& actorSync = Main::get().getActorSync();
        record.mpNum = actorSync.getActorMpNum(container);
        record.refNum = record.mpNum == 0 ? actorSync.getActorCanonicalRefNum(container) : 0;
    }
    else
    {
        record.refNum = container.getCellRef().getRefNum().mIndex;
        record.mpNum = getMpNumForObject(container);
    }
    const std::string key = makeContainerRevisionKey(
        record.cellId, record.refId, record.refNum, record.mpNum);
    if (Main::isConnected())
    {
        const ActorSync& actorSync = Main::get().getActorSync();
        const bool isActorContainer = container.getClass().isActor();
        const bool hasActorAuthority = isActorContainer && actorSync.hasAuthorityForObject(container);
        const bool hasCellAuthority = actorSync.hasAuthority(record.cellId);
        const bool hasAuthoritativeRevision = mContainerRevisions.contains(key);
        if (requiresContainerBootstrapOnOpen(
                isActorContainer, hasActorAuthority, hasCellAuthority, hasAuthoritativeRevision))
        {
            requestContainerBootstrap(container);
            return;
        }
        if (!isActorContainer && !hasCellAuthority)
        {
            // The local store already contains a Set accepted from the server.
            // Keep the exact Ptr bound for later deltas without re-requesting or
            // publishing a non-authority snapshot.
            mOpenContainerTargets[key] = container;
            return;
        }
    }

    mOpenContainerTargets[key] = container;
    sendLocalContainerSnapshot(record, container);
}

bool WorldObjectSync::requestContainerBootstrap(const MWWorld::Ptr& container)
{
    if (container.isEmpty() || container.getCell() == nullptr || !isContainerTarget(container))
        return false;

    ContainerRecord record;
    record.cellId = makeCellId(container);
    record.refId = container.getCellRef().getRefId().serializeText();
    if (container.getClass().isActor() && Main::isInitialised())
    {
        const ActorSync& actorSync = Main::get().getActorSync();
        record.mpNum = actorSync.getActorMpNum(container);
        record.refNum = record.mpNum == 0 ? actorSync.getActorCanonicalRefNum(container) : 0;
    }
    else
    {
        record.refNum = container.getCellRef().getRefNum().mIndex;
        record.mpNum = getMpNumForObject(container);
    }

    if (record.cellId.empty() || record.refId.empty())
        return false;

    const std::string key = makeContainerRevisionKey(
        record.cellId, record.refId, record.refNum, record.mpNum);
    mOpenContainerTargets[key] = container;

    PacketContainer packet;
    packet.container = record;
    packet.authorityGeneration = currentCellAuthorityGeneration(record.cellId);
    packet.mAction = static_cast<uint8_t>(ContainerAction::BootstrapRequest);
    mClient.sendReliable(packet.encode());
    Log(Debug::Verbose) << "[MP] WorldObjectSync: requested authoritative Container bootstrap refId="
                        << record.refId << " refNum=" << record.refNum << " mpNum=" << record.mpNum;
    return true;
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
    pkt.authorityGeneration = currentCellAuthorityGeneration(cellId);
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
    pkt.authorityGeneration = currentCellAuthorityGeneration(record.cellId);
    pkt.mAction = static_cast<uint8_t>(ContainerAction::Set);
    pkt.bootstrapSequence = mNextContainerIdentitySnapshot++;
    ContainerIdentitySnapshot identitySnapshot;
    identitySnapshot.authorityGeneration = pkt.authorityGeneration;

    auto& cstore = target.getClass().getContainerStore(target);
    // Authority bootstrap must snapshot concrete container contents. Organic/leveled
    // containers may still be unresolved here; iterating an unresolved store can
    // serialize an empty Set and incorrectly make the server authoritative for an
    // empty container before the activating client can request the harvest.
    resolveContainerForAuthoritativeSnapshot(cstore);
    for (auto it = cstore.begin(); it != cstore.end(); ++it)
    {
        MWBase::Environment::get().getWorldModel()->registerPtr(*it);
        ContainerItem ci;
        ci.refId = it->getCellRef().getRefId().toString();
        const int rawCount = it->getCellRef().getCount(false);
        ci.count = std::abs(rawCount);
        ci.restocking = rawCount < 0;
        ci.charge = static_cast<int>(it->getCellRef().getCharge());
        ci.instanceId = inventoryInstanceId(it->getCellRef().getRefNum());
        ci.enchantmentCharge = it->getCellRef().getEnchantmentCharge();
        ci.soul = it->getCellRef().getSoul().serializeText();
        pkt.container.items.push_back(ci);
        identitySnapshot.handles.push_back(*it);
        Log(Debug::Info) << "[MP] Container(Set) snapshot identity source=" << record.refId
            << " mpNum=" << record.mpNum << " sequence=" << pkt.bootstrapSequence
            << " ptr=" << it->getBase() << " refNum=" << it->getCellRef().getRefNum().mIndex
            << " contentFile=" << it->getCellRef().getRefNum().mContentFile
            << " instanceId=" << ci.instanceId << " refId=" << ci.refId
            << " count=" << ci.count << " charge=" << ci.charge << " enchant=" << ci.enchantmentCharge
            << " soul=" << ci.soul << " restocking=" << ci.restocking;
    }
    identitySnapshot.record = pkt.container;
    mContainerIdentitySnapshots.emplace(pkt.bootstrapSequence, std::move(identitySnapshot));
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
    pkt.authorityGeneration = currentCellAuthorityGeneration(cellId);
    pkt.mAction = static_cast<uint8_t>(action);
    mClient.sendReliable(pkt.encode());
    Log(Debug::Verbose) << "[MP] WorldObjectSync: sent Container(" << static_cast<int>(action)
                        << ") refId=" << refId
                        << " refNum=" << refNum
                        << " items=" << items.size();
}

void WorldObjectSync::onLocalProjectileStoredInActor(
    const MWWorld::Ptr& actor, const MWWorld::Ptr& projectile)
{
    if (!Main::isInitialised() || !Main::isConnected()
        || actor.isEmpty() || projectile.isEmpty()
        || actor.getCell() == nullptr || !actor.getClass().isActor())
        return;

    const ActorSync& actorSync = Main::get().getActorSync();
    if (!actorSync.hasAuthorityForObject(actor))
    {
        Log(Debug::Verbose) << "[MP] WorldObjectSync: skipped projectile container sync for non-authority actor refId="
                            << actor.getCellRef().getRefId().serializeText();
        return;
    }

    ContainerRecord record;
    record.cellId = makeCellId(actor);
    record.refId = actor.getCellRef().getRefId().serializeText();
    record.mpNum = actorSync.getActorMpNum(actor);
    record.refNum = record.mpNum == 0 ? actorSync.getActorCanonicalRefNum(actor) : 0;
    if (record.cellId.empty() || record.refId.empty())
        return;

    ContainerItem delta;
    delta.refId = projectile.getCellRef().getRefId().serializeText();
    delta.count = 1;
    delta.charge = static_cast<int>(projectile.getCellRef().getCharge());
    delta.instanceId = inventoryInstanceId(projectile.getCellRef().getRefNum());
    delta.enchantmentCharge = projectile.getCellRef().getEnchantmentCharge();
    delta.soul = projectile.getCellRef().getSoul().serializeText();

    const std::string key = makeContainerRevisionKey(
        record.cellId, record.refId, record.refNum, record.mpNum);
    const bool hasAuthoritativeRevision = mContainerRevisions.find(key) != mContainerRevisions.end();
    const bool bootstrapAlreadyQueued = mPendingContainerBootstrapSets.contains(key);
    if (requiresProjectileStoredActorBootstrap(hasAuthoritativeRevision, bootstrapAlreadyQueued))
    {
        // The projectile has already been inserted into the local actor store by
        // vanilla combat. Establish server authority from that concrete post-hit
        // snapshot before sending any later deltas. Reliable ordering guarantees
        // subsequent Add packets follow this Set.
        mPendingContainerBootstrapSets.insert(key);
        mOpenContainerTargets[key] = actor;
        sendLocalContainerSnapshot(record, actor);
        Log(Debug::Info) << "[MP] WorldObjectSync: bootstrapped projectile-stored actor inventory"
                         << " refId=" << record.refId
                         << " refNum=" << record.refNum
                         << " mpNum=" << record.mpNum
                         << " projectile=" << delta.refId;
        return;
    }

    onLocalContainerChanged(record.cellId, record.refId, record.refNum, record.mpNum,
        ContainerAction::Add, { delta });
    Log(Debug::Info) << "[MP] WorldObjectSync: synced projectile-stored actor inventory delta"
                     << " refId=" << record.refId
                     << " refNum=" << record.refNum
                     << " mpNum=" << record.mpNum
                     << " projectile=" << delta.refId;
}

// ---------------------------------------------------------------------------
// Inbound — server tells us to place an object
// ---------------------------------------------------------------------------
void WorldObjectSync::onServerObjectPlace(uint32_t mpNum, const std::string& refId,
                                           int count, const Position& pos,
                                           const std::string& cellId, std::uint32_t authorityGeneration)
{
    const std::uint32_t currentGeneration = currentCellAuthorityGeneration(cellId);
    const auto floorIt = mCellResetGenerationFloors.find(cellId);
    const std::uint32_t resetFloor = floorIt == mCellResetGenerationFloors.end() ? 0 : floorIt->second;
    if (!shouldAcceptObjectPlaceAuthorityGeneration(currentGeneration, resetFloor, authorityGeneration))
    {
        Log(Debug::Warning) << "[MP] WorldObjectSync: dropped stale ObjectPlace"
                            << " cell=" << cellId
                            << " mpNum=" << mpNum
                            << " incomingGeneration=" << authorityGeneration
                            << " currentGeneration=" << currentGeneration
                            << " resetFloor=" << resetFloor;
        return;
    }
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

void WorldObjectSync::onServerObjectCount(const PlacedObjectIdentity& identity, std::int32_t count)
{
    if (count <= 0)
    {
        onServerObjectDelete(identity);
        return;
    }
    if (!tryApplyObjectCount(identity, count))
    {
        Log(Debug::Verbose) << "[MP] WorldObjectSync: queuing ObjectCount"
                            << " refId=" << identity.refId << " count=" << count;
        mPendingCount.push_back({identity, count, 0.f});
    }
}

bool WorldObjectSync::requestInventoryTakeBatch(const MWWorld::Ptr& source,
    const std::vector<InventoryTakeBatchInput>& items, InventoryTakeKind kind,
    InventoryTransferSoundDirection soundDirection, InventoryTakeBatchCallback callback)
{
    if (!Main::isInitialised() || source.isEmpty() || items.empty()
        || items.size() > MaximumInventoryTakeBatchLines || isRemotePlayerInventorySource(source))
        return false;

    InventoryTakeBatchRequest request;
    request.requestId = mTakeRequestPrefix + "-inventory-batch-"
        + std::to_string(mNextInventoryTakeRequestId++);
    request.kind = kind;
    request.source.cellId = makeCellId(source);
    request.source.authorityGeneration = currentCellAuthorityGeneration(request.source.cellId);
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
    request.expectedInventoryRevision
        = Main::get().getPlayerSync().localPlayer().inventoryChanges.revision;
    request.soundDirection = soundDirection;

    std::vector<MWWorld::Ptr> handles;
    handles.reserve(items.size());
    request.items.reserve(items.size());
    for (const InventoryTakeBatchInput& input : items)
    {
        if (input.item.isEmpty() || input.count <= 0)
            return false;
        InventoryTakeBatchLine line;
        line.itemRefId = input.item.getCellRef().getRefId().serializeText();
        line.itemInstanceId = inventoryInstanceId(input.item.getCellRef().getRefNum());
        line.itemCharge = static_cast<std::int32_t>(input.item.getCellRef().getCharge());
        line.itemEnchantmentCharge = input.item.getCellRef().getEnchantmentCharge();
        line.itemSoul = input.item.getCellRef().getSoul().serializeText();
        line.requestedCount = input.count;
        Log(Debug::Info) << "[MP] InventoryTakeBatch request identity request=" << request.requestId
                         << " ptr=" << input.item.getBase()
                         << " refNum=" << input.item.getCellRef().getRefNum().mIndex
                         << " contentFile=" << input.item.getCellRef().getRefNum().mContentFile
                         << " itemInstanceId=" << line.itemInstanceId
                         << " refId=" << line.itemRefId << " count=" << line.requestedCount
                         << " charge=" << line.itemCharge << " enchant=" << line.itemEnchantmentCharge
                         << " soul=" << line.itemSoul << " source=" << request.source.refId
                         << " sourceMpNum=" << request.source.mpNum
                         << " actorInstanceId=" << request.source.actorInstanceId;
        request.items.push_back(std::move(line));
        handles.push_back(input.item);
    }
    if (validateInventoryTakeBatchRequest(request) != InventoryTakeError::None)
    {
        Log(Debug::Warning) << "[MP] Cannot build canonical inventory take batch source="
                            << request.source.refId << " lines=" << request.items.size();
        return false;
    }

    if (callback)
        mInventoryTakeBatchCallbacks.emplace(request.requestId, std::move(callback));
    mInventoryTakeBatchSources[request.requestId] = source;
    mInventoryTakeBatchItems[request.requestId] = std::move(handles);
    mPendingInventoryTakeBatches.push_back(request);
    sendInventoryTakeBatchRequest(request);
    return true;
}

bool WorldObjectSync::requestInventoryTake(const MWWorld::Ptr& source, const MWWorld::Ptr& item,
    int count, InventoryTakeKind kind, InventoryTakeCallback callback)
{
    return requestInventoryTake(source, item, count, kind,
        InventoryTransferSoundDirection::Up, std::move(callback));
}

bool WorldObjectSync::requestInventoryTake(const MWWorld::Ptr& source, const MWWorld::Ptr& item,
    int count, InventoryTakeKind kind, InventoryTransferSoundDirection soundDirection,
    InventoryTakeCallback callback)
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
    request.source.authorityGeneration = currentCellAuthorityGeneration(request.source.cellId);
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
    request.itemInstanceId = inventoryInstanceId(item.getCellRef().getRefNum());
    request.itemCharge = static_cast<std::int32_t>(item.getCellRef().getCharge());
    request.itemEnchantmentCharge = item.getCellRef().getEnchantmentCharge();
    request.itemSoul = item.getCellRef().getSoul().serializeText();
    request.requestedCount = count;
    request.expectedInventoryRevision = Main::get().getPlayerSync().localPlayer().inventoryChanges.revision;
    request.soundDirection = soundDirection;
    Log(Debug::Info) << "[MP] InventoryTake request identity request=" << request.requestId
                     << " ptr=" << item.getBase()
                     << " refNum=" << item.getCellRef().getRefNum().mIndex
                     << " contentFile=" << item.getCellRef().getRefNum().mContentFile
                     << " itemInstanceId=" << request.itemInstanceId
                     << " refId=" << request.itemRefId << " count=" << count
                     << " charge=" << request.itemCharge << " enchant=" << request.itemEnchantmentCharge
                     << " soul=" << request.itemSoul << " source=" << request.source.refId
                     << " sourceMpNum=" << request.source.mpNum
                     << " actorInstanceId=" << request.source.actorInstanceId;
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

bool WorldObjectSync::requestBarterTake(const MWWorld::Ptr& merchant, const MWWorld::Ptr& source,
    const MWWorld::Ptr& item, int count, int barterPrice, InventoryTakeCallback callback)
{
    if (!Main::isInitialised() || merchant.isEmpty() || source.isEmpty() || item.isEmpty() || count <= 0
        || barterPrice <= 0 || !merchant.getClass().isActor() || isRemotePlayerInventorySource(source))
        return false;

    InventoryTakeRequest request;
    request.requestId = mTakeRequestPrefix + "-barter-"
        + std::to_string(mNextInventoryTakeRequestId++);
    request.kind = InventoryTakeKind::Barter;
    request.source.cellId = makeCellId(source);
    request.source.authorityGeneration = currentCellAuthorityGeneration(request.source.cellId);
    request.source.refId = source.getCellRef().getRefId().serializeText();
    request.source.refNum = source.getCellRef().getRefNum().mIndex;
    request.source.mpNum = getMpNumForObject(source);

    ActorSync& actorSync = Main::get().getActorSync();
    if (source.getClass().isActor())
    {
        request.source.actorInstanceId = actorSync.actorNetIdForPtr(request.source.cellId, source);
        request.source.migrationGeneration
            = actorSync.actorMigrationGenerationForPtr(request.source.cellId, source);
        request.source.mpNum = actorSync.getActorMpNum(source);
        request.source.refNum = request.source.mpNum == 0
            ? actorSync.getActorCanonicalRefNum(source) : 0;
    }

    request.merchant.cellId = makeCellId(merchant);
    request.merchant.authorityGeneration = currentCellAuthorityGeneration(request.merchant.cellId);
    request.merchant.refId = merchant.getCellRef().getRefId().serializeText();
    request.merchant.actorInstanceId = actorSync.actorNetIdForPtr(request.merchant.cellId, merchant);
    request.merchant.migrationGeneration
        = actorSync.actorMigrationGenerationForPtr(request.merchant.cellId, merchant);
    request.merchant.mpNum = actorSync.getActorMpNum(merchant);
    request.merchant.refNum = request.merchant.mpNum == 0
        ? actorSync.getActorCanonicalRefNum(merchant) : 0;

    request.itemRefId = item.getCellRef().getRefId().serializeText();
    request.itemInstanceId = inventoryInstanceId(item.getCellRef().getRefNum());
    request.itemCharge = static_cast<std::int32_t>(item.getCellRef().getCharge());
    request.itemEnchantmentCharge = item.getCellRef().getEnchantmentCharge();
    request.itemSoul = item.getCellRef().getSoul().serializeText();
    request.requestedCount = count;
    request.barterPrice = barterPrice;
    request.expectedInventoryRevision = Main::get().getPlayerSync().localPlayer().inventoryChanges.revision;
    if (validateInventoryTakeRequest(request) != InventoryTakeError::None)
    {
        Log(Debug::Warning) << "[MP] Cannot build canonical barter take request merchant="
                            << request.merchant.refId << " source=" << request.source.refId
                            << " item=" << request.itemRefId;
        return false;
    }

    if (callback)
        mInventoryTakeCallbacks.emplace(request.requestId, std::move(callback));
    mInventoryTakeSources[request.requestId] = source;
    mPendingInventoryTakes.push_back(request);

    const bool sourceAuthority = source.getClass().isActor()
        ? actorSync.hasAuthorityForObject(source)
        : actorSync.hasAuthority(request.source.cellId);
    if (sourceAuthority)
        onLocalContainerOpened(source);

    sendInventoryTakeRequest(request);
    return true;
}

bool WorldObjectSync::requestBarterTransaction(const MWWorld::Ptr& merchant,
    const std::vector<BarterLineInput>& lines, int balance, int merchantGold, BarterCallback callback)
{
    if (!Main::isInitialised() || merchant.isEmpty() || !merchant.getClass().isActor()
        || lines.empty() || lines.size() > MaximumBarterLines || merchantGold < 0)
        return false;

    BarterRequest request;
    request.requestId = mTakeRequestPrefix + "-bartertx-" + std::to_string(mNextBarterRequestId++);
    request.balance = balance;
    request.merchantGold = merchantGold;
    request.expectedInventoryRevision = Main::get().getPlayerSync().localPlayer().inventoryChanges.revision;

    ActorSync& actorSync = Main::get().getActorSync();
    request.merchant.cellId = makeCellId(merchant);
    request.merchant.authorityGeneration = currentCellAuthorityGeneration(request.merchant.cellId);
    request.merchant.refId = merchant.getCellRef().getRefId().serializeText();
    request.merchant.actorInstanceId = actorSync.actorNetIdForPtr(request.merchant.cellId, merchant);
    request.merchant.migrationGeneration
        = actorSync.actorMigrationGenerationForPtr(request.merchant.cellId, merchant);
    request.merchant.mpNum = actorSync.getActorMpNum(merchant);
    request.merchant.refNum = request.merchant.mpNum == 0
        ? actorSync.getActorCanonicalRefNum(merchant) : 0;

    std::vector<MWWorld::Ptr> sourcePtrs;
    sourcePtrs.reserve(lines.size());
    const MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
    for (const BarterLineInput& input : lines)
    {
        if (input.item.isEmpty() || input.count <= 0)
            return false;
        BarterLine line;
        line.kind = input.kind;
        line.itemRefId = input.item.getCellRef().getRefId().serializeText();
        line.itemInstanceId = inventoryInstanceId(input.item.getCellRef().getRefNum());
        line.itemCharge = static_cast<std::int32_t>(input.item.getCellRef().getCharge());
        line.itemEnchantmentCharge = input.item.getCellRef().getEnchantmentCharge();
        line.itemSoul = input.item.getCellRef().getSoul().serializeText();
        line.count = input.count;

        if (input.kind == BarterLineKind::Sell)
        {
            const MWWorld::Ptr owner = input.item.getContainerStore()
                ? input.item.getContainerStore()->getPtr() : MWWorld::Ptr{};
            if (owner.isEmpty() || owner != player || line.itemInstanceId == 0)
                return false;
            sourcePtrs.emplace_back();
        }
        else if (input.kind == BarterLineKind::BuyWorldItem)
        {
            if (input.source.isEmpty() || input.source != input.item || !input.source.isInCell())
                return false;
            line.itemInstanceId = 0;
            line.worldObject.cellId = makeCellId(input.source);
            line.worldObject.refId = line.itemRefId;
            const std::uint32_t mpNum = getMpNumForObject(input.source);
            if (mpNum != 0)
            {
                line.worldObject.kind = PlacedObjectKind::ServerPlaced;
                line.worldObject.mpNum = mpNum;
            }
            else
            {
                const ESM::RefNum refNum = input.source.getCellRef().getRefNum();
                line.worldObject.kind = PlacedObjectKind::ContentReference;
                line.worldObject.refIndex = refNum.mIndex;
                line.worldObject.refContentFile = refNum.mContentFile;
            }
            sourcePtrs.push_back(input.source);
        }
        else
        {
            if (input.source.isEmpty() || isRemotePlayerInventorySource(input.source))
                return false;
            line.source.cellId = makeCellId(input.source);
            line.source.authorityGeneration = currentCellAuthorityGeneration(line.source.cellId);
            line.source.refId = input.source.getCellRef().getRefId().serializeText();
            line.source.refNum = input.source.getCellRef().getRefNum().mIndex;
            line.source.mpNum = getMpNumForObject(input.source);
            if (input.source.getClass().isActor())
            {
                line.source.actorInstanceId = actorSync.actorNetIdForPtr(line.source.cellId, input.source);
                line.source.migrationGeneration
                    = actorSync.actorMigrationGenerationForPtr(line.source.cellId, input.source);
                line.source.mpNum = actorSync.getActorMpNum(input.source);
                line.source.refNum = line.source.mpNum == 0
                    ? actorSync.getActorCanonicalRefNum(input.source) : 0;
            }
            sourcePtrs.push_back(input.source);
        }
        request.lines.push_back(std::move(line));
    }

    if (validateBarterRequest(request) != BarterError::None)
    {
        Log(Debug::Warning) << "[MP] Cannot build canonical barter transaction merchant="
                            << request.merchant.refId << " lines=" << request.lines.size();
        return false;
    }

    if (callback)
        mBarterCallbacks.emplace(request.requestId, std::move(callback));
    mBarterSources.emplace(request.requestId, std::move(sourcePtrs));
    mBarterMerchants.emplace(request.requestId, merchant);
    mBarterRetryStates.emplace(request.requestId, BarterRetryState{});
    mPendingBarters.push_back(request);

    // Seed every source the client currently has authority for. This makes a
    // multi-source offer converge without relying on reverse lookup races.
    if (actorSync.hasAuthorityForObject(merchant))
        onLocalContainerOpened(merchant);
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        if (lines[i].kind == BarterLineKind::Sell || lines[i].kind == BarterLineKind::BuyWorldItem
            || lines[i].source.isEmpty())
            continue;
        const bool sourceAuthority = lines[i].source.getClass().isActor()
            ? actorSync.hasAuthorityForObject(lines[i].source)
            : actorSync.hasAuthority(makeCellId(lines[i].source));
        if (sourceAuthority)
            onLocalContainerOpened(lines[i].source);
    }

    sendBarterRequest(request);
    return true;
}

bool WorldObjectSync::requestInventoryPut(const MWWorld::Ptr& destination, const MWWorld::Ptr& item,
    int count, InventoryPutCallback callback)
{
    if (!Main::isInitialised() || destination.isEmpty() || item.isEmpty() || count <= 0)
        return false;

    const bool actorDestination = destination.getClass().isActor();
    if (destination.getType() != ESM::Container::sRecordId
        && (!actorDestination || !destination.getClass().getCreatureStats(destination).isDead()))
        return false;

    const MWWorld::Ptr sourceOwner
        = item.getContainerStore() ? item.getContainerStore()->getPtr() : MWWorld::Ptr{};
    const MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
    if (sourceOwner.isEmpty() || sourceOwner != player || destination == player)
        return false;

    InventoryPutRequest request;
    request.requestId = mTakeRequestPrefix + "-put-" + std::to_string(mNextInventoryPutRequestId++);
    request.destination.cellId = makeCellId(destination);
    request.destination.authorityGeneration = currentCellAuthorityGeneration(request.destination.cellId);
    request.destination.refId = destination.getCellRef().getRefId().serializeText();
    request.destination.refNum = destination.getCellRef().getRefNum().mIndex;
    request.destination.mpNum = getMpNumForObject(destination);
    if (actorDestination)
    {
        ActorSync& actorSync = Main::get().getActorSync();
        request.destination.actorInstanceId
            = actorSync.actorNetIdForPtr(request.destination.cellId, destination);
        request.destination.migrationGeneration
            = actorSync.actorMigrationGenerationForPtr(request.destination.cellId, destination);
        request.destination.mpNum = actorSync.getActorMpNum(destination);
        request.destination.refNum = request.destination.mpNum == 0
            ? actorSync.getActorCanonicalRefNum(destination) : 0;
    }
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
    packet.authorityGeneration = currentCellAuthorityGeneration(record.cellId);
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
    request.source.authorityGeneration = currentCellAuthorityGeneration(request.source.cellId);
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

void WorldObjectSync::sendInventoryTakeBatchRequest(const InventoryTakeBatchRequest& request)
{
    PacketInventoryTakeBatchRequest packet;
    packet.request = request;
    mClient.sendReliable(packet.encode());
}

void WorldObjectSync::refreshInventoryTakeBatchItems(InventoryTakeBatchRequest& request) const
{
    const auto handlesIt = mInventoryTakeBatchItems.find(request.requestId);
    if (handlesIt == mInventoryTakeBatchItems.end() || handlesIt->second.size() != request.items.size())
        return;
    for (std::size_t i = 0; i < request.items.size(); ++i)
    {
        const MWWorld::Ptr& item = handlesIt->second[i];
        if (item.isEmpty())
            continue;
        InventoryTakeBatchLine& line = request.items[i];
        line.itemRefId = item.getCellRef().getRefId().serializeText();
        line.itemInstanceId = inventoryInstanceId(item.getCellRef().getRefNum());
        line.itemCharge = static_cast<std::int32_t>(item.getCellRef().getCharge());
        line.itemEnchantmentCharge = item.getCellRef().getEnchantmentCharge();
        line.itemSoul = item.getCellRef().getSoul().serializeText();
    }
}

void WorldObjectSync::flushDeferredInventoryTakeContainerRemove(const InventorySourceIdentity& source)
{
    ContainerRecord sourceRecord;
    sourceRecord.cellId = source.cellId;
    sourceRecord.refId = source.refId;
    sourceRecord.refNum = source.refNum;
    sourceRecord.mpNum = source.mpNum;
    if (std::any_of(mPendingInventoryTakes.begin(), mPendingInventoryTakes.end(),
            [&](const InventoryTakeRequest& request) {
                return inventoryTakeSourceMatchesContainer(request.source, sourceRecord);
            }))
        return;

    auto deferred = std::find_if(mDeferredInventoryTakeContainerRemoves.begin(),
        mDeferredInventoryTakeContainerRemoves.end(), [&](const auto& entry) {
            return inventoryTakeSourceMatchesContainer(source, entry.second.record);
        });
    if (deferred == mDeferredInventoryTakeContainerRemoves.end())
        return;

    DeferredInventoryTakeContainerRemove batch = std::move(deferred->second);
    mDeferredInventoryTakeContainerRemoves.erase(deferred);
    const std::uint32_t currentGeneration = currentCellAuthorityGeneration(batch.record.cellId);
    if (!shouldAcceptContainerAuthorityGeneration(
            currentGeneration, batch.authorityGeneration, false))
    {
        Log(Debug::Warning) << "[MP] WorldObjectSync: dropped deferred inventory-take Container(Remove)"
                            << " after authority generation changed refId=" << batch.record.refId
                            << " generation=" << batch.authorityGeneration
                            << " currentGeneration=" << currentGeneration;
        return;
    }

    Log(Debug::Info) << "[MP] WorldObjectSync: flushing deferred inventory-take Container(Remove)"
                     << " refId=" << batch.record.refId
                     << " removeRows=" << batch.record.items.size();
    if (!tryApplyContainer(batch.record, ContainerAction::Remove))
        mPendingContainer.push_back({ batch.record, ContainerAction::Remove, 0.f });
}

void WorldObjectSync::sendInventoryPutRequest(const InventoryPutRequest& request)
{
    PacketInventoryPutRequest packet;
    packet.request = request;
    mClient.sendReliable(packet.encode());
}

void WorldObjectSync::sendBarterRequest(const BarterRequest& request)
{
    PacketBarterRequest packet;
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

    const auto callback = mWorldItemTakeCallbacks.find(result.requestId);
    if (callback == mWorldItemTakeCallbacks.end())
        return;

    WorldItemTakeCallback fn = std::move(callback->second);
    mWorldItemTakeCallbacks.erase(callback);
    try
    {
        fn(result);
    }
    catch (const std::exception& e)
    {
        Log(Debug::Error) << "[MP] World item take callback failed request=" << result.requestId
                          << " error=" << e.what();
    }
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
        flushDeferredInventoryTakeContainerRemove(result.source);
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

void WorldObjectSync::onServerInventoryTakeBatchResult(const InventoryTakeBatchResult& result)
{
    Log(result.accepted ? Debug::Info : Debug::Warning)
        << "[MP] Authoritative inventory take batch result request=" << result.requestId
        << " accepted=" << result.accepted << " replayed=" << result.replayed
        << " error=" << getInventoryTakeErrorCode(result.error)
        << " kind=" << static_cast<unsigned>(result.kind)
        << " lines=" << result.lineCount << " count=" << result.itemCount
        << " theft=" << result.theft << " revision=" << result.inventoryRevision;

    const bool terminal = result.error != InventoryTakeError::SourceUnavailable
        && result.error != InventoryTakeError::StaleInventoryRevision;
    if (result.error == InventoryTakeError::SourceUnavailable)
        mInventoryTakeBatchesAwaitingSource.insert(result.requestId);
    else
        mInventoryTakeBatchesAwaitingSource.erase(result.requestId);

    if (terminal)
    {
        std::erase_if(mPendingInventoryTakeBatches,
            [&](const InventoryTakeBatchRequest& request) { return request.requestId == result.requestId; });
        mInventoryTakeBatchSources.erase(result.requestId);
        mInventoryTakeBatchItems.erase(result.requestId);
    }
    else if (result.error == InventoryTakeError::StaleInventoryRevision)
    {
        const auto pending = std::find_if(mPendingInventoryTakeBatches.begin(), mPendingInventoryTakeBatches.end(),
            [&](const InventoryTakeBatchRequest& request) { return request.requestId == result.requestId; });
        if (pending != mPendingInventoryTakeBatches.end())
        {
            pending->expectedInventoryRevision = result.inventoryRevision;
            refreshInventoryTakeBatchItems(*pending);
            sendInventoryTakeBatchRequest(*pending);
        }
    }

    if (terminal)
    {
        const auto callback = mInventoryTakeBatchCallbacks.find(result.requestId);
        if (callback != mInventoryTakeBatchCallbacks.end())
        {
            InventoryTakeBatchCallback fn = std::move(callback->second);
            mInventoryTakeBatchCallbacks.erase(callback);
            try
            {
                fn(result);
            }
            catch (const std::exception& e)
            {
                Log(Debug::Error) << "[MP] Inventory take batch callback failed request=" << result.requestId
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

void WorldObjectSync::onServerBarterResult(const BarterResult& result)
{
    Log(result.accepted ? Debug::Info : Debug::Warning)
        << "[MP] Authoritative barter result request=" << result.requestId
        << " accepted=" << result.accepted << " replayed=" << result.replayed
        << " error=" << getBarterErrorCode(result.error)
        << " balance=" << result.balance
        << " buys=" << result.buyLines << " sells=" << result.sellLines
        << " revision=" << result.inventoryRevision;

    const auto pending = std::find_if(mPendingBarters.begin(), mPendingBarters.end(),
        [&](const BarterRequest& request) { return request.requestId == result.requestId; });
    if (pending == mPendingBarters.end())
    {
        Log(Debug::Warning) << "[MP] Ignoring barter result for unknown request=" << result.requestId;
        return;
    }

    auto retryState = mBarterRetryStates.find(result.requestId);
    const bool retrySource = result.error == BarterError::SourceUnavailable
        && retryState != mBarterRetryStates.end() && !retryState->second.sourceBootstrapRetried;
    const bool retryRevision = result.error == BarterError::StaleInventoryRevision
        && retryState != mBarterRetryStates.end() && !retryState->second.inventoryRevisionRetried;
    const bool terminal = result.accepted || (!retrySource && !retryRevision);
    if (result.error == BarterError::SourceUnavailable)
    {
        if (retrySource)
        {
            retryState->second.sourceBootstrapRetried = true;
            mBartersAwaitingSource.insert(result.requestId);
            mBarterMissingSources[result.requestId] = result.missingSources;
        }
    }
    else
    {
        mBartersAwaitingSource.erase(result.requestId);
        mBarterMissingSources.erase(result.requestId);
    }

    if (terminal)
    {
        std::erase_if(mPendingBarters,
            [&](const BarterRequest& request) { return request.requestId == result.requestId; });
        mBarterSources.erase(result.requestId);
        mBarterMerchants.erase(result.requestId);
        mBartersAwaitingSource.erase(result.requestId);
        mBarterMissingSources.erase(result.requestId);
        mBarterRetryStates.erase(result.requestId);
    }
    else if (retryRevision)
    {
        retryState->second.inventoryRevisionRetried = true;
        pending->expectedInventoryRevision = result.inventoryRevision;
        sendBarterRequest(*pending);
    }

    if (terminal)
    {
        const auto callback = mBarterCallbacks.find(result.requestId);
        if (callback != mBarterCallbacks.end())
        {
            BarterCallback fn = std::move(callback->second);
            mBarterCallbacks.erase(callback);
            try
            {
                fn(result);
            }
            catch (const std::exception& e)
            {
                Log(Debug::Error) << "[MP] Barter callback failed request=" << result.requestId
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
void WorldObjectSync::invalidateContainerCellForReset(const std::string& cellId)
{
    std::erase_if(mContainerIdentitySnapshots,
        [&](const auto& entry) { return entry.second.record.cellId == cellId; });
    if (cellId.empty())
        return;

    MWBase::World* world = MWBase::Environment::get().getWorld();
    std::unordered_set<std::uint32_t> resetMpNums;
    std::vector<MWWorld::Ptr> resetPlacedObjects;
    for (const auto& [mpNum, ptr] : mObjects)
    {
        if (!ptr.isEmpty() && makeCellId(ptr) == cellId)
        {
            resetMpNums.insert(mpNum);
            resetPlacedObjects.push_back(ptr);
        }
    }
    for (const std::uint32_t mpNum : resetMpNums)
    {
        unregisterObject(mpNum);
        mLastKnownObjectPositions.erase(mpNum);
    }

    std::vector<MWWorld::Ptr> resetPendingLocalObjects;
    for (const PendingLocalPlace& pending : mPendingLocalPlace)
    {
        if (pending.cellId == cellId && !pending.ptr.isEmpty())
            resetPendingLocalObjects.push_back(pending.ptr);
    }

    if (world)
    {
        const bool previousSuppression = mSuppressLocalDelete;
        mSuppressLocalDelete = true;
        for (const MWWorld::Ptr& ptr : resetPlacedObjects)
            world->deleteObject(ptr);
        for (const MWWorld::Ptr& ptr : resetPendingLocalObjects)
            world->deleteObject(ptr);
        mSuppressLocalDelete = previousSuppression;
    }

    std::erase_if(mPendingPlace, [&](const PendingPlace& pending) { return pending.cellId == cellId; });
    std::erase_if(mPendingLocalPlace, [&](const PendingLocalPlace& pending) { return pending.cellId == cellId; });
    std::erase_if(mPendingDelete, [&](const PendingDelete& pending) { return pending.identity.cellId == cellId; });
    std::erase_if(mPendingCount, [&](const PendingCount& pending) { return pending.identity.cellId == cellId; });
    std::erase_if(mPendingMove, [&](const PendingMove& pending) { return resetMpNums.contains(pending.mpNum); });
    const std::string prefix = cellId + '\0';
    std::erase_if(mPendingContainer, [&](const PendingContainer& pending) { return pending.record.cellId == cellId; });
    std::erase_if(mDeferredInventoryTakeContainerRemoves,
        [&](const auto& entry) { return entry.second.record.cellId == cellId; });
    std::erase_if(mPendingHarvests, [&](const PendingHarvest& pending) { return pending.source.cellId == cellId; });

    for (auto it = mPendingInventoryTakes.begin(); it != mPendingInventoryTakes.end();)
    {
        if (it->source.cellId != cellId || it->source.actorInstanceId != 0)
        {
            ++it;
            continue;
        }
        mInventoryTakeCallbacks.erase(it->requestId);
        mInventoryTakeSources.erase(it->requestId);
        mInventoryTakesAwaitingSource.erase(it->requestId);
        it = mPendingInventoryTakes.erase(it);
    }
    for (auto it = mPendingInventoryTakeBatches.begin(); it != mPendingInventoryTakeBatches.end();)
    {
        if (it->source.cellId != cellId || it->source.actorInstanceId != 0)
        {
            ++it;
            continue;
        }
        mInventoryTakeBatchCallbacks.erase(it->requestId);
        mInventoryTakeBatchSources.erase(it->requestId);
        mInventoryTakeBatchItems.erase(it->requestId);
        mInventoryTakeBatchesAwaitingSource.erase(it->requestId);
        it = mPendingInventoryTakeBatches.erase(it);
    }
    for (auto it = mPendingInventoryPuts.begin(); it != mPendingInventoryPuts.end();)
    {
        if (it->destination.cellId != cellId || it->destination.actorInstanceId != 0)
        {
            ++it;
            continue;
        }
        mInventoryPutCallbacks.erase(it->requestId);
        mInventoryPutDestinations.erase(it->requestId);
        mInventoryPutsAwaitingDestination.erase(it->requestId);
        it = mPendingInventoryPuts.erase(it);
    }
    for (auto it = mPendingBarters.begin(); it != mPendingBarters.end();)
    {
        const bool touchesCell = std::any_of(it->lines.begin(), it->lines.end(), [&](const BarterLine& line) {
            return line.source.actorInstanceId == 0 && line.source.cellId == cellId;
        });
        if (!touchesCell)
        {
            ++it;
            continue;
        }
        mBarterCallbacks.erase(it->requestId);
        mBarterSources.erase(it->requestId);
        mBarterMerchants.erase(it->requestId);
        mBartersAwaitingSource.erase(it->requestId);
        mBarterMissingSources.erase(it->requestId);
        mBarterRetryStates.erase(it->requestId);
        it = mPendingBarters.erase(it);
    }

    std::erase_if(mOpenContainerTargets, [&](const auto& entry) { return entry.first.rfind(prefix, 0) == 0; });
    std::erase_if(mContainerRevisions, [&](const auto& entry) { return entry.first.rfind(prefix, 0) == 0; });
    std::erase_if(mPendingContainerBootstrapSets, [&](const std::string& key) { return key.rfind(prefix, 0) == 0; });
}

void WorldObjectSync::applyContainerCellReset(MWWorld::CellStore& cell, const std::string& cellId)
{
    std::size_t resetCount = 0;
    cell.forEach([&](MWWorld::Ptr ptr) -> bool
    {
        if (ptr.isEmpty() || ptr.getType() != ESM::Container::sRecordId
            || !ptr.getRefData().hasCustomData())
            return true;

        auto& store = ptr.getClass().getContainerStore(ptr);
        // Reset invalidates the prior canonical Set. Leave leveled contents
        // unresolved on every client; the server-selected authority will resolve
        // them in sendLocalContainerSnapshot() before publishing the fresh Set.
        store.resetToBaseState(false);
        MWBase::Environment::get().getWindowManager()->inventoryUpdated(ptr);
        ++resetCount;
        return true;
    });

    mContainerResetCellsPending.erase(cellId);
    Log(Debug::Info) << "[MP] WorldObjectSync: restored cached container base state after cell reset"
                     << " cell=" << cellId
                     << " containers=" << resetCount;
}

void WorldObjectSync::onServerContainer(const ContainerRecord& record, ContainerAction action,
    std::uint32_t authorityGeneration, std::uint64_t bootstrapSequence)
{
    const std::uint32_t currentGeneration = currentCellAuthorityGeneration(record.cellId);
    const bool reset = action == ContainerAction::Reset;
    if (!shouldAcceptContainerAuthorityGeneration(currentGeneration, authorityGeneration, reset))
    {
        Log(Debug::Warning) << "[MP] WorldObjectSync: dropped stale Container packet"
                            << " action=" << static_cast<unsigned>(action)
                            << " cell=" << record.cellId
                            << " generation=" << authorityGeneration
                            << " currentGeneration=" << currentGeneration;
        return;
    }
    if (reset)
    {
        invalidateContainerCellForReset(record.cellId);
        mContainerResetCellsPending.insert(record.cellId);
        auto& resetFloor = mCellResetGenerationFloors[record.cellId];
        resetFloor = std::max(resetFloor, authorityGeneration);
        if (Main::isInitialised())
            Main::get().getActorSync().invalidateCellForReset(record.cellId);

        if (MWBase::World* world = MWBase::Environment::get().getWorld())
        {
            auto& worldImp = static_cast<MWWorld::World&>(*world);
            if (MWWorld::CellStore* activeCell = findActiveCellById(worldImp, record.cellId))
                applyContainerCellReset(*activeCell, record.cellId);
        }
        return;
    }
    if (action == ContainerAction::BootstrapRequest)
    {
        onLocalContainerOpened(record.cellId, record.refId, record.refNum, record.mpNum);
        return;
    }
    if (action == ContainerAction::Set)
    {
        const auto snapshot = mContainerIdentitySnapshots.find(bootstrapSequence);
        if (bootstrapSequence != 0 && snapshot != mContainerIdentitySnapshots.end()
            && snapshot->second.authorityGeneration == authorityGeneration
            && snapshot->second.record.cellId == record.cellId
            && snapshot->second.record.refId == record.refId
            && snapshot->second.record.mpNum == record.mpNum
            && snapshot->second.record.refNum == record.refNum)
        {
            const bool bound = bindContainerSnapshotIdentities(
                snapshot->second.record.items, snapshot->second.handles, record.items);
            Log(Debug::Info) << "[MP] Container(Set) bootstrap identities source=" << record.refId
                << " mpNum=" << record.mpNum << " sequence=" << bootstrapSequence << " bound=" << bound;
        }
        std::erase_if(mContainerIdentitySnapshots, [&](const auto& entry) {
            return entry.second.record.cellId == record.cellId && entry.second.record.refId == record.refId
                && entry.second.record.mpNum == record.mpNum && entry.second.record.refNum == record.refNum;
        });
        for (const auto& item : record.items)
            Log(Debug::Info) << "[MP] Container(Set) received identity source=" << record.refId
                << " mpNum=" << record.mpNum << " instanceId=" << item.instanceId
                << " refId=" << item.refId << " count=" << item.count << " charge=" << item.charge
                << " enchant=" << item.enchantmentCharge << " soul=" << item.soul;
    }
    if (action == ContainerAction::Remove)
    {
        const std::string key = makeContainerRevisionKey(
            record.cellId, record.refId, record.refNum, record.mpNum);
        const std::size_t pendingSameSource = static_cast<std::size_t>(std::count_if(
            mPendingInventoryTakes.begin(), mPendingInventoryTakes.end(),
            [&](const InventoryTakeRequest& request) {
                return inventoryTakeSourceMatchesContainer(request.source, record);
            }));
        auto deferred = mDeferredInventoryTakeContainerRemoves.find(key);
        if (deferred != mDeferredInventoryTakeContainerRemoves.end()
            && deferred->second.authorityGeneration != authorityGeneration)
        {
            Log(Debug::Warning) << "[MP] WorldObjectSync: discarded deferred inventory-take Container(Remove)"
                                << " after authority generation changed refId=" << record.refId
                                << " oldGeneration=" << deferred->second.authorityGeneration
                                << " newGeneration=" << authorityGeneration;
            mDeferredInventoryTakeContainerRemoves.erase(deferred);
            deferred = mDeferredInventoryTakeContainerRemoves.end();
        }
        if (shouldDeferInventoryTakeContainerRemove(
                pendingSameSource, deferred != mDeferredInventoryTakeContainerRemoves.end()))
        {
            if (deferred == mDeferredInventoryTakeContainerRemoves.end())
            {
                DeferredInventoryTakeContainerRemove batch;
                batch.record = record;
                batch.authorityGeneration = authorityGeneration;
                mDeferredInventoryTakeContainerRemoves.emplace(key, std::move(batch));
            }
            else
            {
                deferred->second.record.items.insert(
                    deferred->second.record.items.end(), record.items.begin(), record.items.end());
            }
            Log(Debug::Info) << "[MP] WorldObjectSync: deferred inventory-take Container(Remove)"
                             << " refId=" << record.refId
                             << " pendingSameSource=" << pendingSameSource
                             << " removeRows=" << record.items.size();
            return;
        }
    }
    const bool applied = tryApplyContainer(record, action);
    if (!applied)
    {
        Log(Debug::Verbose) << "[MP] WorldObjectSync: queuing Container refId=" << record.refId;
        mPendingContainer.push_back({record, action, 0.f});
    }
    if (action == ContainerAction::Set)
    {
        mPendingContainerBootstrapSets.erase(makeContainerRevisionKey(
            record.cellId, record.refId, record.refNum, record.mpNum));
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
        for (InventoryTakeBatchRequest& request : mPendingInventoryTakeBatches)
        {
            if (inventoryTakeSourceMatchesContainer(request.source, record)
                && mInventoryTakeBatchesAwaitingSource.erase(request.requestId) != 0)
            {
                request.expectedInventoryRevision
                    = Main::get().getPlayerSync().localPlayer().inventoryChanges.revision;
                refreshInventoryTakeBatchItems(request);
                sendInventoryTakeBatchRequest(request);
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
        for (BarterRequest& request : mPendingBarters)
        {
            if (mBartersAwaitingSource.find(request.requestId) == mBartersAwaitingSource.end())
                continue;
            const auto matches = [&](const InventorySourceIdentity& identity) {
                if (identity.cellId != record.cellId || identity.refId != record.refId)
                    return false;
                if (identity.actorInstanceId != 0)
                    return (identity.refNum == 0 || identity.refNum == record.refNum)
                        && (identity.mpNum == 0 || identity.mpNum == record.mpNum);
                return identity.refNum == record.refNum && identity.mpNum == record.mpNum;
            };
            auto missing = mBarterMissingSources.find(request.requestId);
            if (missing == mBarterMissingSources.end())
                continue;
            std::erase_if(missing->second, matches);
            if (missing->second.empty() && mBartersAwaitingSource.erase(request.requestId) != 0)
            {
                mBarterMissingSources.erase(missing);
                request.expectedInventoryRevision
                    = Main::get().getPlayerSync().localPlayer().inventoryChanges.revision;
                sendBarterRequest(request);
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

    uint32_t refNum = ptr.getCellRef().getRefNum().mIndex;
    uint32_t mpNum = getMpNumForObject(ptr);
    if (ptr.getClass().isActor() && Main::isInitialised())
    {
        const ActorSync& actorSync = Main::get().getActorSync();
        mpNum = actorSync.getActorMpNum(ptr);
        refNum = mpNum == 0 ? actorSync.getActorCanonicalRefNum(ptr) : 0;
    }

    const std::string key = makeContainerRevisionKey(makeCellId(ptr),
        ptr.getCellRef().getRefId().serializeText(), refNum, mpNum);
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
bool WorldObjectSync::tryApplyObjectCount(const PlacedObjectIdentity& identity, std::int32_t count)
{
    if (count <= 0)
        return false;
    MWBase::World* world = MWBase::Environment::get().getWorld();
    if (!world)
        return false;

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
        if (object.isEmpty())
            return false;
    }

    object.getCellRef().setCount(count);
    Log(Debug::Info) << "[MP] WorldObjectSync: applied authoritative object count"
                     << " mpNum=" << identity.mpNum
                     << " refId=" << identity.refId
                     << " cell=" << identity.cellId
                     << " count=" << count;
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

    for (const BarterRequest& request : mPendingBarters)
    {
        const auto merchantIt = mBarterMerchants.find(request.requestId);
        if (merchantIt != mBarterMerchants.end()
            && request.merchant.cellId == record.cellId && request.merchant.refId == record.refId
            && (request.merchant.refNum == 0 || request.merchant.refNum == record.refNum)
            && (request.merchant.mpNum == 0 || request.merchant.mpNum == record.mpNum))
        {
            const MWWorld::Ptr& merchant = merchantIt->second;
            if (!merchant.isEmpty() && merchant.getCell() != nullptr && isContainerTarget(merchant))
                return merchant;
        }

        const auto sourcesIt = mBarterSources.find(request.requestId);
        if (sourcesIt == mBarterSources.end())
            continue;
        const auto& sources = sourcesIt->second;
        for (std::size_t i = 0; i < request.lines.size() && i < sources.size(); ++i)
        {
            const BarterLine& line = request.lines[i];
            if (line.kind == BarterLineKind::Sell || line.source.cellId != record.cellId
                || line.source.refId != record.refId
                || (line.source.refNum != 0 && line.source.refNum != record.refNum)
                || (line.source.mpNum != 0 && line.source.mpNum != record.mpNum))
                continue;
            const MWWorld::Ptr& source = sources[i];
            if (!source.isEmpty() && source.getCell() != nullptr && isContainerTarget(source)
                && makeCellId(source) == record.cellId)
                return source;
        }
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
        if (!store || !store->getCell())
            continue;

        const MWWorld::Cell* cell = store->getCell();
        std::string activeCellId;
        if (cell->isExterior())
        {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "EXT:%d,%d", cell->getGridX(), cell->getGridY());
            activeCellId = buffer;
        }
        else
            activeCellId = std::string(cell->getNameId());
        if (activeCellId != record.cellId)
            continue;

        // A newly active cell can still have references lazily materialized. A
        // bootstrap request must be able to resolve an untouched static container
        // by canonical refNum without requiring the authority player to open it first.
        store->load();

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
void WorldObjectSync::applyContainerRemove(
    MWWorld::ContainerStore& cstore, const std::vector<ContainerItem>& items)
{
    for (const auto& ci : items)
    {
        int remaining = ci.count;
        std::vector<MWWorld::Ptr> matches;
        for (auto it = cstore.begin(); it != cstore.end(); ++it)
        {
            if (lowerAscii(it->getCellRef().getRefId().serializeText()) == lowerAscii(ci.refId)
                && (ci.instanceId != 0
                    ? inventoryInstanceId(it->getCellRef().getRefNum()) == ci.instanceId
                    : (static_cast<int>(it->getCellRef().getCharge()) == ci.charge
                        && std::abs(it->getCellRef().getEnchantmentCharge() - ci.enchantmentCharge) < 0.001f
                        && it->getCellRef().getSoul().serializeText() == ci.soul)))
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
}

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
                    const int signedCount = ci.restocking ? -ci.count : ci.count;
                    MWWorld::ManualRef ref(esmStore, ESM::RefId::stringRefId(ci.refId), signedCount);
                    if (!ref.getPtr().isEmpty())
                    {
                        MWWorld::Ptr ptr = ref.getPtr();
                        ptr.getCellRef().setCharge(ci.charge);
                        ptr.getCellRef().setRefNum(inventoryInstanceRefNum(ci.instanceId));
                        ptr.getCellRef().setEnchantmentCharge(ci.enchantmentCharge);
                        ptr.getCellRef().setSoul(ci.soul.empty() ? ESM::RefId()
                            : ESM::RefId::deserializeText(ci.soul));
                        const auto added = cstore.add(ptr, signedCount, true, false, true);
                        setInventoryInstanceAlias(added->getCellRef().getRefNum(), ci.instanceId);
                    }
                }
            }
        }
        if (record.items.empty())
            clearDeadActorEquipmentVisuals(*world, target);

        // A Set is externally authoritative even when it arrived while a GUI
        // held a temporary-resolution handle. Releasing that handle must not
        // rebuild the positive base inventory over the reconciled snapshot.
        cstore.commitResolved();

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
            const int signedCount = ci.restocking ? -ci.count : ci.count;
            MWWorld::ManualRef ref(esmStore, ESM::RefId::stringRefId(ci.refId), signedCount);
            if (!ref.getPtr().isEmpty())
            {
                MWWorld::Ptr ptr = ref.getPtr();
                ptr.getCellRef().setCharge(ci.charge);
                ptr.getCellRef().setRefNum(inventoryInstanceRefNum(ci.instanceId));
                ptr.getCellRef().setEnchantmentCharge(ci.enchantmentCharge);
                ptr.getCellRef().setSoul(ci.soul.empty() ? ESM::RefId()
                    : ESM::RefId::deserializeText(ci.soul));
                const auto added = cstore.add(ptr, signedCount);
                setInventoryInstanceAlias(added->getCellRef().getRefNum(), ci.instanceId);
            }
        }
    }
    else if (action == ContainerAction::Remove)
    {
        applyContainerRemove(cstore, record.items);
        if (containerStoreEmpty(cstore))
            clearDeadActorEquipmentVisuals(*world, target);
    }

    if (action == ContainerAction::Set)
        MWBase::Environment::get().getWindowManager()->inventoryUpdated(target);

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
