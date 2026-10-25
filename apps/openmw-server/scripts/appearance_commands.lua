-- Chat entry point. The separate appearance controller still owns all sessions
-- and saves; this only asks the authenticated player's client to toggle its UI.
local mp=require('mp')
local config=require('config')
local M={}
function M.handleChat(player,data,env)
    local prefix=env and env.commandPrefix or config.COMMAND_PREFIX or '/'
    local text=data and data.message or ''
    local command=text:match('^(%S+)')
    if command~=prefix..'appearance' then return nil end
    if not text:sub(#command+1):match('^%s*$') then
        player:sendMessage('Usage: '..prefix..'appearance');return false
    end
    if config.CHARACTER_APPEARANCE_ENABLED==false then
        player:sendMessage('The appearance editor is disabled on this server.');return false
    end
    mp.send(player.guid,'AppearanceEditor_Message',{version=1,op='toggle'})
    return false
end
return M
