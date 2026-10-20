-- Opt-in isolated fixture supply, not a public-server grant rule.
local mp=require('mp');local seen={};local last=-10
return {engineHandlers={onUpdate=function()
    local now=mp.getUptime();if now-last<1 then return end;last=now
    for _,p in ipairs(mp.getPlayers()) do if (p.characterId==1001 or p.characterId==1002) and not seen[p.guid] then
        seen[p.guid]=true
        mp.ensureInventoryItem(p.guid,'misc_de_lute_01');mp.ensureInventoryItem(p.guid,'misc_de_drum_01')
    end end
end}}
