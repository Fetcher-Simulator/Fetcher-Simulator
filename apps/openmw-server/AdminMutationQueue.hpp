#ifndef OPENMW_SERVER_ADMINMUTATIONQUEUE_HPP
#define OPENMW_SERVER_ADMINMUTATIONQUEUE_HPP

#include "AdminHttpServer.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace mwmp
{
    class AdminMutationQueue
    {
    public:
        enum class Type : std::uint8_t
        {
            ResetCell,
            ResetAllCells,
        };

        enum class State : std::uint8_t
        {
            Queued,
            Running,
            Completed,
            Cancelled,
        };

        struct Request
        {
            Type type = Type::ResetCell;
            std::string cellId;
            State state = State::Queued;
            AdminHttpServer::Response response;
            std::mutex mutex;
            std::condition_variable condition;
        };

        using RequestPtr = std::shared_ptr<Request>;
        using Executor = std::function<AdminHttpServer::Response(Type, const std::string&)>;

        RequestPtr enqueue(Type type, std::string cellId = {})
        {
            std::lock_guard lock(mMutex);
            if (!mAccepting)
                return {};
            auto request = std::make_shared<Request>();
            request->type = type;
            request->cellId = std::move(cellId);
            mPending.push_back(request);
            return request;
        }

        AdminHttpServer::Response wait(const RequestPtr& request, std::chrono::milliseconds timeout)
        {
            if (!request)
                return cancelledResponse();

            std::unique_lock lock(request->mutex);
            const auto terminal = [&] {
                return request->state == State::Completed || request->state == State::Cancelled;
            };
            if (!request->condition.wait_for(lock, timeout, terminal))
            {
                if (request->state == State::Queued)
                {
                    request->state = State::Cancelled;
                    request->response.status = 504;
                    request->response.body = "{\"ok\":false,\"error\":\"admin_reset_timeout\"}";
                    request->condition.notify_all();
                    return request->response;
                }

                // A running destructive request cannot truthfully return a
                // timeout failure: it may already have committed. Preserve
                // synchronous semantics and wait for its actual result.
                request->condition.wait(lock, terminal);
            }
            return request->response;
        }

        void drain(const Executor& executor)
        {
            std::vector<RequestPtr> pending;
            {
                std::lock_guard lock(mMutex);
                pending.swap(mPending);
            }

            for (const RequestPtr& request : pending)
            {
                {
                    std::lock_guard lock(request->mutex);
                    if (request->state != State::Queued)
                        continue;
                    request->state = State::Running;
                }

                AdminHttpServer::Response response;
                try
                {
                    response = executor(request->type, request->cellId);
                }
                catch (...)
                {
                    response.status = 500;
                    response.body = "{\"ok\":false,\"error\":\"admin_mutation_failed\"}";
                }

                {
                    std::lock_guard lock(request->mutex);
                    request->response = std::move(response);
                    request->state = State::Completed;
                }
                request->condition.notify_all();
            }
        }

        void cancelAll()
        {
            std::vector<RequestPtr> pending;
            {
                std::lock_guard lock(mMutex);
                mAccepting = false;
                pending.swap(mPending);
            }
            for (const RequestPtr& request : pending)
            {
                {
                    std::lock_guard lock(request->mutex);
                    if (request->state != State::Queued)
                        continue;
                    request->state = State::Cancelled;
                    request->response = cancelledResponse();
                }
                request->condition.notify_all();
            }
        }

        std::size_t pendingCount() const
        {
            std::lock_guard lock(mMutex);
            return mPending.size();
        }

    private:
        static AdminHttpServer::Response cancelledResponse()
        {
            AdminHttpServer::Response response;
            response.status = 503;
            response.body = "{\"ok\":false,\"error\":\"server_shutting_down\"}";
            return response;
        }

        mutable std::mutex mMutex;
        std::vector<RequestPtr> mPending;
        bool mAccepting = true;
    };
}

#endif
