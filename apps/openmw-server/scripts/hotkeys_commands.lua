local mp=require('mp')
local M={}
function M.handleChat(player,data,env)
    local prefix=env and env.commandPrefix or '/'
    local command=(data.message or ''):match('^(%S+)%s*$')
    if command~=prefix..'voicelines' then return nil end
    mp.send(player.guid,'Hotkeys_Message',{version=1,op='voices'})
    return false
end
return M
