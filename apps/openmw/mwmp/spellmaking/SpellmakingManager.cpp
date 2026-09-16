#include "SpellmakingManager.hpp"
#include "../../mwbase/environment.hpp"
#include "../../mwbase/world.hpp"
#include "../../mwmechanics/creaturestats.hpp"
#include "../../mwworld/class.hpp"
#include "../network/Client.hpp"
#include "../records/RecordCreationManager.hpp"
#include "../sync/PlayerSync.hpp"
#include <components/openmw-mp/Packets/Records/PacketSpellmakingRequest.hpp>

namespace mwmp
{
    SpellmakingManager::SpellmakingManager(NetworkClient& client, RecordCreationManager& records, PlayerSync& player)
        : mClient(client)
        , mRecords(records)
        , mPlayer(player)
    {
    }

    bool SpellmakingManager::request(records::SpellmakingRequest request, Completion completion, std::string& error)
    {
        if (request.name.empty() || request.name.size() > 64 || request.effects.empty()
            || request.effects.size() > records::MaxSpellmakingEffects)
        {
            error = "Choose a spell name of at most 64 bytes and one to eight effects.";
            return false;
        }
        if (mPending || !mPlayer.spellbookReady())
        {
            error = "Please wait for your spellbook to finish updating.";
            return false;
        }
        request.requestId = "spellmaking-" + mRecords.nextRequestId();
        request.inventoryRevision = mRecords.inventoryRevision();
        request.spellbookRevision = mPlayer.spellbookRevision();
        mPending = Pending{ std::move(request), std::move(completion), std::nullopt, std::chrono::steady_clock::now() };
        PacketSpellmakingRequest packet;
        packet.request = mPending->request;
        mClient.sendReliable(packet.encode());
        return true;
    }
    void SpellmakingManager::onResult(records::SpellmakingResult result)
    {
        if (mPending && mPending->request.requestId == result.requestId)
        {
            // Only indeterminate outcomes retry. Durable rejections (including
            // terminal server errors) must release the UI instead of retrying forever.
            if (result.error != records::SpellmakingError::RequestPending)
                mPending->result = std::move(result);
        }
    }
    bool SpellmakingManager::ready(const records::SpellmakingResult& result) const
    {
        if (!result.accepted)
            return true;
        if (mRecords.inventoryRevision() < result.inventoryRevision || !mPlayer.inventoryReady() || !mPlayer.spellbookReady()
            || mPlayer.spellbookRevision() < result.spellbookRevision)
            return false;
        MWBase::World* world = MWBase::Environment::get().getWorld();
        if (!world)
            return false;
        const auto player = world->getPlayerPtr();
        return !player.isEmpty()
            && player.getClass().getCreatureStats(player).getSpells().hasSpell(
                ESM::RefId::stringRefId(result.recordId));
    }
    void SpellmakingManager::update()
    {
        if (!mPending)
            return;
        if (mPending->result && ready(*mPending->result))
        {
            auto pending = std::move(*mPending);
            mPending.reset();
            pending.completion(*pending.result);
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now - mPending->lastSend >= std::chrono::seconds(5))
        {
            PacketSpellmakingRequest packet;
            packet.request = mPending->request;
            mClient.sendReliable(packet.encode());
            mPending->lastSend = now;
        }
    }
    void SpellmakingManager::cancelAll()
    {
        if (!mPending)
            return;
        auto pending = std::move(*mPending);
        mPending.reset();
        records::SpellmakingResult result;
        result.requestId = pending.request.requestId;
        result.error = records::SpellmakingError::ServerError;
        pending.completion(result);
    }
}
