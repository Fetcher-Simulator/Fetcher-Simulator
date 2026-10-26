local mp=require('mp')
local M={}
function M.handleChat(player,data,env)
    local prefix=env and env.commandPrefix or '/'
    local command=(data.message or ''):match('^(%S+)%s*$')
    if command~=prefix..'voicelines' and command~=prefix..'hotbar' then return nil end
    mp.send(player.guid,'Hotkeys_Message',{version=1,op=command==prefix..'voicelines' and 'voices' or 'items'})
    return false
end
return M
