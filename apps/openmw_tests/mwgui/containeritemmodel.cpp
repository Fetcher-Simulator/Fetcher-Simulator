#include <gtest/gtest.h>

#include "../../openmw/mwgui/containeritemmodel.hpp"

namespace
{
    TEST(ContainerItemModelTradingPolicy, ExcludesEquippedActorStackBeforeMerchantMerging)
    {
        EXPECT_FALSE(MWGui::ContainerItemModel::shouldIncludeTradingSourceItem(true, true, true));
    }

    TEST(ContainerItemModelTradingPolicy, KeepsUnequippedActorAndOwnedContainerStock)
    {
        EXPECT_TRUE(MWGui::ContainerItemModel::shouldIncludeTradingSourceItem(true, true, false));
        EXPECT_TRUE(MWGui::ContainerItemModel::shouldIncludeTradingSourceItem(true, false, false));
        EXPECT_TRUE(MWGui::ContainerItemModel::shouldIncludeTradingSourceItem(true, false, true));
    }

    TEST(ContainerItemModelTradingPolicy, NonTradingModelsKeepEquippedInventoryItems)
    {
        EXPECT_TRUE(MWGui::ContainerItemModel::shouldIncludeTradingSourceItem(false, true, true));
    }
}
