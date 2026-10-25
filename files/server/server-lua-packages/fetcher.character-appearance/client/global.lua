local world=require('openmw.world')
return {eventHandlers={AppearanceEditor_Message=function(data)
    for _,player in ipairs(world.players) do player:sendEvent('AppearanceEditor_Message',data) end
end}}
