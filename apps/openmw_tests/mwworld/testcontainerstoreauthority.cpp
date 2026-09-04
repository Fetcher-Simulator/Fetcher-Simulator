#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwclass/container.hpp"
#include "apps/openmw/mwclass/misc.hpp"
#include "apps/openmw/mwgui/containeritemmodel.hpp"
#include "apps/openmw/mwmp/sync/WorldObjectSync.hpp"
#include "apps/openmw/mwworld/containerstore.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwworld/livecellref.hpp"
#include "apps/openmw/mwworld/worldmodel.hpp"
#include "apps/openmw/mwmp/sync/InventoryIdentity.hpp"

#include <components/esm3/loadcont.hpp>
#include <components/esm3/loadmisc.hpp>
#include <components/esm3/readerscache.hpp>

#include <gtest/gtest.h>

#include <optional>

namespace MWWorld
{
    namespace
    {
        struct ExposedContainerStore : ContainerStore
        {
            using ContainerStore::addNewStack;
        };

        struct ContainerAuthorityFixture
        {
            MWBase::Environment environment;
            ESMStore store;
            ESM::ReadersCache readers;
            WorldModel worldModel{ store, readers };
            ESM::Miscellaneous finiteItem;
            ESM::Miscellaneous restockingItem;
            ESM::Container container;
            ESM::CellRef containerRef;
            std::optional<LiveCellRef<ESM::Container>> liveContainer;

            ContainerAuthorityFixture()
            {
                MWClass::Container::registerSelf();
                MWClass::Miscellaneous::registerSelf();
                environment.setESMStore(store);
                environment.setWorldModel(worldModel);

                finiteItem.blank();
                finiteItem.mId = ESM::RefId::stringRefId("finite_merchant_item");
                store.insertStatic(finiteItem);
                restockingItem.blank();
                restockingItem.mId = ESM::RefId::stringRefId("restocking_merchant_item");
                store.insertStatic(restockingItem);
                store.setUp();

                container.blank();
                container.mId = ESM::RefId::stringRefId("merchant_chest");
                container.mInventory.mList = {
                    { 1, finiteItem.mId },
                    { -2, restockingItem.mId },
                };
                containerRef.blank();
                containerRef.mRefID = container.mId;
                containerRef.mRefNum = ESM::RefNum{ .mIndex = 0x2a, .mContentFile = 0xd };
                liveContainer.emplace(containerRef, &container);
                worldModel.registerPtr(containerPtr());
            }

            Ptr containerPtr() { return Ptr(&*liveContainer); }
        };

        TEST(ContainerStoreAuthority, CopiedStackCanDropSourceInventoryIdentity)
        {
            ContainerAuthorityFixture fixture;

            ESM::CellRef sourceRef;
            sourceRef.blank();
            sourceRef.mRefID = fixture.finiteItem.mId;
            sourceRef.mRefNum = mwmp::inventoryInstanceRefNum(6537);
            LiveCellRef<ESM::Miscellaneous> liveSource(sourceRef, &fixture.finiteItem);
            Ptr source(&liveSource);
            source.getCellRef().setCount(840);

            ExposedContainerStore preserved;
            const auto preservedIt = preserved.addNewStack(source, 1);
            ASSERT_NE(preservedIt, preserved.end());
            EXPECT_EQ(preservedIt->getCellRef().getCount(), 1);
            EXPECT_EQ(mwmp::inventoryInstanceId(preservedIt->getCellRef().getRefNum()), 6537u);

            ExposedContainerStore detached;
            const auto detachedIt = detached.addNewStack(source, 1, false);
            ASSERT_NE(detachedIt, detached.end());
            EXPECT_EQ(detachedIt->getCellRef().getCount(), 1);
            EXPECT_EQ(mwmp::inventoryInstanceId(detachedIt->getCellRef().getRefNum()), 0u);
            EXPECT_FALSE(detachedIt->getCellRef().getRefNum().isSet());

            EXPECT_EQ(source.getCellRef().getCount(), 840);
            EXPECT_EQ(mwmp::inventoryInstanceId(source.getCellRef().getRefNum()), 6537u);
        }

        TEST(ContainerStoreAuthority, AuthoritativeTemporaryResolutionSurvivesHandleRelease)
        {
            ContainerAuthorityFixture fixture;
            ContainerStore store;
            store.setPtr(fixture.containerPtr());
            {
                ResolutionHandle handle = store.resolveTemporarily();
                ASSERT_TRUE(store.isResolved());
                for (auto item = store.begin(); item != store.end(); ++item)
                {
                    if (item->getCellRef().getRefId() == fixture.finiteItem.mId)
                        item->getCellRef().setCount(0);
                }
                store.commitResolved();
            }

            EXPECT_TRUE(store.isResolved());
            int finiteCount = 0;
            int restockingCount = 0;
            for (auto item = store.begin(); item != store.end(); ++item)
            {
                if (item->getCellRef().getRefId() == fixture.finiteItem.mId)
                    finiteCount += item->getCellRef().getCount(false);
                if (item->getCellRef().getRefId() == fixture.restockingItem.mId)
                    restockingCount += item->getCellRef().getCount(false);
            }
            EXPECT_EQ(finiteCount, 0);
            EXPECT_EQ(restockingCount, -2);
        }

        TEST(ContainerStoreAuthority, UncommittedTemporaryResolutionRetainsNormalUnresolvedSemantics)
        {
            ContainerAuthorityFixture fixture;
            ContainerStore store;
            store.setPtr(fixture.containerPtr());
            {
                ResolutionHandle handle = store.resolveTemporarily();
                ASSERT_TRUE(store.isResolved());
            }

            EXPECT_FALSE(store.isResolved());
            int finiteCount = 0;
            int restockingCount = 0;
            for (auto item = store.begin(); item != store.end(); ++item)
            {
                if (item->getCellRef().getRefId() == fixture.finiteItem.mId)
                    finiteCount += item->getCellRef().getCount(false);
                if (item->getCellRef().getRefId() == fixture.restockingItem.mId)
                    restockingCount += item->getCellRef().getCount(false);
            }
            EXPECT_EQ(finiteCount, 1);
            EXPECT_EQ(restockingCount, -2);

        }
        TEST(ContainerStoreAuthority, DeferredModelDoesNotResolveUntilAuthoritativeSet)
        {
            ContainerAuthorityFixture fixture;
            ContainerStore store;
            store.setPtr(fixture.containerPtr());

            MWWorld::ResolutionHandle handle = MWGui::ContainerItemModel::resolveContentsForDisplay(store,
                MWGui::ContainerItemModel::ContentResolution::RequireAuthoritative);
            EXPECT_FALSE(store.isResolved());
            EXPECT_FALSE(MWGui::ContainerItemModel::canDisplayContents(store,
                MWGui::ContainerItemModel::ContentResolution::RequireAuthoritative));

            store.resolve();
            store.commitResolved();
            EXPECT_TRUE(MWGui::ContainerItemModel::canDisplayContents(store,
                MWGui::ContainerItemModel::ContentResolution::RequireAuthoritative));
        }

        TEST(ContainerStoreAuthority, AuthoritySnapshotMayResolveAndPublishConcreteContents)
        {
            ContainerAuthorityFixture fixture;
            ContainerStore store;
            store.setPtr(fixture.containerPtr());

            MWWorld::ResolutionHandle handle = MWGui::ContainerItemModel::resolveContentsForDisplay(store,
                MWGui::ContainerItemModel::ContentResolution::RequireAuthoritative);
            EXPECT_FALSE(store.isResolved());

            store.resolve();
            store.commitResolved();
            EXPECT_TRUE(store.isResolved());
            EXPECT_TRUE(MWGui::ContainerItemModel::canDisplayContents(store,
                MWGui::ContainerItemModel::ContentResolution::RequireAuthoritative));
        }

        TEST(ContainerStoreAuthority, SinglePlayerModelPreservesTemporaryResolution)
        {
            ContainerAuthorityFixture fixture;
            ContainerStore store;
            store.setPtr(fixture.containerPtr());

            MWWorld::ResolutionHandle handle = MWGui::ContainerItemModel::resolveContentsForDisplay(store,
                MWGui::ContainerItemModel::ContentResolution::ResolveTemporarily);
            EXPECT_TRUE(store.isResolved());
        }

        TEST(ContainerStoreAuthority, OrganicAuthoritySnapshotResolvesBeforeIteration)
        {
            ContainerAuthorityFixture fixture;
            fixture.container.mFlags |= ESM::Container::Organic;
            ContainerStore store;
            store.setPtr(fixture.containerPtr());
            EXPECT_FALSE(store.isResolved());

            mwmp::WorldObjectSync::resolveContainerForAuthoritativeSnapshot(store);

            EXPECT_TRUE(store.isResolved());
            EXPECT_NE(store.begin(), store.end());
        }

        TEST(ContainerStoreAuthority, DeferredModelImmediatelyUsesKnownAuthoritativeContents)
        {
            ContainerAuthorityFixture fixture;
            ContainerStore store;
            store.setPtr(fixture.containerPtr());
            store.resolve();
            store.commitResolved();

            MWWorld::ResolutionHandle handle = MWGui::ContainerItemModel::resolveContentsForDisplay(store,
                MWGui::ContainerItemModel::ContentResolution::RequireAuthoritative);
            EXPECT_TRUE(store.isResolved());
            EXPECT_TRUE(MWGui::ContainerItemModel::canDisplayContents(store,
                MWGui::ContainerItemModel::ContentResolution::RequireAuthoritative));
        }

        TEST(ContainerStoreAuthority, ResetToBaseStateDiscardsAuthoritativeMutationAndResolvesVanillaSeed)
        {
            ContainerAuthorityFixture fixture;
            ContainerStore store;
            store.setPtr(fixture.containerPtr());
            store.resolve();
            for (auto item = store.begin(); item != store.end(); ++item)
            {
                if (item->getCellRef().getRefId() == fixture.finiteItem.mId)
                    item->getCellRef().setCount(99);
                if (item->getCellRef().getRefId() == fixture.restockingItem.mId)
                    item->getCellRef().setCount(-9);
            }
            store.commitResolved();

            store.resetToBaseState();

            EXPECT_TRUE(store.isResolved());
            int finiteCount = 0;
            int restockingCount = 0;
            for (auto item = store.begin(); item != store.end(); ++item)
            {
                if (item->getCellRef().getRefId() == fixture.finiteItem.mId)
                    finiteCount += item->getCellRef().getCount(false);
                if (item->getCellRef().getRefId() == fixture.restockingItem.mId)
                    restockingCount += item->getCellRef().getCount(false);
            }
            EXPECT_EQ(finiteCount, 1);
            EXPECT_EQ(restockingCount, -2);
        }

        TEST(ContainerStoreAuthority, DeferredResetWaitsForFreshAuthorityResolution)
        {
            ContainerAuthorityFixture fixture;
            ContainerStore store;
            store.setPtr(fixture.containerPtr());
            store.resolve();
            store.commitResolved();

            store.resetToBaseState(false);
            EXPECT_FALSE(store.isResolved());

            store.resolve();
            store.commitResolved();
            EXPECT_TRUE(store.isResolved());
            int finiteCount = 0;
            int restockingCount = 0;
            for (auto item = store.begin(); item != store.end(); ++item)
            {
                if (item->getCellRef().getRefId() == fixture.finiteItem.mId)
                    finiteCount += item->getCellRef().getCount(false);
                if (item->getCellRef().getRefId() == fixture.restockingItem.mId)
                    restockingCount += item->getCellRef().getCount(false);
            }
            EXPECT_EQ(finiteCount, 1);
            EXPECT_EQ(restockingCount, -2);
        }
    }
}
