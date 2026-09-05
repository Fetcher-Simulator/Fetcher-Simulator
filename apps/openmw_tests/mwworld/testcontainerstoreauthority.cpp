#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwclass/container.hpp"
#include "apps/openmw/mwclass/armor.hpp"
#include "apps/openmw/mwclass/weapon.hpp"
#include "apps/openmw/mwclass/misc.hpp"
#include "apps/openmw/mwgui/containeritemmodel.hpp"
#include "apps/openmw/mwmp/sync/WorldObjectSync.hpp"
#include "apps/openmw/mwworld/containerstore.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwworld/livecellref.hpp"
#include "apps/openmw/mwworld/manualref.hpp"
#include "apps/openmw/mwworld/worldmodel.hpp"
#include "apps/openmw/mwmp/sync/InventoryIdentity.hpp"

#include <components/esm3/loadcont.hpp>
#include <components/esm3/loadmisc.hpp>
#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadweap.hpp>
#include <components/esm3/loadench.hpp>
#include <components/openmw-mp/InventoryTake.hpp>
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

        class NativeInventoryTake : public testing::Test
        {
        protected:
            MWBase::Environment environment;
            ESMStore store;

            void SetUp() override
            {
                environment.setESMStore(store);
                MWClass::Armor::registerSelf();
                MWClass::Weapon::registerSelf();
                MWClass::Miscellaneous::registerSelf();
                ESM::Armor armor;
                armor.blank();
                armor.mId = ESM::RefId::stringRefId("iron_cuirass");
                armor.mData.mHealth = 300;
                store.insertStatic(armor);
                armor.mId = ESM::RefId::stringRefId("scripted_cuirass");
                armor.mScript = ESM::RefId::stringRefId("item_script");
                store.insertStatic(armor);
                ESM::Weapon weapon;
                weapon.blank();
                weapon.mId = ESM::RefId::stringRefId("sword");
                weapon.mData.mType = ESM::Weapon::LongBladeOneHand;
                weapon.mData.mHealth = 1000;
                store.insertStatic(weapon);
                ESM::Enchantment enchantment;
                enchantment.blank();
                enchantment.mId = ESM::RefId::stringRefId("test_enchantment");
                enchantment.mData.mType = ESM::Enchantment::WhenStrikes;
                enchantment.mData.mCharge = 100;
                store.insertStatic(enchantment);
                weapon.mId = ESM::RefId::stringRefId("enchanted_sword");
                weapon.mEnchant = enchantment.mId;
                store.insertStatic(weapon);
                ESM::Miscellaneous gem;
                gem.blank();
                gem.mId = ESM::RefId::stringRefId("soulgem");
                store.insertStatic(gem);
                store.setUp();
            }

            bool stacks(const mwmp::ContainerItem& left, const mwmp::ContainerItem& right)
            {
                ManualRef leftRef(store, ESM::RefId::stringRefId(left.refId), 1);
                ManualRef rightRef(store, ESM::RefId::stringRefId(right.refId), 1);
                const auto apply = [](Ptr ptr, const mwmp::ContainerItem& row) {
                    ptr.getCellRef().setCharge(row.charge);
                    ptr.getCellRef().setEnchantmentCharge(row.enchantmentCharge);
                    ptr.getCellRef().setSoul(ESM::RefId::stringRefId(row.soul));
                };
                apply(leftRef.getPtr(), left);
                apply(rightRef.getPtr(), right);
                return ContainerStore().stacks(leftRef.getPtr(), rightRef.getPtr());
            }

            auto take(std::vector<mwmp::ContainerItem>& source, const mwmp::ContainerItem& request)
            {
                return mwmp::takeAuthoritativeContainerItems(source, request.refId, request.charge,
                    request.enchantmentCharge, request.soul, request.count,
                    [this](const auto& left, const auto& right) { return stacks(left, right); });
            }
        };

        TEST_F(NativeInventoryTake, ExactDamagedArmorTakeSucceeds)
        {
            mwmp::ContainerItem row{ "iron_cuirass", 1, 130, 1 };
            ASSERT_FALSE(stacks(row, row));
            std::vector<mwmp::ContainerItem> source{ row };
            auto request = row;
            request.refId = "IRON_CUIRASS";
            auto taken = take(source, request);
            ASSERT_TRUE(taken);
            EXPECT_EQ(taken->taken, row);
            EXPECT_TRUE(source.empty());
        }

        TEST_F(NativeInventoryTake, ExactDamagedWeaponTakeSucceeds)
        {
            mwmp::ContainerItem row{ "sword", 1, 999, 1 };
            ASSERT_FALSE(stacks(row, row));
            std::vector<mwmp::ContainerItem> source{ row };
            auto taken = take(source, row);
            ASSERT_TRUE(taken);
            EXPECT_EQ(taken->taken, row);
            EXPECT_TRUE(source.empty());
        }

        TEST_F(NativeInventoryTake, DifferentConditionsCanAllBeLootedIndividually)
        {
            std::vector<mwmp::ContainerItem> source{
                { "iron_cuirass", 1, 130, 1 }, { "iron_cuirass", 1, 100, 2 }, { "iron_cuirass", 1, 50, 3 } };
            const auto original = source;
            auto middle = take(source, original[1]);
            ASSERT_TRUE(middle);
            EXPECT_EQ(middle->taken, original[1]);
            EXPECT_EQ(source, (std::vector<mwmp::ContainerItem>{ original[0], original[2] }));
            for (const auto& row : { original[2], original[0] })
            {
                auto taken = take(source, row);
                ASSERT_TRUE(taken);
                EXPECT_EQ(taken->taken, row);
            }
            EXPECT_TRUE(source.empty());
        }

        TEST_F(NativeInventoryTake, OnlyPristineUnscriptedUnusedItemsAggregate)
        {
            for (const mwmp::ContainerItem& row : {
                    mwmp::ContainerItem{ "iron_cuirass", 1, 130, 1 },
                    mwmp::ContainerItem{ "scripted_cuirass", 1, 300, 1 },
                    mwmp::ContainerItem{ "enchanted_sword", 1, 1000, 1, 50.f } })
            {
                SCOPED_TRACE(row.refId);
                ASSERT_FALSE(stacks(row, row));
                auto second = row;
                second.instanceId = 2;
                std::vector<mwmp::ContainerItem> source{ row, second };
                const auto original = source;
                auto request = row;
                request.count = 2;
                EXPECT_FALSE(take(source, request));
                EXPECT_EQ(source, original);
                for (const auto& expected : original)
                {
                    auto taken = take(source, expected);
                    ASSERT_TRUE(taken);
                    EXPECT_EQ(taken->taken, expected);
                }
                EXPECT_TRUE(source.empty());
            }
            std::vector<mwmp::ContainerItem> pristine{
                { "iron_cuirass", 1, 300, 1 }, { "iron_cuirass", 1, 300, 2 } };
            ASSERT_TRUE(stacks(pristine[0], pristine[1]));
            auto taken = take(pristine, { "iron_cuirass", 2, 300 });
            ASSERT_TRUE(taken);
            EXPECT_TRUE(pristine.empty());
        }

        TEST_F(NativeInventoryTake, SoulMetadataSelectsDistinctRows)
        {
            std::vector<mwmp::ContainerItem> source{
                { "soulgem", 1, -1, 1, -1.f, "rat" }, { "soulgem", 1, -1, 2, -1.f, "golden saint" } };
            const auto original = source;
            ASSERT_FALSE(stacks(source[0], source[1]));
            auto request = source[1];
            request.count = 2;
            EXPECT_FALSE(take(source, request));
            EXPECT_EQ(source, original);
            request.count = 1;
            auto taken = take(source, request);
            ASSERT_TRUE(taken);
            EXPECT_EQ(taken->taken, original[1]);
            EXPECT_EQ(source, (std::vector<mwmp::ContainerItem>{ original[0] }));
        }

        TEST_F(NativeInventoryTake, RepeatedLootKeepsNonStackableDestinationIdentities)
        {
            for (const auto& row : { mwmp::ContainerItem{ "iron_cuirass", 1, 130, 1 },
                    mwmp::ContainerItem{ "scripted_cuirass", 1, 300, 1 },
                    mwmp::ContainerItem{ "enchanted_sword", 1, 1000, 1, 50.f } })
            {
                SCOPED_TRACE(row.refId);
                auto second = row;
                second.instanceId = 2;
                std::vector<mwmp::ContainerItem> source{ row, second };
                std::vector<mwmp::Item> destination;
                for (uint32_t id : { 10u, 11u })
                {
                    auto taken = take(source, row);
                    ASSERT_TRUE(taken);
                    mwmp::Item added;
                    added.refId = taken->taken.refId;
                    added.count = taken->taken.count;
                    added.charge = taken->taken.charge;
                    added.enchantmentCharge = taken->taken.enchantmentCharge;
                    added.soul = taken->taken.soul;
                    added.instanceId = id;
                    EXPECT_EQ(mwmp::mergeAuthoritativeInventoryItem(destination, added,
                                  stacks(taken->taken, taken->taken)),
                        mwmp::AuthoritativeStackMutation::Added);
                }
                ASSERT_EQ(destination.size(), 2u);
                EXPECT_EQ(destination[0].instanceId, 10u);
                EXPECT_EQ(destination[1].instanceId, 11u);
                EXPECT_EQ(destination[0].charge, row.charge);
                EXPECT_EQ(destination[1].enchantmentCharge, row.enchantmentCharge);
            }
        }

        TEST_F(NativeInventoryTake, DifferentUsedEnchantmentChargesRemainIndividuallyLootable)
        {
            std::vector<mwmp::ContainerItem> source{
                { "enchanted_sword", 1, 1000, 1, 50.f }, { "enchanted_sword", 1, 1000, 2, 25.f } };
            const auto original = source;
            ASSERT_FALSE(stacks(source[0], source[1]));
            auto taken = take(source, original[1]);
            ASSERT_TRUE(taken);
            EXPECT_EQ(taken->taken, original[1]);
            EXPECT_EQ(source, (std::vector<mwmp::ContainerItem>{ original[0] }));
            taken = take(source, original[0]);
            ASSERT_TRUE(taken);
            EXPECT_EQ(taken->taken, original[0]);
            EXPECT_TRUE(source.empty());
        }

        TEST_F(NativeInventoryTake, NativeFullHealthRepresentationsCanAggregate)
        {
            std::vector<mwmp::ContainerItem> source{
                { "iron_cuirass", 1, -1, 1 }, { "iron_cuirass", 1, 300, 2 } };
            ASSERT_TRUE(stacks(source[0], source[1]));
            auto taken = take(source, { "iron_cuirass", 2, -1 });
            ASSERT_TRUE(taken);
            EXPECT_EQ(taken->backingRows.size(), 2u);
            EXPECT_TRUE(source.empty());
            // A client may retain the other native-equivalent representative.
            source = { { "iron_cuirass", 1, 300, 2 } };
            taken = take(source, { "iron_cuirass", 1, -1 });
            ASSERT_TRUE(taken);
            EXPECT_EQ(taken->taken.charge, 300);
            EXPECT_TRUE(source.empty());
        }

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

        TEST(ContainerStoreAuthority, BootstrapBindsAuthoritativeInstanceAcrossLiveMetadataDrift)
        {
            ContainerAuthorityFixture fixture;
            mwmp::clearInventoryInstanceAliases();

            ESM::CellRef sourceRef;
            sourceRef.blank();
            sourceRef.mRefID = fixture.finiteItem.mId;
            sourceRef.mRefNum = ESM::RefNum{ .mIndex = 0x1234, .mContentFile = 0xd };
            LiveCellRef<ESM::Miscellaneous> liveSource(sourceRef, &fixture.finiteItem);
            Ptr source(&liveSource);
            source.getCellRef().setCount(1);
            source.getCellRef().setCharge(998);
            source.getCellRef().setEnchantmentCharge(180.725f);

            mwmp::ContainerItem captured;
            captured.refId = source.getCellRef().getRefId().serializeText();
            captured.count = 1;
            captured.charge = 998;
            captured.enchantmentCharge = 180.392f;
            mwmp::ContainerItem accepted = captured;
            accepted.instanceId = 9001;

            EXPECT_TRUE(mwmp::WorldObjectSync::bindContainerSnapshotIdentities(
                { captured }, { source }, { accepted }));
            EXPECT_EQ(mwmp::inventoryInstanceId(source.getCellRef().getRefNum()), 9001u);
            EXPECT_FLOAT_EQ(source.getCellRef().getEnchantmentCharge(), 180.725f);

            mwmp::clearInventoryInstanceAliases();
        }

        TEST(ContainerStoreAuthority, BootstrapRejectsReorderedOrDifferentAuthoritativeRow)
        {
            ContainerAuthorityFixture fixture;
            mwmp::clearInventoryInstanceAliases();

            ESM::CellRef sourceRef;
            sourceRef.blank();
            sourceRef.mRefID = fixture.finiteItem.mId;
            sourceRef.mRefNum = ESM::RefNum{ .mIndex = 0x1235, .mContentFile = 0xd };
            LiveCellRef<ESM::Miscellaneous> liveSource(sourceRef, &fixture.finiteItem);
            Ptr source(&liveSource);
            source.getCellRef().setCount(1);

            mwmp::ContainerItem captured;
            captured.refId = source.getCellRef().getRefId().serializeText();
            captured.count = 1;
            captured.charge = -1;
            mwmp::ContainerItem accepted = captured;
            accepted.refId = "different_item";
            accepted.instanceId = 9002;

            EXPECT_FALSE(mwmp::WorldObjectSync::bindContainerSnapshotIdentities(
                { captured }, { source }, { accepted }));
            EXPECT_EQ(mwmp::inventoryInstanceId(source.getCellRef().getRefNum()), 0u);

            mwmp::clearInventoryInstanceAliases();
        }

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
