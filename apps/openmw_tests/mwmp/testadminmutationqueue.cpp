#include <gtest/gtest.h>

#include <apps/openmw-server/AdminMutationQueue.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <string>
#include <vector>

using namespace std::chrono_literals;

TEST(AdminMutationQueue, DoesNotExecuteUntilMainThreadDrain)
{
    mwmp::AdminMutationQueue queue;
    bool executed = false;
    const auto request = queue.enqueue(mwmp::AdminMutationQueue::Type::ResetCell, "Balmora");
    ASSERT_TRUE(request);
    EXPECT_EQ(queue.pendingCount(), 1u);
    EXPECT_FALSE(executed);

    queue.drain([&](mwmp::AdminMutationQueue::Type type, const std::string& cellId) {
        executed = true;
        EXPECT_EQ(type, mwmp::AdminMutationQueue::Type::ResetCell);
        EXPECT_EQ(cellId, "Balmora");
        mwmp::AdminHttpServer::Response response;
        response.status = 200;
        response.body = "ok";
        return response;
    });

    EXPECT_TRUE(executed);
    EXPECT_EQ(queue.pendingCount(), 0u);
    const auto response = queue.wait(request, 0ms);
    EXPECT_EQ(response.status, 200);
    EXPECT_EQ(response.body, "ok");
}

TEST(AdminMutationQueue, QueuedTimeoutCancelsDestructiveMutationBeforeDrain)
{
    mwmp::AdminMutationQueue queue;
    std::atomic<int> executions = 0;
    const auto request = queue.enqueue(mwmp::AdminMutationQueue::Type::ResetAllCells);
    ASSERT_TRUE(request);

    const auto response = queue.wait(request, 1ms);
    EXPECT_EQ(response.status, 504);
    EXPECT_NE(response.body.find("admin_reset_timeout"), std::string::npos);

    queue.drain([&](mwmp::AdminMutationQueue::Type, const std::string&) {
        ++executions;
        return mwmp::AdminHttpServer::Response{};
    });
    EXPECT_EQ(executions.load(), 0);
}

TEST(AdminMutationQueue, ShutdownCancelsQueuedWaiterAndWakesIt)
{
    mwmp::AdminMutationQueue queue;
    const auto request = queue.enqueue(mwmp::AdminMutationQueue::Type::ResetCell, "Seyda Neen");
    ASSERT_TRUE(request);

    std::promise<void> entered;
    auto enteredFuture = entered.get_future();
    auto waiter = std::async(std::launch::async, [&] {
        entered.set_value();
        return queue.wait(request, 5s);
    });
    ASSERT_EQ(enteredFuture.wait_for(100ms), std::future_status::ready);

    queue.cancelAll();
    ASSERT_EQ(waiter.wait_for(100ms), std::future_status::ready);
    const auto response = waiter.get();
    EXPECT_EQ(response.status, 503);
    EXPECT_NE(response.body.find("server_shutting_down"), std::string::npos);
    EXPECT_FALSE(queue.enqueue(mwmp::AdminMutationQueue::Type::ResetAllCells));
}

TEST(AdminMutationQueue, MultipleMutationsExecuteSeriallyInQueueOrder)
{
    mwmp::AdminMutationQueue queue;
    const auto first = queue.enqueue(mwmp::AdminMutationQueue::Type::ResetCell, "A");
    const auto second = queue.enqueue(mwmp::AdminMutationQueue::Type::ResetCell, "B");
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);

    std::vector<std::string> order;
    queue.drain([&](mwmp::AdminMutationQueue::Type, const std::string& cellId) {
        order.push_back(cellId);
        mwmp::AdminHttpServer::Response response;
        response.status = 200;
        response.body = cellId;
        return response;
    });

    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], "A");
    EXPECT_EQ(order[1], "B");
    EXPECT_EQ(queue.wait(first, 0ms).body, "A");
    EXPECT_EQ(queue.wait(second, 0ms).body, "B");
}
