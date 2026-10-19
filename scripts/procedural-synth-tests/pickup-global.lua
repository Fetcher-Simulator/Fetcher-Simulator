local world=require('openmw.world');local types=require('openmw.types')
return {eventHandlers={SynthPickup_QADrop=function(data)
    for _,player in ipairs(world.players) do if types.NPC.record(player).name=='Synth A' then
        for _,item in ipairs(types.Actor.inventory(player):getAll()) do
            if item.recordId=='fetcher_procedural_keyboard_v1' then
                item:teleport(player.cell,data.position,{onGround=true});return
            end
        end
    end end
    error('QA portable keyboard was not in the owner inventory')
end}}
