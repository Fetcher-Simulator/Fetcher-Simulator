-- Disposable fixture only: this probe changes inventory/world state and a project.
local core=require('openmw.core');local nearby=require('openmw.nearby');local self=require('openmw.self')
local types=require('openmw.types');local I=require('openmw.interfaces');local mp=require('openmw.mp')
local phase,start,at,position=0,nil,0,nil
local seen={};local role
local function log(s) print('[SynthPickupTest] '..role..' '..s) end
local function keyboards()
    local items={}
    for _,item in ipairs(types.Actor.inventory(self):getAll()) do
        if item.recordId=='fetcher_procedural_keyboard_v1' then items[#items+1]=item end
    end
    return items
end
local function run()
    local now=core.getRealTime();start=start or now;role=role or types.NPC.record(self).name
    local api=I.ProceduralSynth;if not api then return end
    local states=api.status();local state=states[1];local w=api.workstation()
    if role=='Synth B' then
        if state and state.nativePlaying and state.voices>0 and not seen[state.id] then
            seen[state.id]=true;log('PASS observed native playback at keyboard '..state.id)
        end
        local count=0;for _ in pairs(seen) do count=count+1 end
        if count>=2 and #states==0 and phase==0 then phase=10;log('PASS both physical keyboard instances stopped and disappeared after pickup') end
        return
    end
    if role~='Synth A' or phase==10 then return end
    if phase==0 and now-start>6 then
        assert(#keyboards()==0,'probe expects no portable keyboard in inventory')
        for _,obj in ipairs(nearby.activators) do if obj.recordId=='fetcher_procedural_synth_v1' then
            position=obj.position;obj:activateBy(self);phase=1;at=now;break
        end end
    elseif phase==1 and w then
        w.project.name='Portable keyboard QA';w:add('piano','b:studio-keys','Pickup notes',60)
        w.project.tracks[1].notes={{n=60,s=0,l=2,v=0.8}};w:touch();api.studio()
        assert(w:play('clips'));phase=2;at=now
    elseif phase==2 and state and state.nativePlaying and now-at>3 then
        assert(api.pickupKeyboard());phase=3;at=now;log('requested pickup after native playback')
    elseif phase==3 and #keyboards()==1 and not w then
        assert(keyboards()[1].count==1,'pickup duplicated the item')
        local record=types.Miscellaneous.record(keyboards()[1]);assert(record.icon:lower():find('keyboard.dds',1,true),'missing keyboard inventory icon')
        log('PASS legacy activator -> exactly one inventory keyboard; custom icon; editor closed')
        core.sendGlobalEvent('SynthPickup_QADrop',{position=position});phase=4;at=now
    elseif phase==4 then
        for _,obj in ipairs(nearby.items) do if obj.recordId=='fetcher_procedural_keyboard_v1' and (mp.getObjectMpNum(obj) or 0)>0 then
            assert(#keyboards()==0,'placement retained an inventory copy');obj:activateBy(self);phase=5;at=now;break
        end end
    elseif phase==5 and w then api.loadProject();phase=6;at=now
    elseif phase==6 and w and w.project.name=='Portable keyboard QA' then
        assert(#w.project.tracks==1 and #w.project.tracks[1].notes==1,'pickup did not preserve the project')
        log('PASS inventory -> placed keyboard -> activate -> reload saved notes')
        assert(w:play('clips'));phase=7;at=now
    elseif phase==7 and state and state.nativePlaying and now-at>3 then
        assert(api.pickupKeyboard());phase=8;at=now
    elseif phase==8 and #keyboards()==1 and not w then
        log('PASS placed portable item picked up again without duplication; native playback cleaned up')
        phase=10
    end
    if now-start>80 then error('pickup probe timed out at phase '..phase) end
end
return {engineHandlers={onFrame=function()
    if phase==-1 then return end
    local ok,err=pcall(run);if not ok then phase=-1;log('FAIL '..tostring(err)) end
end},eventHandlers={Synth_Message=function(data)
    if data.pickupFailed then phase=-1;log('FAIL server pickup: '..tostring(data.message)) end
end}}
