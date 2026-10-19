-- Disposable fixture only. Never install this script on a public server.
local mp=require('mp')
return {eventHandlers={SynthBand_QARemove=function(data)
    local player=mp.getPlayer(data.pid);local obj=mp.getObjectByMpNum(data.id or 0)
    if player and player.name=='Synth A' and obj and obj.refId=='fetcher_procedural_keyboard_v1' and obj.cell==player.cell then
        mp.removePlacedObject(obj.mpNum,obj.cell)
    end
end}}
