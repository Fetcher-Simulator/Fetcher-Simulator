-- Disposable named-project/recovery/native-clock QA. Back up fixture state first.
local core=require('openmw.core');local self=require('openmw.self');local nearby=require('openmw.nearby')
local types=require('openmw.types');local I=require('openmw.interfaces');local mp=require('openmw.mp')
local phase,start,at,role=0,nil,0,nil;local flags={}
local function log(text) print('[SynthLibraryTest] '..(role or '?')..' '..text) end
local function entry(l,name) for _,e in ipairs(l.entries) do if e.name==name then return e end end end
local function activate()
    for _,list in ipairs{nearby.activators,nearby.items} do for _,o in ipairs(list) do
        if o.recordId=='fetcher_procedural_synth_v1' or o.recordId=='fetcher_procedural_keyboard_v1' then o:activateBy(self);return end
    end end
end
local function run()
    local now=core.getRealTime();start=start or now;role=role or types.NPC.record(self).name
    local api=I.ProceduralSynth;if not api then return end
    if role=='Synth B' then
        local state=api.status()[1]
        if state and not flags.at then
            flags.at=now;mp.sendToServer('Synth_Request',{version=1,op='libList',id=state.id,epoch=state.epoch,seq=999999,requestId=777})
        end
        if flags.at and now-flags.at>4 and not flags.private then assert(not flags.received,'observer received owner library');flags.private=true;log('PASS observer cannot read owner project library') end
        if state and state.nativePlaying and state.voices>0 and not flags.audio then flags.audio=true;log('PASS native playback audible while owner saves/recoveries') end
        return
    end
    if role~='Synth A' or phase==99 then return end
    local w=api.workstation();local l=api.projectLibrary()
    if phase==0 and now-start>6 then if not w then activate() elseif l.ready and not l.pending then
        api.studio()
        local previous=entry(l,'Library QA recovered')
        if previous then flags.reloaded=previous.id;assert(l:open(previous.id,false,true));phase=80;at=now
        else assert(w:newSong(120,4,4,8));w.project.name='Library QA first';assert(w:addNote(1,60,0));assert(l:save(false));phase=1;at=now end
    end
    elseif w and l and not l.pending and now-at>.8 then
        at=now
        if phase==1 then assert(not w.dirty and l.active>0);flags.first=l.active;assert(l:save(true,'Library QA copy'));phase=2
        elseif phase==2 then assert(l.active~=flags.first and not w.dirty);flags.copy=l.active;assert(l:rename(assert(entry(l,'Library QA copy')),'Library QA renamed'));phase=3
        elseif phase==3 then
            assert(w.project.name=='Library QA renamed');assert(w:addNote(1,64,4));assert(w:play('song'))
            l.lastAuto=now-31;l:frame(now);assert(l.pending and l.pending.op=='libSnapshot');phase=4
        elseif phase==4 then assert(w.dirty and l.autosaved==w.generation);assert(l:list());phase=5
        elseif phase==5 then
            assert(#l.recoveries>0);flags.recovery=l.recoveries[1].id
            assert(l:open(flags.first,false)==false and l.action);assert(l:snapshot(true));phase=6
        elseif phase==6 then
            if l.active==flags.first then assert(#w:noteData(1)==1 and not w.dirty);assert(l:open(flags.recovery,true));phase=7 end
        elseif phase==7 then assert(l.active==0 and w.dirty and #w:noteData(1)==2);assert(l:save(true,'Library QA recovered'));phase=8
        elseif phase==8 then assert(not w.dirty);assert(l:delete(assert(entry(l,'Library QA renamed'))));phase=9
        elseif phase==9 then
            assert(not entry(l,'Library QA renamed') and entry(l,'Library QA first') and entry(l,'Library QA recovered'))
            assert(w:play('song'));w.page='projects';w.revision=w.revision+1;phase=99
            log('PASS named Save/Save As/rename/delete, separate autosave, guarded Open and recovery copy; READY restart persistence check')
        elseif phase==80 then
            assert(l.active==flags.reloaded and not w.dirty and #w:noteData(1)==2 and #l.recoveries>0)
            w.page='projects';w.revision=w.revision+1;phase=99;log('PASS named project and recovery survive server/client restart; READY Projects UI inspection')
        end
    end
    if now-start>90 then error('library probe timeout phase '..phase..' / '..(l and l.status or 'no session')) end
end
return {engineHandlers={onFrame=function() if phase==-1 then return end;local ok,err=pcall(run);if not ok then phase=-1;log('FAIL '..tostring(err)) end end},
    eventHandlers={Synth_Message=function(data) if role=='Synth B' and data.op=='libResult' and data.requestId==777 then flags.received=true end end}}
