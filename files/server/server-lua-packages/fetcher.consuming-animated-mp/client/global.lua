local mp = require('openmw.mp')
local world = require('openmw.world')
local types = require('openmw.types')
local resources = require('scripts.multiplayer.fetcher.consuming-animated-mp.client.resources')
local receiver = 'scripts/multiplayer/fetcher/consuming-animated-mp/client/receiver.lua'
return { eventHandlers = { PotionAnim_Presentation = function(data)
    if not resources.available or not mp.isConnected() or type(data) ~= 'table' or data.version ~= 1 then return end
    for _, actor in ipairs(world.activeActors) do
        local matches = data.actorId and mp.getActorInstanceId(actor) == data.actorId
            or not data.actorId and actor.recordId == 'mp_remote_' .. tostring(data.sourceGuid)
        if matches and types.NPC.objectIsInstance(actor) and not types.Player.objectIsInstance(actor)
            and not types.Actor.isDead(actor) then
            if not actor:hasScript(receiver) then
                if data.op ~= 'start' then return end
                actor:addScript(receiver)
            end
            actor:sendEvent('ConsumingAnimatedMP_Play', data)
            return
        end
    end
end } }
