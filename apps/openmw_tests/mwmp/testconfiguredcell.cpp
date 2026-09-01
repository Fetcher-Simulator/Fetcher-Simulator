#include <gtest/gtest.h>

#include <apps/openmw-server/ConfiguredCell.hpp>

TEST(ConfiguredCell, PreservesNamedInteriorWithCommaSpace)
{
    EXPECT_EQ(mwmp::normalizeConfiguredCell("Seyda Neen, Census and Excise Office"),
        "Seyda Neen, Census and Excise Office");
}

TEST(ConfiguredCell, NormalizesExteriorCoordinateWhitespace)
{
    EXPECT_EQ(mwmp::normalizeConfiguredCell("3, -2"), "3,-2");
    EXPECT_EQ(mwmp::normalizeConfiguredCell("EXT:-3, -2"), "EXT:-3,-2");
}

TEST(ConfiguredCell, PreservesNonCoordinateCommaNames)
{
    EXPECT_EQ(mwmp::normalizeConfiguredCell("Balmora, Guild of Mages"), "Balmora, Guild of Mages");
    EXPECT_EQ(mwmp::normalizeConfiguredCell("Vivec, 1"), "Vivec, 1");
}
