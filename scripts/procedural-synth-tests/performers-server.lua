-- Disposable QA only: test removal of the leader's generated NPC.
local mp=require('mp')
return {eventHandlers={SynthPerformer_QARemove=function(data)
    local p=mp.getPlayer(data.pid);local actor=mp.getActorByMpNum(data.mpNum or 0)
    if p and p.characterId==1001 and actor and actor.refId:match('^fetcher_synth_bard_1001_') then mp.removeActor(actor.mpNum,actor.cell) end
end}}
