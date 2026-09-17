#include "PersuasionService.hpp"
#include <components/openmw-mp/Packets/Player/PacketPersuasion.hpp>
#include <limits>

namespace mwmp
{
    PersuasionService::Outcome PersuasionService::execute(const PersuasionRequest& request, std::string_view hash, const Context& c)
    {
        Outcome out;
        auto& r=out.result;
        r.requestId=request.requestId; r.actorId=request.actorId; r.inventoryRevision=c.inventoryRevision;
        r.relationshipRevision=c.relationship.revision; r.baseDisposition=c.relationship.baseDisposition;
        r.currentDisposition=c.currentBase;
        const auto encode=[&] { PacketPersuasionResult packet; packet.result=r; out.encoded=packet.encode(); };
        const auto reject=[&](PersuasionError error, bool journal=true) {
            r.error=error; encode();
            if (journal) {
                CraftRequestRecord row; row.accountId=c.accountId; row.characterId=c.characterId;
                row.requestId=request.requestId; row.requestHash=hash;
                mDb.insertRejectedCraftRequest(row,{reinterpret_cast<const char*>(out.encoded.data()),out.encoded.size()});
            }
            return out;
        };
        if (const auto old=mDb.loadCraftRequest(c.accountId,c.characterId,request.requestId))
        {
            if (old->requestHash!=hash) return reject(PersuasionError::Conflict,false);
            PacketPersuasionResult packet;
            out.encoded.assign(old->resultPayload.begin(),old->resultPayload.end());
            if (!packet.decode(out.encoded)) throw std::runtime_error("Corrupt persuasion journal");
            r=packet.result; out.replayed=true; return out;
        }
        if (!c.player || request.requestId.empty() || request.requestId.size()>128 || hash.empty()
            || static_cast<unsigned>(request.action)>5) return reject(PersuasionError::Invalid);
        if (!c.actorAvailable || !request.actorId || request.actorId!=c.relationship.actorId
            || request.generation!=c.generation) return reject(PersuasionError::Unavailable);
        if (request.inventoryRevision!=c.inventoryRevision || request.relationshipRevision!=c.relationship.revision)
            return reject(PersuasionError::Stale);
        const int bribe=request.action==PersuasionAction::Bribe10 ? 10 : request.action==PersuasionAction::Bribe100 ? 100
            : request.action==PersuasionAction::Bribe1000 ? 1000 : 0;
        std::int64_t gold=0;
        for (const auto& item:c.player->inventoryChanges.items) if (item.refId=="gold_001" && item.count>0) gold+=item.count;
        if (gold<bribe) return reject(PersuasionError::InsufficientGold);
        if (bribe && !c.container) return reject(PersuasionError::Unavailable);
        const auto effect=resolvePersuasion(c.mechanics,static_cast<PersuasionType>(request.action),c.roll(),c.gmst);
        int permanent=effect.permanent;
        if (effect.temporary>0 && permanent>0 && c.relationship.baseDisposition+permanent<0)
            permanent=-c.relationship.baseDisposition;
        r.success=effect.success; r.fight=effect.fight; r.flee=effect.flee;
        r.currentDisposition=c.currentBase+effect.temporary;
        // Only the native permanent component is durable. The current dialogue
        // component lives in this connection and disappears at goodbye/reconnect.
        r.baseDisposition=std::clamp(c.relationship.baseDisposition+permanent,-c.derivedOffset,100-c.derivedOffset);
        r.relationshipRevision++;
        r.chargedGold=effect.success ? bribe : 0;
        out.inventory=c.player->inventoryChanges.items;
        if (r.chargedGold)
        {
            int remaining=r.chargedGold;
            for (auto& item:out.inventory) if (item.refId=="gold_001" && item.count>0) {
                const int amount=std::min(item.count,remaining); item.count-=amount; remaining-=amount;
            }
            std::erase_if(out.inventory,[](const auto& i){return i.count<=0;});
            out.container=c.container;
            ContainerItem added; added.refId="gold_001"; added.count=r.chargedGold;
            auto it=std::find_if(out.container->items.begin(),out.container->items.end(),[](const auto& i){return i.refId=="gold_001" && !i.restocking && i.count>=0;});
            if (it!=out.container->items.end()) {
                if (it->count>std::numeric_limits<int>::max()-r.chargedGold) return reject(PersuasionError::Invalid);
                it->count+=r.chargedGold;
            } else out.container->items.push_back(added);
            r.inventoryRevision++;
        }
        encode();
        DynamicRecordCommit commit;
        commit.accountId=c.accountId; commit.characterId=c.characterId;
        commit.requestId=request.requestId; commit.requestHash=hash;
        commit.expectedInventoryRevision=c.inventoryRevision; commit.resultingInventoryRevision=r.inventoryRevision;
        if (r.chargedGold) commit.inventory=out.inventory;
        commit.relationship=RelationshipMutation{{r.actorId,r.baseDisposition,r.relationshipRevision},c.relationship.revision};
        commit.bribeContainer=out.container;
        commit.resultPayload.assign(out.encoded.begin(),out.encoded.end());
        const auto status=mDb.commitDynamicRecordRequest(commit);
        if (status==DynamicRecordCommitStatus::DuplicateRequest || status==DynamicRecordCommitStatus::DuplicateRequestConflict)
            return execute(request,hash,c);
        if (status!=DynamicRecordCommitStatus::Committed) return reject(PersuasionError::Stale);
        out.committed=true; return out;
    }
}
