#include "AlchemyCreationManager.hpp"

#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>
#include <utility>
#include <vector>

#include <components/openmw-mp/Packets/Records/PacketAlchemyRequest.hpp>

#include "../../mwbase/environment.hpp"
#include "../../mwbase/world.hpp"
#include "../../mwworld/esmstore.hpp"
#include "../network/Client.hpp"
#include "../records/RecordCreationManager.hpp"

namespace mwmp
{
    AlchemyCreationManager::AlchemyCreationManager(NetworkClient& client, RecordCreationManager& recordCreationManager)
        : mClient(client)
        , mRecordCreationManager(recordCreationManager)
    {
        // The idempotency journal survives process restarts, so a simple
        // per-process counter would eventually collide with an older request.
        std::random_device random;
        const auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        std::ostringstream prefix;
        prefix << "client-alchemy-" << std::hex << timestamp << '-'
               << static_cast<uint64_t>(random()) << static_cast<uint64_t>(random());
        mRequestPrefix = prefix.str();
    }

    std::string AlchemyCreationManager::nextRequestId()
    {
        return mRequestPrefix + '-' + std::to_string(mNextRequest++);
    }

    bool AlchemyCreationManager::request(
        records::AlchemyRequest request, Completion completion, std::string& error)
    {
        if (!completion)
            throw std::invalid_argument("An alchemy completion callback is required");
        if (!mPending.empty())
        {
            error = "Another alchemy request is still pending.";
            return false;
        }
        if (request.requestId.empty())
            request.requestId = nextRequestId();

        request.inventoryRevision = mRecordCreationManager.inventoryRevision();
        const std::string requestId = request.requestId;
        mPending.emplace(requestId, Pending{ std::move(completion), std::nullopt });
        PacketAlchemyRequest packet;
        packet.request = std::move(request);
        mClient.sendReliable(packet.encode());
        return true;
    }

    void AlchemyCreationManager::onResult(records::AlchemyResult result)
    {
        auto it = mPending.find(result.requestId);
        if (it == mPending.end())
            return;
        it->second.result = std::move(result);
    }

    bool AlchemyCreationManager::isReady(const records::AlchemyResult& result) const
    {
        if (!result.accepted)
            return true;
        // The authoritative inventory (including the new revision) must be
        // visible before the completion fires.
        if (mRecordCreationManager.inventoryRevision() < result.inventoryRevision)
            return false;
        // Every canonical potion record referenced by the result must resolve
        // in the local ESMStore.
        MWBase::World* world = MWBase::Environment::get().getWorld();
        if (world == nullptr)
            return false;
        const MWWorld::ESMStore& store = world->getStore();
        for (const records::AlchemyAttemptResult& attempt : result.attempts)
        {
            if (!attempt.success)
                continue;
            if (attempt.recordId.empty()
                || store.find(ESM::RefId::stringRefId(attempt.recordId)) == 0)
                return false;
        }
        return true;
    }

    void AlchemyCreationManager::update()
    {
        if (mPending.empty())
            return;
        std::vector<std::pair<Completion, records::AlchemyResult>> completed;
        for (auto it = mPending.begin(); it != mPending.end();)
        {
            if (!it->second.result || !isReady(*it->second.result))
            {
                ++it;
                continue;
            }
            completed.emplace_back(std::move(it->second.completion), std::move(*it->second.result));
            it = mPending.erase(it);
        }
        for (auto& [completion, result] : completed)
            completion(result);
    }

    void AlchemyCreationManager::cancelAll(records::AlchemyError error)
    {
        std::vector<std::pair<Completion, records::AlchemyResult>> cancelled;
        cancelled.reserve(mPending.size());
        for (auto& [requestId, pending] : mPending)
        {
            records::AlchemyResult result;
            result.requestId = requestId;
            result.accepted = false;
            result.error = error;
            cancelled.emplace_back(std::move(pending.completion), std::move(result));
        }
        mPending.clear();
        for (auto& [completion, result] : cancelled)
            completion(result);
    }
}
