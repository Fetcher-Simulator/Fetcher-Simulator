#include <components/sceneutil/positionattitudetransform.hpp>

#include "apps/openmw/mwclass/npc.hpp"
#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwworld/cellvisitors.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwworld/livecellref.hpp"
#include "apps/openmw/mwworld/ptr.hpp"
#include "apps/openmw/mwworld/worldmodel.hpp"

#include <components/esm3/loadnpc.hpp>
#include <components/esm3/readerscache.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace MWWorld
{
    namespace
    {
        using namespace testing;

        TEST(MWWorldPtrTest, CurrentBindingDoesNotReplaceUnregisteredOrDifferentLiveObjects)
        {
            MWClass::Npc::registerSelf();
            ESMStore store;
            ESM::ReadersCache readers;
            WorldModel world(store, readers);
            ESM::Cell record;
            record.blank();
            record.updateId();
            CellStore origin(Cell(record), store, readers);
            CellStore destination(Cell(record), store, readers);
            ESM::NPC npc;
            npc.blank();
            ESM::CellRef ref;
            ref.blank();
            ref.mRefNum = ESM::RefNum{ .mIndex = 1, .mContentFile = 0 };
            LiveCellRef<ESM::NPC> first(ref, &npc);
            LiveCellRef<ESM::NPC> replacement(ref, &npc);
            Ptr cached(&first, &origin);
            EXPECT_TRUE(world.getCurrentPtr({}).isEmpty());
            EXPECT_EQ(world.getCurrentPtr(cached).getCell(), &origin);
            world.registerPtr(Ptr(&first, &destination));
            EXPECT_EQ(world.getCurrentPtr(cached).getCell(), &destination);
            EXPECT_FALSE(world.getCurrentPtr(Ptr(&first)).isInCell());
            world.registerPtr(Ptr(&replacement, &destination));
            EXPECT_EQ(world.getCurrentPtr(cached).getBase(), &first);
            EXPECT_EQ(world.getCurrentPtr(cached).getCell(), &origin);
        }

        TEST(MWWorldPtrTest, MovedActorSurvivesOriginUnloadAfterRepeatedBorderCommits)
        {
            MWBase::Environment environment;
            MWClass::Npc::registerSelf();
            ESMStore store;
            ESM::ReadersCache readers;
            WorldModel world(store, readers);
            environment.setWorldModel(world);
            // Pre-existing content cells avoid the runtime exterior-created Lua hook.
            for (const auto [x, y] : { std::pair(-120, -55), std::pair(-121, -55), std::pair(-120, -56) })
            {
                ESM::Cell cell;
                cell.blank();
                cell.mData.mX = x;
                cell.mData.mY = y;
                cell.updateId();
                store.insert(cell);
            }
            auto& origin = world.getExterior({ -120, -55, ESM::Cell::sDefaultWorldspaceId });
            auto& neighbor = world.getExterior({ -121, -55, ESM::Cell::sDefaultWorldspaceId });
            auto& destination = world.getExterior({ -120, -56, ESM::Cell::sDefaultWorldspaceId });
            ESM::NPC npc;
            npc.blank();
            npc.mId = ESM::RefId::stringRefId("border_guard");
            ESM::CellRef ref;
            ref.blank();
            ref.mRefID = npc.mId;
            ref.mRefNum = ESM::RefNum{ .mIndex = 91706, .mContentFile = 20 };
            LiveCellRef<ESM::NPC> live(ref, &npc);
            Ptr cached(origin.insert(&live), &origin);
            world.registerPtr(cached);
            const auto id = cached.getCellRef().getRefNum();
            osg::ref_ptr<SceneUtil::PositionAttitudeTransform> node = new SceneUtil::PositionAttitudeTransform;
            cached.getRefData().setBaseNode(node);

            // Use the same current-binding lookup and same-cell gate as World::moveObject.
            auto commitMove = [&](const Ptr& previous, CellStore& target) {
                const Ptr current = world.getCurrentPtr(previous);
                return current.getCell() == &target ? current : current.getCell()->moveTo(current, &target);
            };

            // Physics crosses first; the reliable network commit still holds
            // the pre-crossing Ptr. Repeat in reverse, then leave the origin.
            origin.moveTo(cached, &neighbor);
            cached = commitMove(cached, neighbor);
            neighbor.moveTo(cached, &origin);
            cached = commitMove(cached, origin);
            cached = commitMove(cached, destination);

            ListAndResetObjectsVisitor unload;
            origin.forEach(unload, true);
            EXPECT_TRUE(unload.mObjects.empty());
            EXPECT_EQ(cached.getRefData().getBaseNode(), node.get());
            EXPECT_EQ(world.getPtr(id).getCell(), &destination);
            std::size_t destinationCount = 0;
            destination.forEach([&](const Ptr& ptr) {
                EXPECT_EQ(ptr, cached);
                ++destinationCount;
                return true;
            }, true);
            EXPECT_EQ(destinationCount, 1u);

            // The origin may be re-entered and unloaded again without creating
            // a second reference or losing the moved actor's scene binding.
            cached = commitMove(cached, origin);
            EXPECT_EQ(cached.getBase(), world.getPtr(id).getBase());
            EXPECT_EQ(origin.count(), 1u);
            EXPECT_EQ(destination.count(), 0u);
            cached = commitMove(cached, destination);
            unload.mObjects.clear();
            origin.forEach(unload, true);
            EXPECT_TRUE(unload.mObjects.empty());
            EXPECT_EQ(cached.getRefData().getBaseNode(), node.get());
        }

        TEST(MWWorldPtrTest, toStringShouldReturnHumanReadableTextRepresentationOfPtrWithNullRef)
        {
            Ptr ptr;
            EXPECT_EQ(ptr.toString(), "null object");
        }

        TEST(MWWorldPtrTest, toStringShouldReturnHumanReadableTextRepresentationOfPtrWithDeletedRef)
        {
            MWClass::Npc::registerSelf();
            ESM::NPC npc;
            npc.blank();
            npc.mId = ESM::RefId::stringRefId("Player");
            ESMStore store;
            store.insert(npc);
            ESM::CellRef cellRef;
            cellRef.blank();
            cellRef.mRefID = npc.mId;
            cellRef.mRefNum = ESM::RefNum{ .mIndex = 0x2a, .mContentFile = 0xd };
            LiveCellRef<ESM::NPC> liveCellRef(cellRef, &npc);
            liveCellRef.mData.setDeletedByContentFile(true);
            Ptr ptr(&liveCellRef);
            EXPECT_THAT(ptr.toString(), StrCaseEq("deleted object0xd00002a (NPC, \"player\")"));
        }

        TEST(MWWorldPtrTest, toStringShouldReturnHumanReadableTextRepresentationOfPtr)
        {
            MWClass::Npc::registerSelf();
            ESM::NPC npc;
            npc.blank();
            npc.mId = ESM::RefId::stringRefId("Player");
            ESMStore store;
            store.insert(npc);
            ESM::CellRef cellRef;
            cellRef.blank();
            cellRef.mRefID = npc.mId;
            cellRef.mRefNum = ESM::RefNum{ .mIndex = 0x2a, .mContentFile = 0xd };
            LiveCellRef<ESM::NPC> liveCellRef(cellRef, &npc);
            Ptr ptr(&liveCellRef);
            EXPECT_THAT(ptr.toString(), StrCaseEq("object0xd00002a (NPC, \"player\")"));
        }

        TEST(MWWorldPtrTest, underlyingLiveCellRefShouldBeDeregisteredOnDestruction)
        {
            MWClass::Npc::registerSelf();
            ESM::NPC npc;
            npc.blank();
            npc.mId = ESM::RefId::stringRefId("Player");
            ESMStore store;
            store.insert(npc);
            ESM::ReadersCache readersCache;
            WorldModel worldModel(store, readersCache);
            ESM::CellRef cellRef;
            cellRef.blank();
            cellRef.mRefID = npc.mId;
            cellRef.mRefNum = ESM::FormId{ .mIndex = 0x2a, .mContentFile = 0xd };
            {
                LiveCellRef<ESM::NPC> liveCellRef(cellRef, &npc);
                Ptr ptr(&liveCellRef);
                worldModel.registerPtr(ptr);
                ASSERT_EQ(worldModel.getPtr(cellRef.mRefNum), ptr);
            }
            EXPECT_EQ(worldModel.getPtr(cellRef.mRefNum), Ptr());
        }
    }
}
