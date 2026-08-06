#include "RecordCreationManager.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

#include <components/esm/refid.hpp>
#include <components/openmw-mp/Packets/Records/PacketRecordCreateRequest.hpp>

#include "../../mwbase/environment.hpp"
#include "../../mwbase/world.hpp"
#include "../../mwworld/esmstore.hpp"
#include "../network/Client.hpp"

namespace mwmp
{
    RecordCreationManager::RecordCreationManager(NetworkClient& client)
        : mClient(client)
    {
        // The idempotency journal survives process restarts, so a simple
        // per-process counter would eventually collide with an older request.
        std::random_device random;
        const auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        std::ostringstream prefix;
        prefix << "client-record-" << std::hex << timestamp << '-'
               << static_cast<uint64_t>(random()) << static_cast<uint64_t>(random());
        mRequestPrefix = prefix.str();
    }

    std::string RecordCreationManager::nextRequestId()
    {
        return mRequestPrefix + '-' + std::to_string(mNextRequest++);
    }

    void RecordCreationManager::request(records::RecordCreateRequest request, Completion completion)
    {
        if (request.requestId.empty())
            request.requestId = nextRequestId();
        if (!completion)
            throw std::invalid_argument("A record-create completion callback is required");
        if (mPending.contains(request.requestId))
            throw std::invalid_argument("A record-create request with this ID is already pending");

        request.inventoryRevision = mInventoryRevision;
        const std::string requestId = request.requestId;
        mPending.emplace(requestId, Pending{ std::move(completion), std::nullopt });
        PacketRecordCreateRequest packet;
        packet.request = std::move(request);
        mClient.sendReliable(packet.encode());
    }

    void RecordCreationManager::onResult(records::RecordCreateResult result)
    {
        auto it = mPending.find(result.requestId);
        if (it == mPending.end())
            return;
        mInventoryRevision = std::max(mInventoryRevision, result.inventoryRevision);
        it->second.result = std::move(result);
        mStoreChanged = true;
    }

    void RecordCreationManager::setInventoryRevision(uint64_t revision)
    {
        mInventoryRevision = std::max(mInventoryRevision, revision);
    }

    bool RecordCreationManager::isReady(const records::RecordCreateResult& result) const
    {
        if (!result.accepted)
            return true;
        if (mInventoryRevision < result.inventoryRevision)
            return false;
        MWBase::World* world = MWBase::Environment::get().getWorld();
        if (world == nullptr)
            return false;
        const MWWorld::ESMStore& store = world->getStore();
        for (const records::CreatedRecord& created : result.records)
        {
            if (created.recordId.empty()
                || store.find(ESM::RefId::stringRefId(created.recordId)) == 0)
                return false;
        }
        return true;
    }

    void RecordCreationManager::update()
    {
        if (!mStoreChanged && mPending.empty())
            return;
        mStoreChanged = false;
        std::vector<std::pair<Completion, records::RecordCreateResult>> completed;
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

    void RecordCreationManager::cancelAll(records::CreateError error)
    {
        std::vector<std::pair<Completion, records::RecordCreateResult>> cancelled;
        cancelled.reserve(mPending.size());
        for (auto& [requestId, pending] : mPending)
        {
            records::RecordCreateResult result;
            result.requestId = requestId;
            result.accepted = false;
            result.error = error;
            result.inventoryRevision = mInventoryRevision;
            cancelled.emplace_back(std::move(pending.completion), std::move(result));
        }
        mPending.clear();
        for (auto& [completion, result] : cancelled)
            completion(result);
    }
}
