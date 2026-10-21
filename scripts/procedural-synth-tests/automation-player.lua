local core=require('openmw.core');local self=require('openmw.self');local nearby=require('openmw.nearby');local types=require('openmw.types')
local I=require('openmw.interfaces');local synth=require('openmw.synth')
local MIDI=require('scripts.multiplayer.fetcher.procedural-synth.client.midi')
local Format=require('scripts.multiplayer.fetcher.procedural-synth.shared.project')(require('scripts.multiplayer.fetcher.procedural-synth.shared.schema'))
local Sequence=require('scripts.multiplayer.fetcher.procedural-synth.shared.sequence')
local SoundSets=require('scripts.multiplayer.fetcher.procedural-synth.client.soundsets')
local phase,start,at,role=0,nil,0,nil;local flags={}
local function log(s) print('[SynthAutomationTest] '..(role or '?')..' '..s) end
local function entry(l,name) for _,e in ipairs(l.entries) do if e.name==name then return e end end end
local function activate() for _,list in ipairs{nearby.activators,nearby.items} do for _,o in ipairs(list) do
    if o.recordId=='fetcher_procedural_synth_v1' or o.recordId=='fetcher_procedural_keyboard_v1' then o:activateBy(self);return end
end end end
local function run()
    local now=core.getRealTime();start=start or now;role=role or types.NPC.record(self).name
    local api=I.ProceduralSynth;if not api then return end
    if role=='Synth B' then
        local state=api.status()[1]
        if state and state.nativePlaying and state.voices>0 and not flags.audio then
            flags.audio=true;log('PASS late-join observer hears native automation score; strikes='..state.nativeStrikes..' fault='..state.nativeFault)
        end
        return
    end
    if role~='Synth A' or phase==99 then return end
    local w=api.workstation();local l=api.projectLibrary();local state=api.status()[1]
    if phase==0 and now-start>6 then if not w then activate() elseif l.ready and not l.pending then
        api.studio();assert(synth.version>=14)
        local previous=entry(l,'Automation QA curves')
        if previous then assert(l:open(previous.id,false,true));phase=80;at=now
        else
            assert(w:newSong(120,4,4,4));w.project.composition.loop=true
            w.noteLength=8
            for _,s in ipairs{0,8,16,24,32,40,48,56} do assert(w:addNote(1,60+s%12,s)) end
            assert(w:addPart('bass'));for _,s in ipairs{0,16,32,48} do assert(w:addNote(2,36,s)) end
            assert(w:setAutomation(1,'cutoff',0,500,true));assert(w:setAutomation(1,'cutoff',32,15000,true));assert(w:setAutomation(1,'cutoff',63,500,false))
            assert(w:setAutomation(1,'pitchBend',0,0,true));assert(w:setAutomation(1,'pitchBend',32,2,true));assert(w:setAutomation(1,'pitchBend',63,0,false))
            assert(w:setAutomation(1,'program',0,0,false));assert(w:setAutomation(1,'program',32,24,false));assert(w:followPrograms(1,true))
            assert(w:setAutomation(2,'expression',0,.25,true));assert(w:setAutomation(2,'expression',32,1,false))
            assert(w:setAutomation(1,'pedal',0,1,false));assert(w:setAutomation(1,'pedal',48,0,false))
            assert(l:save(true,'Automation QA curves'));phase=1;at=now
        end
    end
    elseif w and l and not l.pending and now-at>1 then at=now
        if phase==1 then
            assert(not w.dirty and w.project.tracks[1].followPrograms);flags.basic=l.active
            assert(w:exportMidi());local files=synth.listMidiExports();local parsed=MIDI.parse(synth.readMidiExport(files[#files]));local part=MIDI.parts(parsed)[1]
            assert(MIDI.automation(parsed,part).pitchBend);log('PASS native SMF bend/program/CC export and parser round trip')
            assert(w:play('song'));phase=2
        elseif phase==2 and state and state.nativePlaying and state.nativeStrikes>0 then
            assert(w:beginAutomationWrite());local history=#w.undoStack
            for i=1,12 do w.beat=i/4;assert(w:recordAutomation(1,'expression',i/12)) end
            w:endAutomationWrite();assert(#w.undoStack==history+1);assert(w:undo());assert(w:redo());w:stop(true)
            assert(l:save(false));log('PASS native score, recorded curve take, Undo/Redo and named save');phase=3
        elseif phase==3 then
            -- Exercise the full saved-curve budget over the real Lua wire and SQLite.
            for _,t in ipairs(w.project.tracks) do t.followPrograms=false;t.automation={} end
            for lane=1,2 do for _,name in ipairs{'cutoff','resonance','expression','modulation'} do
                local spec=Format.automationSpecs[name];local points={}
                for i=0,1023 do points[#points+1]={s=i/16,v=spec.min+(spec.max-spec.min)*(i%16)/16,linear=false} end
                w.project.tracks[lane].automation[name]=points
            end end
            w:touch();assert(l:save(true,'Automation QA budget'));phase=4
        elseif phase==4 then
            assert(not w.dirty);flags.budget=l.active;assert(l:open(flags.budget,false,true));phase=5
        elseif phase==5 then
            local count=0;for _,t in ipairs(w.project.tracks) do for _,points in pairs(t.automation) do count=count+#points end end
            assert(count==8192);local plan=Sequence.compile(w.project,nil,MIDI,'song',Format,SoundSets.programPatch)
            assert(#plan.bytes/11==8192+12);assert(w:play('song'));phase=6
            log('PASS 8192 automation points survive real wire/SQLite reload and compile within bounded native score')
        elseif phase==6 and state and state.nativePlaying then
            w:stop(true);assert(l:open(flags.basic,false,true));phase=7
        elseif phase==7 then
            assert(w.project.tracks[1].followPrograms and #w.project.tracks[1].automation.expression==12);assert(w:play('song'));w.selected=1;w.page='auto';w.revision=w.revision+1
            phase=99;log('PASS full-budget native playback; READY late join and restart checks; Auto page open')
        elseif phase==80 then
            assert(w.project.tracks[1].followPrograms and #w.project.tracks[1].automation.expression==12)
            assert(w:play('song'));w.selected=1;w.page='auto';w.revision=w.revision+1;phase=99
            log('PASS automation/controller project and program policy survive server/client restart; READY Auto UI')
        end
    end
    if now-start>120 then error('automation probe timeout phase '..phase..' / '..(l and l.status or 'no library')) end
end
return {engineHandlers={onFrame=function() if phase==-1 then return end;local ok,err=pcall(run);if not ok then phase=-1;log('FAIL '..tostring(err)) end end}}
