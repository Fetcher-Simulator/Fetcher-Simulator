#include <gtest/gtest.h>

#include <components/openmw-mp/InventoryTransferSound.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerInventoryTransferSound.hpp>

namespace
{
    mwmp::InventoryTransferSound soundEvent()
    {
        mwmp::InventoryTransferSound event;
        event.eventId = "inventory-take:42:request-7";
        event.actorGuid = 42;
        event.itemRefId = "misc_com_bottle_01";
        event.itemInstanceId = 9182;
        event.itemCount = 5;
        event.inventoryRevision = 13;
        event.mutation = mwmp::InventoryTransferMutation::Removed;
        event.direction = mwmp::InventoryTransferSoundDirection::Down;
        return event;
    }
}

TEST(InventoryTransferSoundProtocol, ValidatesServerAuthoredIdentityAndBounds)
{
    EXPECT_TRUE(mwmp::validateInventoryTransferSound(soundEvent()));

    auto invalid = soundEvent();
    invalid.actorGuid = 0;
    EXPECT_FALSE(mwmp::validateInventoryTransferSound(invalid));
    auto legacyStack = soundEvent();
    legacyStack.itemInstanceId = 0;
    EXPECT_TRUE(mwmp::validateInventoryTransferSound(legacyStack));
    invalid = soundEvent();
    invalid.mutation = static_cast<mwmp::InventoryTransferMutation>(0);
    EXPECT_FALSE(mwmp::validateInventoryTransferSound(invalid));
    invalid = soundEvent();
    invalid.direction = static_cast<mwmp::InventoryTransferSoundDirection>(99);
    EXPECT_FALSE(mwmp::validateInventoryTransferSound(invalid));
    invalid = soundEvent();
    invalid.itemCount = 0;
    EXPECT_FALSE(mwmp::validateInventoryTransferSound(invalid));
}

TEST(InventoryTransferSoundProtocol, RoundTripsAndRejectsMalformedPayloads)
{
    mwmp::PacketPlayerInventoryTransferSound outgoing;
    outgoing.event = soundEvent();
    const auto bytes = outgoing.encode();

    mwmp::PacketPlayerInventoryTransferSound incoming;
    ASSERT_TRUE(incoming.decode(bytes));
    EXPECT_EQ(incoming.event, outgoing.event);

    auto badDirection = bytes;
    badDirection.back() = 0;
    EXPECT_FALSE(incoming.decode(badDirection));

    auto badMutation = bytes;
    badMutation[badMutation.size() - 2] = 0;
    EXPECT_FALSE(incoming.decode(badMutation));

    auto trailing = bytes;
    trailing.push_back(0);
    EXPECT_FALSE(incoming.decode(trailing));
}
