#include "EnchantingCreationManager.hpp"

#include <chrono>
#include <iomanip>
#include <random>
#include <sstream>
#include <utility>
#include <vector>

#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadbook.hpp>
#include <components/esm3/loadclot.hpp>
#include <components/esm3/loadench.hpp>
#include <components/esm3/loadweap.hpp>
#include <components/openmw-mp/Packets/Records/PacketEnchantingRequest.hpp>

#include "../../mwbase/environment.hpp"
#include "../../mwbase/world.hpp"
#include "../../mwworld/esmstore.hpp"
#include "../network/Client.hpp"
#include "../records/RecordCreationManager.hpp"

namespace mwmp
{
    EnchantingCreationManager::EnchantingCreationManager(
        NetworkClient& client, RecordCreationManager& recordCreationManager)
        : mClient(client)
        , mRecordCreationManager(recordCreationManager)
    {
        // The idempotency journal survives process restarts, so a simple
        // per-process counter would eventually collide with an older request.
        std::random_device random;
        const auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        std::ostringstream prefix;
        prefix << "client-enchanting-" << std::hex << timestamp << '-'
               << static_cast<uint64_t>(random()) << static_cast<uint64_t>(random());
        mRequestPrefix = prefix.str();
    }

    std::string EnchantingCreationManager::nextRequestId()
    {
        return mRequestPrefix + '-' + std::to_string(mNextRequest++);
    }

    bool EnchantingCreationManager::request(
        records::EnchantingRequest request, Completion completion, std::string& error)
    {
        if (!completion)
            throw std::invalid_argument("An enchanting completion callback is required");
        if (!mPending.empty())
        {
            error = "Another enchanting request is still pending.";
            return false;
        }
        if (request.requestId.empty())
            request.requestId = nextRequestId();

        request.inventoryRevision = mRecordCreationManager.inventoryRevision();
        const std::string requestId = request.requestId;
        mPending.emplace(requestId, Pending{ std::move(completion), std::nullopt });
        PacketEnchantingRequest packet;
        packet.request = std::move(request);
        mClient.sendReliable(packet.encode());
        return true;
    }

    void EnchantingCreationManager::onResult(records::EnchantingResult result)
    {
        auto it = mPending.find(result.requestId);
        if (it == mPending.end())
            return;
        it->second.result = std::move(result);
    }

    bool EnchantingCreationManager::isReady(const records::EnchantingResult& result) const
    {
        if (!result.accepted || !result.success)
            return true;
        // The authoritative inventory (including the new revision) must be
        // visible before the completion fires.
        if (mRecordCreationManager.inventoryRevision() < result.inventoryRevision)
            return false;
        MWBase::World* world = MWBase::Environment::get().getWorld();
        if (world == nullptr)
            return false;
        const MWWorld::ESMStore& store = world->getStore();

        // The owning item must resolve locally and its enchantment reference
        // must point at the returned canonical enchantment id.
        const ESM::RefId enchantmentRefId = ESM::RefId::stringRefId(result.enchantmentRecordId);
        bool itemResolved = false;
        if (const ESM::Weapon* weapon = store.get<ESM::Weapon>().search(ESM::RefId::stringRefId(result.itemRecordId)))
            itemResolved = weapon->mEnchant == enchantmentRefId;
        else if (const ESM::Armor* armor = store.get<ESM::Armor>().search(ESM::RefId::stringRefId(result.itemRecordId)))
            itemResolved = armor->mEnchant == enchantmentRefId;
        else if (const ESM::Clothing* clothing
            = store.get<ESM::Clothing>().search(ESM::RefId::stringRefId(result.itemRecordId)))
            itemResolved = clothing->mEnchant == enchantmentRefId;
        else if (const ESM::Book* book = store.get<ESM::Book>().search(ESM::RefId::stringRefId(result.itemRecordId)))
            itemResolved = book->mEnchant == enchantmentRefId;
        if (!itemResolved)
            return false;

        // The enchantment itself must resolve locally.
        if (store.get<ESM::Enchantment>().search(enchantmentRefId) == nullptr)
            return false;

        return true;
    }

    void EnchantingCreationManager::update()
    {
        if (mPending.empty())
            return;
        std::vector<std::pair<Completion, records::EnchantingResult>> completed;
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

    void EnchantingCreationManager::cancelAll(records::EnchantingError error)
    {
        std::vector<std::pair<Completion, records::EnchantingResult>> cancelled;
        cancelled.reserve(mPending.size());
        for (auto& [requestId, pending] : mPending)
        {
            records::EnchantingResult result;
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
