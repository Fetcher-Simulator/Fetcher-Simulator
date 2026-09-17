#include "RelationshipManager.hpp"
#include "../Main.hpp"
#include "../network/Client.hpp"
#include "../records/RecordCreationManager.hpp"
#include "../sync/ActorSync.hpp"
#include "../sync/PlayerSync.hpp"
#include "../../mwbase/environment.hpp"
#include "../../mwbase/world.hpp"
#include "../../mwworld/class.hpp"
#include "../../mwworld/esmstore.hpp"
#include "../../mwmechanics/npcstats.hpp"
#include <components/esm3/loadgmst.hpp>
#include <components/openmw-mp/ServicePricing.hpp>
#include <components/openmw-mp/Packets/Player/PacketPersuasion.hpp>

namespace mwmp
{
    std::uint64_t RelationshipManager::actorId(const MWWorld::Ptr& actor) const
    {
        if(actor.isEmpty()) return 0;
        return Main::get().getActorSync().actorNetIdForPtr(std::string(MWBase::Environment::get().getWorld()->getCellName()),actor);
    }
    bool RelationshipManager::enqueue(const MWWorld::Ptr& actor,PersuasionAction action,Completion callback)
    {
        PersuasionRequest request;
        request.actorId=actorId(actor);
        request.generation=Main::get().getActorSync().actorMigrationGenerationForPtr(std::string(MWBase::Environment::get().getWorld()->getCellName()),actor);
        if(!request.actorId || !request.generation || mPending.size()>8) return false;
        request.requestId="persuasion-"+Main::get().getRecordCreationManager().nextRequestId(); request.action=action;
        mPending.push_back({std::move(request),std::move(callback),{}, {},false}); return true;
    }
    void RelationshipManager::begin(const MWWorld::Ptr& actor)
    {
        if(!actor.getClass().isNpc()) return;
        if(mActor!=actor) close();
        mActor=actor; enqueue(actor,PersuasionAction::Read);
    }
    void RelationshipManager::close()
    {
        if(!mActor.isEmpty()) enqueue(mActor,PersuasionAction::Close);
        mActor=MWWorld::Ptr();
    }
    bool RelationshipManager::persuade(const MWWorld::Ptr& actor,int type,Completion callback)
    {
        if(type<0 || type>5 || mPending.size()>8) return false;
        return enqueue(actor,static_cast<PersuasionAction>(type),std::move(callback));
    }
    int RelationshipManager::baseDisposition(const MWWorld::Ptr& actor,bool temporary) const
    {
        if(actor.isEmpty() || !actor.getClass().isNpc()) return 0;
        const auto it=mStates.find(actorId(actor));
        if(it==mStates.end()) return actor.get<ESM::NPC>()->mBase->mNpdt.mDisposition;
        return temporary && actor==mActor ? it->second.currentDisposition : it->second.baseDisposition;
    }
    Crafting::EnchantingBarterInput RelationshipManager::pricing(const MWWorld::Ptr& actor) const
    {
        const auto& store=*MWBase::Environment::get().getESMStore();
        const auto* npc=store.get<ESM::NPC>().search(actor.getCellRef().getRefId());
        const auto& fatigue=actor.getClass().getCreatureStats(actor).getFatigue();
        DynamicStats stats; stats.fatigue.base=fatigue.getBase(); stats.fatigue.mod=fatigue.getModifier(); stats.fatigue.current=fatigue.getCurrent();
        return serviceBarterInput(npc,Main::get().getPlayerSync().localPlayer(),&stats,
            [&](std::string_view id){return store.get<ESM::GameSetting>().find(id)->mValue.getFloat();},baseDisposition(actor));
    }
    void RelationshipManager::onResult(const PersuasionResult& result)
    {
        if(result.error==PersuasionError::None) {
            const auto it=mStates.find(result.actorId);
            if(it==mStates.end() || result.relationshipRevision>=it->second.relationshipRevision)
                mStates[result.actorId]=result;
            if(!mActor.isEmpty() && actorId(mActor)==result.actorId && mActor.getClass().isNpc())
                mActor.getClass().getNpcStats(mActor).setBaseDisposition(result.currentDisposition);
        }
        if(!mPending.empty() && mPending.front().request.requestId==result.requestId && result.error!=PersuasionError::Pending)
            mPending.front().result=result;
    }
    void RelationshipManager::update()
    {
        if(mPending.empty()) return;
        auto& p=mPending.front(); auto& main=Main::get();
        if(p.result && (p.result->error!=PersuasionError::None ||
            (main.getPlayerSync().inventoryReady() && main.getRecordCreationManager().inventoryRevision()>=p.result->inventoryRevision))) {
            auto done=std::move(p); mPending.pop_front(); if(done.completion) done.completion(*done.result); return;
        }
        const auto now=std::chrono::steady_clock::now();
        if(!p.sent || now-p.lastSend>=std::chrono::seconds(5)) {
            if(!p.sent) {
                if(!main.getPlayerSync().inventoryReady()) return;
                p.request.inventoryRevision=main.getRecordCreationManager().inventoryRevision();
                const auto it=mStates.find(p.request.actorId);
                p.request.relationshipRevision=it==mStates.end() ? 0 : it->second.relationshipRevision;
            }
            PacketPersuasionRequest packet; packet.request=p.request;
            main.getNetworking().sendReliable(packet.encode()); p.sent=true; p.lastSend=now;
        }
    }
    void RelationshipManager::clear() { mPending.clear(); mStates.clear(); mActor=MWWorld::Ptr(); }
}
