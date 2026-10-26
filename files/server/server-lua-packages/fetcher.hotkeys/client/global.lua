local world=require('openmw.world')
return {eventHandlers={Hotkeys_Message=function(data)
    for _,player in ipairs(world.players) do player:sendEvent('Hotkeys_Message',data) end
end}}
