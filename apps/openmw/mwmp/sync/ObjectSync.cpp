#include "ObjectSync.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <utility>

#include <osg/Math>

#include <components/debug/debuglog.hpp>
#include <components/openmw-mp/Packets/Object/PacketDoorState.hpp>
#include <components/esm3/loaddoor.hpp>

#include "../../mwbase/environment.hpp"
#include "../../mwbase/soundmanager.hpp"
#include "../../mwbase/world.hpp"
#include "../../mwworld/cellreflist.hpp"
#include "../../mwworld/cellstore.hpp"
#include "../../mwworld/class.hpp"
#include "../../mwworld/doorstate.hpp"
#include "../../mwworld/livecellref.hpp"
#include "../../mwworld/ptr.hpp"
#include "../../mwworld/scene.hpp"
#include "../../mwworld/worldimp.hpp"

#include "../network/Client.hpp"
#include "../Main.hpp"
#include "../sync/PlayerSync.hpp"

namespace mwmp
{
namespace
{
    std::string doorIdentityKey(const std::string& cellId, const std::string& refId, std::uint32_t refNum)
    {
        std::string normalizedRefId = refId;
        std::transform(normalizedRefId.begin(), normalizedRefId.end(), normalizedRefId.begin(),
            [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        return cellId + "|" + normalizedRefId + "|" + std::to_string(refNum);
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
}

ObjectSync::ObjectSync(NetworkClient& client)
    : mClient(client)
{
}

// ---------------------------------------------------------------------------
void ObjectSync::onDoorStateChanged(const std::string& cellId,
                                    const std::string& refId,
                                    uint32_t           refNum,
                                    bool               isOpen,
                                    bool               isLocked,
                                    int                lockLevel)
{
    DoorEntry entry;
    entry.cellId    = cellId;
    entry.refId     = refId;
    entry.refNum    = refNum;
    entry.isOpen    = isOpen;
    entry.isLocked  = isLocked;
    entry.lockLevel = lockLevel;
    std::uint64_t& revision = mDoorRevisions[doorIdentityKey(cellId, refId, refNum)];
    if (revision == std::numeric_limits<std::uint64_t>::max())
        return;
    entry.revision = ++revision;

    mOutgoingDoors.push_back({ cellId, std::move(entry) });
}

// ---------------------------------------------------------------------------
void ObjectSync::flushOutgoingDoorStates()
{
    if (mOutgoingDoors.empty())
        return;

    std::vector<OutgoingDoor> outgoing;
    outgoing.swap(mOutgoingDoors);

    for (const auto& door : outgoing)
    {
        PacketDoorState pkt;
        pkt.authorGuid = Main::get().getPlayerSync().localPlayer().guid;
        pkt.cellId = door.cellId;
        pkt.doors.push_back(door.entry);

        mClient.sendReliable(pkt.encode());
    }
}

// ---------------------------------------------------------------------------
bool ObjectSync::tryApplyDoorState(const std::string& cellId,
                                   const std::string& refId,
                                   uint32_t           refNum,
                                   bool               isOpen,
                                   bool               isLocked,
                                   int                lockLevel,
                                   bool               bootstrap)
{
    MWBase::World* world = MWBase::Environment::get().getWorld();
    if (!world)
        return false;

    const MWWorld::DoorState targetState = isOpen
        ? MWWorld::DoorState::Opening
        : MWWorld::DoorState::Closing;

    auto& scene = static_cast<MWWorld::World*>(world)->getWorldScene();
    for (MWWorld::CellStore* store : scene.getActiveCells())
    {
        if (!store || makeCellId(*store) != cellId)
            continue;

        for (const auto& liveRef : store->getReadOnlyDoors().mList)
        {
            if (liveRef.mRef.getRefId().toString() != refId)
                continue;
            if (refNum != 0 && liveRef.mRef.getRefNum().mIndex != refNum)
                continue;

            MWWorld::Ptr doorPtr(
                const_cast<MWWorld::LiveCellRefBase*>(
                    static_cast<const MWWorld::LiveCellRefBase*>(&liveRef)),
                store);

            doorPtr.getCellRef().setLockLevel(lockLevel);
            doorPtr.getCellRef().setLocked(isLocked);

            const float closedRotation = doorPtr.getCellRef().getPosition().rot[2];
            const float targetRotation = closedRotation + (isOpen ? osg::DegreesToRadians(90.f) : 0.f);

            if (bootstrap)
            {
                const ESM::Position& currentPosition = doorPtr.getRefData().getPosition();
                if (std::abs(currentPosition.rot[2] - targetRotation) > 0.0001f)
                {
                    world->rotateObject(doorPtr,
                        osg::Vec3f(currentPosition.rot[0], currentPosition.rot[1], targetRotation),
                        MWBase::RotationFlag_none);
                }
                doorPtr.getClass().setDoorState(doorPtr, MWWorld::DoorState::Idle);
                Log(Debug::Verbose) << "[MP] ObjectSync: applied silent authoritative door bootstrap"
                                    << " cell=" << cellId
                                    << " refId=" << refId
                                    << " refNum=" << refNum
                                    << " open=" << isOpen
                                    << " locked=" << isLocked;
                return true;
            }

            const MWWorld::DoorState currentState = doorPtr.getClass().getDoorState(doorPtr);
            const float currentRotation = doorPtr.getRefData().getPosition().rot[2];
            const bool visuallyOpen = std::abs(currentRotation - closedRotation) > 0.0001f;
            const bool alreadyAtTarget = isOpen
                ? currentState == MWWorld::DoorState::Opening
                    || (currentState == MWWorld::DoorState::Idle && visuallyOpen)
                : currentState == MWWorld::DoorState::Closing
                    || (currentState == MWWorld::DoorState::Idle && !visuallyOpen);
            if (alreadyAtTarget)
            {
                Log(Debug::Verbose) << "[MPDIAG] ObjectSync door state already applied"
                                    << " cell=" << cellId
                                    << " refId=" << refId
                                    << " refNum=" << refNum
                                    << " isOpen=" << isOpen;
                return true;
            }

            if (MWBase::SoundManager* sound = MWBase::Environment::get().getSoundManager())
            {
                const ESM::RefId& soundId = isOpen ? liveRef.mBase->mOpenSound : liveRef.mBase->mCloseSound;
                if (!soundId.empty())
                    sound->playSound3D(doorPtr, soundId, 1.0f, 1.0f);
            }

            world->activateDoor(doorPtr, targetState);
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
void ObjectSync::onServerDoorState(const std::string& cellId,
                                   const std::string& refId,
                                   uint32_t           refNum,
                                   bool               isOpen,
                                   bool               isLocked,
                                   int                lockLevel,
                                   std::uint64_t       revision,
                                   bool                bootstrap)
{
    mDoorRevisions[doorIdentityKey(cellId, refId, refNum)] = revision;
    if (!tryApplyDoorState(cellId, refId, refNum, isOpen, isLocked, lockLevel, bootstrap))
    {
        // Cell not loaded yet (e.g. pre-world bootstrap). Keep the whole
        // authoritative revision so it can be applied before scene insertion.
        Log(Debug::Verbose) << "[MP] ObjectSync: queuing door state for retry: " << refId;
        mPendingDoors.push_back({ cellId, refId, refNum, isOpen, isLocked, lockLevel, bootstrap, 0.f });
    }
}

void ObjectSync::prepareCellForInsertion(MWWorld::CellStore& cell)
{
    const std::string cellId = makeCellId(cell);
    if (cellId.empty() || mPendingDoors.empty())
        return;

    mPendingDoors.erase(
        std::remove_if(mPendingDoors.begin(), mPendingDoors.end(),
            [&](const PendingDoor& pending)
            {
                if (!pending.bootstrap || pending.cellId != cellId)
                    return false;

                for (const auto& liveRef : cell.getReadOnlyDoors().mList)
                {
                    if (liveRef.mRef.getRefId().toString() != pending.refId)
                        continue;
                    if (pending.refNum != 0 && liveRef.mRef.getRefNum().mIndex != pending.refNum)
                        continue;

                    MWWorld::Ptr doorPtr(
                        const_cast<MWWorld::LiveCellRefBase*>(
                            static_cast<const MWWorld::LiveCellRefBase*>(&liveRef)),
                        &cell);
                    doorPtr.getCellRef().setLockLevel(pending.lockLevel);
                    doorPtr.getCellRef().setLocked(pending.isLocked);

                    ESM::Position position = doorPtr.getRefData().getPosition();
                    position.rot[2] = doorPtr.getCellRef().getPosition().rot[2]
                        + (pending.isOpen ? osg::DegreesToRadians(90.f) : 0.f);
                    doorPtr.getRefData().setPosition(position);
                    doorPtr.getClass().setDoorState(doorPtr, MWWorld::DoorState::Idle);

                    Log(Debug::Verbose) << "[MP] ObjectSync: applied pre-insertion door bootstrap"
                                        << " cell=" << cellId
                                        << " refId=" << pending.refId
                                        << " refNum=" << pending.refNum
                                        << " open=" << pending.isOpen
                                        << " locked=" << pending.isLocked;
                    return true;
                }
                return false;
            }),
        mPendingDoors.end());
}

void ObjectSync::resetSessionState()
{
    mOutgoingDoors.clear();
    mPendingDoors.clear();
    mDoorRevisions.clear();
}

// ---------------------------------------------------------------------------
void ObjectSync::update(float dt)
{
    flushOutgoingDoorStates();

    if (mPendingDoors.empty()) return;

    static constexpr float RetryRate = 0.2f; // retry every 200ms

    mPendingDoors.erase(
        std::remove_if(mPendingDoors.begin(), mPendingDoors.end(),
            [&](PendingDoor& pd) -> bool
            {
                pd.retryTimer += dt;
                if (pd.retryTimer < RetryRate) return false;
                pd.retryTimer = 0.f;
                if (tryApplyDoorState(
                        pd.cellId, pd.refId, pd.refNum, pd.isOpen, pd.isLocked, pd.lockLevel, pd.bootstrap))
                    return true; // applied — remove from pending
                return false;   // still not found — keep retrying
            }),
        mPendingDoors.end());
}

} // namespace mwmp
