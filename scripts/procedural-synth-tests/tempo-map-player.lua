local core=require('openmw.core');local self=require('openmw.self');local nearby=require('openmw.nearby');local types=require('openmw.types')
local I=require('openmw.interfaces');local synth=require('openmw.synth')
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local MIDI=require(prefix..'client.midi');local Format=require(prefix..'shared.project')(require(prefix..'shared.schema'))
local Export=require(prefix..'client.midi-export');local Sequence=require(prefix..'shared.sequence');local SoundSets=require(prefix..'client.soundsets')
local phase,start,at,role=0,nil,0,nil;local flags={}
local function log(s) print('[SynthTempoMapTest] '..(role or '?')..' '..s) end
local function activate() for _,list in ipairs{nearby.activators,nearby.items} do for _,o in ipairs(list) do
    if o.recordId=='fetcher_procedural_synth_v1' or o.recordId=='fetcher_procedural_keyboard_v1' then o:activateBy(self);return end
end end end
local function verify(w)
    local p=w.project;assert(p.timeline.beats==28 and #p.timeline.tempos==3 and #p.timeline.meters==2)
    assert(Format.barStart(p,4)==11 and Format.barStart(p,6)==17.5 and p.tracks[1].automation.expression)
    assert(p.tracks[2].arrangement and #p.tracks[2].arrangement.regions==1)
    local plan=Sequence.compile(p,nil,MIDI,'song',Format,SoundSets.programPatch)
    local ok,frames=Sequence.validateBarGrid(plan.barGrid,plan.length);assert(ok and plan.length==984000 and frames[4]==336000)
end
local function run()
    local now=core.getRealTime();start=start or now;role=role or types.NPC.record(self).name
    local api=I.ProceduralSynth;if not api then return end
    if role=='Synth B' then
        local s=api.status()[1]
        if s and s.nativePlaying and s.voices>0 and not flags.audio then
            flags.audio=true;assert(s.nativeFault==0);log('PASS observer hears native tempo/meter song; strikes='..s.nativeStrikes..' fault='..s.nativeFault)
        end
        return
    end
    if role~='Synth A' or phase==99 then return end
    local w=api.workstation();local l=api.projectLibrary();local state=api.status()[1]
    if phase==0 and now-start>6 then
        if not w then activate() elseif l.ready and not l.pending then
            api.studio();local previous
            for _,e in ipairs(l.entries) do if e.name=='Tempo map QA' then previous=e end end
            if previous then assert(l:open(previous.id,false,true));phase=80;at=now else
                assert(w:newSong(120,4,4,8));assert(w:setComposition{loop=true})
                assert(w:editTempo(8,60) and w:editTempo(14,80))
                assert(w:editMeter(8,3,4) and w:editMeter(14,7,8))
                w.noteLength=2
                for step=0,108,4 do assert(w:addNote(1,60+step%12,step)) end
                assert(w:setAutomation(1,'expression',0,.3,true) and w:setAutomation(1,'expression',112,1,false))
                assert(w:addPart('bass'));assert(w:addNote(2,36,0));w:selectAllNotes(2);assert(w:captureClip(2))
                verify(w);assert(l:save(true,'Tempo map QA'));phase=1;at=now
            end
        end
    elseif w and l and not l.pending and now-at>1 then
        if phase==1 then
            assert(not w.dirty);flags.id=l.active
            local bytes=Export.encode(w.project,nil,MIDI,Format);local result=synth.exportMidi('Tempo map QA',bytes)
            local parsed=MIDI.parse(synth.readMidiExport(result.name));assert(#parsed.tempos==3 and #parsed.meters==3)
            log('PASS native SMF writer/reader preserves three tempo and meter segments')
            assert(l:open(flags.id,false,true));phase=2;at=now
        elseif phase==2 then
            verify(w);assert(w:play('song'));w.page='timing';w.revision=w.revision+1;phase=3;at=now
            log('PASS named SQLite reload retains maps, PPQ notes, automation and linked clips')
        elseif phase==3 and state and state.nativePlaying and state.nativePosition>4.05 and state.nativePosition<5.5 then
            assert(state.nativeFault==0);w.noteLength=2;assert(w:addNote(1,72,43));flags.playId=state.nativeStrikes;phase=4;at=now
            log('PASS live note edit queued inside changing-tempo passage')
        elseif phase==4 and now-at>4 and state and state.nativePlaying then
            assert(state.nativeFault==0);assert(l:save(false));phase=5;at=now
            log('PASS shared native bar-boundary replacement continues without a fault')
        elseif phase==5 then
            verify(w);w.page='timing';w.revision=w.revision+1;phase=99
            log('PASS READY Timing map UI; ongoing loop for observer/restart checks')
        elseif phase==80 then
            verify(w);assert(w:play('song'));w.page='timing';w.revision=w.revision+1;phase=99
            log('PASS saved tempo/meter maps survive server/client restart; READY Timing map UI')
        end
    end
    if now-start>100 then error('tempo-map probe timeout phase '..phase) end
end
return {engineHandlers={onFrame=function() if phase==-1 then return end;local ok,err=pcall(run);if not ok then phase=-1;log('FAIL '..tostring(err)) end end}}
