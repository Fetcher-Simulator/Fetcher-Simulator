#include <gtest/gtest.h>

#include <components/openmw-mp/Packets/Player/PacketPlayerVehicleRequest.hpp>
#include <components/openmw-mp/Packets/Player/PacketPlayerVehicleState.hpp>

namespace
{
    TEST(PlayerVehicleStatePacket, RoundTripsPassengerSeatIdentity)
    {
        mwmp::BasePlayer outgoing;
        outgoing.guid = 22;
        outgoing.vehicle.active = true;
        outgoing.vehicle.revision = 9;
        outgoing.vehicle.parkedObjectMpNum = 7001;
        outgoing.vehicle.profileId = "fetcher.vehicles.pickup_85.v1";
        outgoing.vehicle.occupantRole = mwmp::VehicleOccupantRole::Passenger;
        outgoing.vehicle.driverGuid = 11;
        outgoing.vehicle.seatIndex = 1;

        mwmp::PacketPlayerVehicleState encoder;
        encoder.setPlayer(&outgoing);
        const std::vector<uint8_t> bytes = encoder.encode(3);

        mwmp::BasePlayer incoming;
        mwmp::PacketPlayerVehicleState decoder;
        decoder.setPlayer(&incoming);
        ASSERT_TRUE(decoder.decode(bytes));
        EXPECT_TRUE(incoming.vehicle.active);
        EXPECT_EQ(incoming.vehicle.revision, 9u);
        EXPECT_EQ(incoming.vehicle.parkedObjectMpNum, 7001u);
        EXPECT_EQ(incoming.vehicle.profileId, "fetcher.vehicles.pickup_85.v1");
        EXPECT_EQ(incoming.vehicle.occupantRole, mwmp::VehicleOccupantRole::Passenger);
        EXPECT_EQ(incoming.vehicle.driverGuid, 11u);
        EXPECT_EQ(incoming.vehicle.seatIndex, 1u);
    }

    TEST(PlayerVehicleRequestPacket, RoundTripsPassengerSeatRequest)
    {
        mwmp::PacketPlayerVehicleRequest outgoing;
        outgoing.action = mwmp::VehicleRequestAction::EnterPassenger;
        outgoing.parkedObjectMpNum = 7001;
        outgoing.driverGuid = 11;
        outgoing.seatIndex = mwmp::sAutomaticVehicleSeat;
        const std::vector<uint8_t> bytes = outgoing.encode(4);

        mwmp::PacketPlayerVehicleRequest incoming;
        ASSERT_TRUE(incoming.decode(bytes));
        EXPECT_EQ(incoming.action, mwmp::VehicleRequestAction::EnterPassenger);
        EXPECT_EQ(incoming.parkedObjectMpNum, 7001u);
        EXPECT_EQ(incoming.driverGuid, 11u);
        EXPECT_EQ(incoming.seatIndex, mwmp::sAutomaticVehicleSeat);
    }
}
