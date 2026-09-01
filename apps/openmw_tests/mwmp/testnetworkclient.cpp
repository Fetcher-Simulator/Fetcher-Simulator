#include <gtest/gtest.h>

#include <apps/openmw/mwmp/network/Client.hpp>

TEST(NetworkClientTest, DestructorDoesNotNotifyOwner)
{
    int callbacks = 0;
    {
        mwmp::NetworkClient client;
        client.setStateChangeCallback(
            [&](mwmp::ConnectionState, mwmp::ConnectionState) { ++callbacks; });

        ASSERT_TRUE(client.connect("127.0.0.1", 9));
        ASSERT_EQ(client.getState(), mwmp::ConnectionState::Connecting);
        ASSERT_EQ(callbacks, 1);

        callbacks = 0;
    }

    EXPECT_EQ(callbacks, 0);
}
