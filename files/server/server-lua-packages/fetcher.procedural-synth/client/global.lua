local world = require('openmw.world')
local mp = require('openmw.mp')
local I = require('openmw.interfaces')
local function activate(object, actor)
    if (object.recordId ~= 'fetcher_procedural_synth_v1' and object.recordId ~= 'fetcher_procedural_keyboard_v1') or not mp.isConnected() then return end
    for _, player in ipairs(world.players) do
        if actor == player then player:sendEvent('Synth_Activate', {id=mp.getObjectMpNum(object)}) end
    end
    return false -- A placed inventory keyboard opens Studio instead of ActionTake.
end
I.Activation.addGlobalHandler(activate)
return { eventHandlers = { Synth_Message = function(data)
    for _, player in ipairs(world.players) do player:sendEvent('Synth_Message', data) end
end } }
