-- Disposable fixture: two instruments, route/mix/SQLite/late-state/missing cleanup.
local core=require('openmw.core');local nearby=require('openmw.nearby');local self=require('openmw.self')
local types=require('openmw.types');local I=require('openmw.interfaces');local mp=require('openmw.mp')
local phase,start,at=0,nil,0;local role;local seen={};local target
local function log(s) print('[SynthBandTest] '..role..' '..s) end
local function output(state,slot) for _,o in ipairs(state.outputs or {}) do if o.slot==slot then return o end end end
local function member(w,mute,level)
    local old=w.project.band[1];return {name=old.name,target=old.target,mute=mute,level=level}
end
local function run()
    local now=core.getRealTime();start=start or now;role=role or types.NPC.record(self).name
    local api=I.ProceduralSynth;if not api then return end
    local states=api.status();local state=states[1];local w=api.workstation()
    if role=='Synth B' then
        if state and state.nativePlaying and output(state,1) and not seen.play then
            local a,b=output(state,0),output(state,1)
            if a.voices>0 and b.voices>0 then
                assert(math.abs(a.position-b.position)<0.06,'observer output phase skew')
                seen.play=true;log('PASS late-started observer has two physical native outputs; phase delta '..math.abs(a.position-b.position))
            end
        end
        if state and state.availability and state.availability[1]=='Missing' and not seen.missing then
            assert(not output(state,1) and state.nativePlaying,'missing member moved or stopped other output')
            seen.missing=true;log('PASS observer missing instrument stays silent; home song continues')
        end
        if seen.missing and #states==0 and phase==0 then phase=10;log('PASS Finish removed every band stream') end
        return
    end
    if role~='Synth A' or phase==10 then return end
    if phase==0 and now-start>6 then
        for _,obj in ipairs(nearby.activators) do if obj.recordId=='fetcher_procedural_synth_v1' then obj:activateBy(self);phase=1;at=now;break end end
    elseif phase==1 and w then
        local choices=w.cb.instruments();assert(#choices>=1,'second physical keyboard unavailable');target=choices[1].target
        w.project.name='Band routing QA';w:add('piano');w:add('piano')
        for n,t in ipairs(w.project.tracks) do t.name='Band part '..n;t.notes={}
            for step=0,12,4 do t.notes[#t.notes+1]={n=n==1 and 48 or 72,s=step,l=3,v=0.8} end
        end
        assert(w:setMember(1,{name='Lead keyboard',target=target,level=1,mute=false}));assert(w:setOutput(2,1));w:touch();api.studio();w.page='band';w.revision=w.revision+1
        phase=2;at=now
    elseif phase==2 and state and state.availability[1]=='Ready' then
        assert(w:play('clips'));phase=3;at=now
    elseif phase==3 and state and state.nativePlaying and now-at>5 then
        local a,b=output(state,0),output(state,1);assert(a and b and a.id~=b.id,'routing has one emitter')
        assert(math.abs(a.position-b.position)<0.06,'owner output phase skew')
        log('PASS owner two output emitters on song clock; phase delta '..math.abs(a.position-b.position))
        assert(w:save());assert(w:setMember(1,member(w,true,0.5)));phase=4;at=now
    elseif phase==4 and state and state.band[1].mute and now-at>3 then
        local a,b=output(state,0),output(state,1);assert(a.playing and b.playing and b.peak<0.001,'mute did not silence native member')
        log('PASS member mute silences its native output without stopping the song');assert(w:setMember(1,member(w,false,0.5)));phase=5;at=now
    elseif phase==5 and now-at>3 then api.loadProject();phase=6;at=now
    elseif phase==6 and w and not w.dirty and now-at>2 then
        assert(w.project.name=='Band routing QA' and w.project.band[1].target.mpNum==target.mpNum and w.project.tracks[2].output.slot==1,'routing persistence mismatch')
        assert(not w.project.band[1].mute and w.project.band[1].level==1,'saved member mix mismatch')
        log('PASS character database round trip retained member identity, output and mix')
        assert(w:play('clips'));phase=7;at=now
    elseif phase==7 and state and state.nativePlaying and now-at>4 then
        mp.sendToServer('SynthBand_QARemove',{id=target.mpNum});phase=8;at=now
    elseif phase==8 and state and state.availability[1]=='Missing' and now-at>3 then
        assert(state.nativePlaying and not output(state,1),'removed member fell back or stopped home transport')
        log('PASS removed bound keyboard remains silent while home keeps playing');api.close();phase=10;log('PASS complete')
    end
    if now-start>100 then error('band probe timeout at phase '..phase) end
end
return {engineHandlers={onFrame=function()
    if phase==-1 then return end
    local ok,err=pcall(run);if not ok then phase=-1;log('FAIL '..tostring(err)) end
end},eventHandlers={Synth_Message=function(data)
    if data.op=='scoreError' or data.op=='error' then log('ERROR '..tostring(data.message)) end
end}}
