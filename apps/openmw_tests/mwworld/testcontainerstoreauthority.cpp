#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwclass/container.hpp"
#include "apps/openmw/mwclass/misc.hpp"
#include "apps/openmw/mwworld/containerstore.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwworld/livecellref.hpp"
#include "apps/openmw/mwworld/worldmodel.hpp"

#include <components/esm3/loadcont.hpp>
#include <components/esm3/loadmisc.hpp>
#include <components/esm3/readerscache.hpp>

#include <gtest/gtest.h>

#include <optional>

namespace MWWorld
{
    namespace
    {
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
    }
}
