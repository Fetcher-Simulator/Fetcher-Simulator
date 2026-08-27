#include <gtest/gtest.h>

#include <components/openmw-mp/Barter.hpp>
#include <components/openmw-mp/Packets/Object/PacketBarter.hpp>
#include <components/openmw-mp/Packets/Object/PacketObjectCount.hpp>
#include <components/esm3/loadlevlist.hpp>

namespace
{
    mwmp::InventorySourceIdentity actor(std::string refId, std::uint32_t refNum)
    {
        mwmp::InventorySourceIdentity value;
        value.cellId = "Taris, Lower City Black Market";
        value.refId = std::move(refId);
        value.refNum = refNum;
        value.actorInstanceId
            = mwmp::packActorInstanceKey({ mwmp::ActorKeyKind::VanillaRefNum, refNum });
        value.migrationGeneration = 1;
        return value;
    }

    mwmp::InventorySourceIdentity container(std::string refId, std::uint32_t refNum)
    {
        mwmp::InventorySourceIdentity value;
        value.cellId = "Taris, Lower City Black Market";
        value.refId = std::move(refId);
        value.refNum = refNum;
        return value;
    }

    mwmp::BarterRequest request()
    {
        mwmp::BarterRequest value;
        value.requestId = "barter-batch-42";
        value.merchant = actor("SW_TarisLowerVendor1", 100);
        value.balance = -500;
        value.merchantGold = 1500;
        value.expectedInventoryRevision = 9;

        mwmp::BarterLine finite;
        finite.kind = mwmp::BarterLineKind::BuyFinite;
        finite.source = container("SW_TarisChestLCVend1", 101);
        finite.itemRefId = "SW_BlasterRepeater";
        finite.itemInstanceId = 4001;
        finite.itemCharge = 80;
        finite.itemEnchantmentCharge = 12.5f;
        finite.itemSoul = "soul";
        finite.count = 2;
        value.lines.push_back(finite);

        mwmp::BarterLine restock;
        restock.kind = mwmp::BarterLineKind::BuyRestocking;
        restock.source = value.merchant;
        restock.itemRefId = "SW_Spice";
        restock.itemCharge = -1;
        restock.itemEnchantmentCharge = -1.f;
        restock.count = 3;
        value.lines.push_back(restock);

        mwmp::BarterLine sale;
        sale.kind = mwmp::BarterLineKind::Sell;
        sale.itemRefId = "repair_prongs";
        sale.itemInstanceId = 9001;
        sale.itemCharge = 10;
        sale.itemEnchantmentCharge = -1.f;
        sale.count = 1;
        value.lines.push_back(sale);

        mwmp::BarterLine world;
        world.kind = mwmp::BarterLineKind::BuyWorldItem;
        world.worldObject.kind = mwmp::PlacedObjectKind::ContentReference;
        world.worldObject.cellId = value.merchant.cellId;
        world.worldObject.refId = "SW_LoosePart";
        world.worldObject.refIndex = 333;
        world.worldObject.refContentFile = 2;
        world.itemRefId = world.worldObject.refId;
        world.itemCharge = -1;
        world.itemEnchantmentCharge = -1.f;
        world.count = 1;
        value.lines.push_back(world);
        return value;
    }
}

TEST(BarterProtocol, MultiLineRequestRoundTripsAllLineKinds)
{
    const auto value = request();
    ASSERT_EQ(mwmp::validateBarterRequest(value), mwmp::BarterError::None);
    mwmp::PacketBarterRequest outgoing;
    outgoing.request = value;
    mwmp::PacketBarterRequest incoming;
    ASSERT_TRUE(incoming.decode(outgoing.encode()));
    EXPECT_EQ(incoming.request, value);
}

TEST(BarterProtocol, CanonicalHashInputIncludesLineOrderAndEveryIdentity)
{
    const auto value = request();
    const std::string canonical = mwmp::canonicalBarterRequest(value);
    const auto differs = [&](auto mutate) {
        auto changed = value;
        mutate(changed);
        EXPECT_NE(mwmp::canonicalBarterRequest(changed), canonical);
    };
    differs([](auto& v) { std::swap(v.lines[0], v.lines[1]); });
    differs([](auto& v) { v.requestId += "x"; });
    differs([](auto& v) { v.merchant.migrationGeneration++; });
    differs([](auto& v) { v.lines[0].kind = mwmp::BarterLineKind::BuyRestocking; });
    differs([](auto& v) { v.lines[0].source.refNum++; });
    differs([](auto& v) { v.lines.back().worldObject.refIndex++; });
    differs([](auto& v) { v.lines[0].itemRefId += "x"; });
    differs([](auto& v) { v.lines[0].itemInstanceId++; });
    differs([](auto& v) { v.lines[0].itemCharge++; });
    differs([](auto& v) { v.lines[0].itemEnchantmentCharge += 1.f; });
    differs([](auto& v) { v.lines[0].itemSoul = "different"; });
    differs([](auto& v) { v.lines[0].count++; });
    differs([](auto& v) { v.balance++; });
    differs([](auto& v) { v.merchantGold++; });
    differs([](auto& v) { v.expectedInventoryRevision++; });
}

TEST(BarterProtocol, RejectsInvalidCountsKindsIdentitiesAndDuplicateWorldObjects)
{
    auto value = request();
    value.lines[0].count = 0;
    EXPECT_EQ(mwmp::validateBarterRequest(value), mwmp::BarterError::InvalidCount);
    value = request();
    value.lines[0].kind = static_cast<mwmp::BarterLineKind>(99);
    EXPECT_EQ(mwmp::validateBarterRequest(value), mwmp::BarterError::InvalidRequest);
    value = request();
    value.lines[2].itemInstanceId = 0;
    EXPECT_EQ(mwmp::validateBarterRequest(value), mwmp::BarterError::InvalidRequest);
    value = request();
    value.lines[0].source.actorInstanceId = 1;
    EXPECT_EQ(mwmp::validateBarterRequest(value), mwmp::BarterError::InvalidRequest);
    value = request();
    value.lines.back().worldObject.refContentFile = -1;
    EXPECT_EQ(mwmp::validateBarterRequest(value), mwmp::BarterError::InvalidRequest);
    value = request();
    value.lines.push_back(value.lines.back());
    EXPECT_EQ(mwmp::validateBarterRequest(value), mwmp::BarterError::InvalidRequest);
    value = request();
    value.lines.resize(mwmp::MaximumBarterLines + 1, value.lines.front());
    EXPECT_EQ(mwmp::validateBarterRequest(value), mwmp::BarterError::InvalidRequest);
}

TEST(BarterProtocol, ResultCarriesAuthoritativeGoldAndMissingSources)
{
    mwmp::PacketBarterResult outgoing;
    outgoing.result.requestId = "barter-batch-42";
    outgoing.result.error = mwmp::BarterError::SourceUnavailable;
    outgoing.result.inventoryRevision = 9;
    outgoing.result.balance = -500;
    outgoing.result.merchantGold = 1500;
    outgoing.result.buyLines = 3;
    outgoing.result.sellLines = 1;
    outgoing.result.missingSources = { container("chest_a", 101), actor("merchant", 100) };
    mwmp::PacketBarterResult incoming;
    ASSERT_TRUE(incoming.decode(outgoing.encode()));
    EXPECT_EQ(incoming.result, outgoing.result);

    outgoing.result.missingSources.clear();
    EXPECT_FALSE(incoming.decode(outgoing.encode()));
}

TEST(BarterRestock, ProvesEligibleDirectAndNestedLeveledListDescendantsWithoutRerolling)
{
    ESM::ItemLevList nested;
    nested.mId = ESM::RefId::stringRefId("nested_list");
    nested.mFlags = 0;
    nested.mChanceNone = 100;
    nested.mList = { { ESM::RefId::stringRefId("spice"), 5 } };

    ESM::ItemLevList root;
    root.mId = ESM::RefId::stringRefId("root_list");
    root.mFlags = 0;
    root.mChanceNone = 100;
    root.mList = {
        { ESM::RefId::stringRefId("low_item"), 1 },
        { nested.mId, 5 },
        { ESM::RefId::stringRefId("high_item"), 10 },
    };
    const auto find = [&](std::string_view id) -> const ESM::ItemLevList* {
        return id == "nested_list" ? &nested : nullptr;
    };

    // ChanceNone is deliberately ignored: the concrete authoritative snapshot
    // already proves that an item materialized.
    EXPECT_TRUE(mwmp::isEligibleBarterRestockDescendant(root, "spice", 5, find));
    EXPECT_FALSE(mwmp::isEligibleBarterRestockDescendant(root, "high_item", 5, find));
    EXPECT_FALSE(mwmp::isEligibleBarterRestockDescendant(root, "low_item", 5, find));
    EXPECT_FALSE(mwmp::isEligibleBarterRestockDescendant(root, "unrelated", 50, find));

    root.mFlags = ESM::ItemLevList::AllLevels;
    EXPECT_TRUE(mwmp::isEligibleBarterRestockDescendant(root, "low_item", 10, find));
}

TEST(BarterMerchantGold, RestocksAtVanillaDelayAndCarriesExpectedStateForCas)
{
    const mwmp::BarterMerchantGoldState stored { 125, 100.0 };

    const auto beforeReset = mwmp::resolveBarterMerchantGold(1000, stored, 123.99, 24.0);
    EXPECT_EQ(beforeReset.authoritativeGold, 125);
    EXPECT_EQ(beforeReset.expectedGold, 125);
    EXPECT_DOUBLE_EQ(beforeReset.expectedRestockTime, 100.0);
    EXPECT_DOUBLE_EQ(beforeReset.resultingRestockTime, 100.0);
    EXPECT_TRUE(beforeReset.hadStoredState);
    EXPECT_FALSE(beforeReset.resetApplied);

    const auto atReset = mwmp::resolveBarterMerchantGold(1000, stored, 124.0, 24.0);
    EXPECT_EQ(atReset.authoritativeGold, 1000);
    EXPECT_EQ(atReset.expectedGold, 125);
    EXPECT_DOUBLE_EQ(atReset.expectedRestockTime, 100.0);
    EXPECT_DOUBLE_EQ(atReset.resultingRestockTime, 124.0);
    EXPECT_TRUE(atReset.hadStoredState);
    EXPECT_TRUE(atReset.resetApplied);

    const auto firstVisit = mwmp::resolveBarterMerchantGold(1000, std::nullopt, 500.0, 24.0);
    EXPECT_EQ(firstVisit.authoritativeGold, 1000);
    EXPECT_EQ(firstVisit.expectedGold, 1000);
    EXPECT_DOUBLE_EQ(firstVisit.expectedRestockTime, 0.0);
    EXPECT_DOUBLE_EQ(firstVisit.resultingRestockTime, 500.0);
    EXPECT_FALSE(firstVisit.hadStoredState);
    EXPECT_TRUE(firstVisit.resetApplied);

    const auto afterServerClockRollback = mwmp::resolveBarterMerchantGold(1000, stored, 8.0, 24.0);
    EXPECT_EQ(afterServerClockRollback.authoritativeGold, 1000);
    EXPECT_DOUBLE_EQ(afterServerClockRollback.expectedRestockTime, 100.0);
    EXPECT_DOUBLE_EQ(afterServerClockRollback.resultingRestockTime, 8.0);
    EXPECT_TRUE(afterServerClockRollback.resetApplied);
}

TEST(BarterProtocol, ObjectCountPacketRoundTripsPartialPlacedStack)
{
    mwmp::PacketObjectCount outgoing;
    outgoing.object.kind = mwmp::PlacedObjectKind::ContentReference;
    outgoing.object.cellId = "Taris, Lower City Black Market";
    outgoing.object.refId = "SW_LooseGrenades";
    outgoing.object.refIndex = 777;
    outgoing.object.refContentFile = 2;
    outgoing.count = 7;

    mwmp::PacketObjectCount incoming;
    ASSERT_TRUE(incoming.decode(outgoing.encode()));
    EXPECT_EQ(incoming.object, outgoing.object);
    EXPECT_EQ(incoming.count, 7);
}
