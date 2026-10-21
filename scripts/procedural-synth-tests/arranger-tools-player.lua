-- Disposable native QA; setup must back up the current fixture first.
local core=require('openmw.core');local self=require('openmw.self');local nearby=require('openmw.nearby');local types=require('openmw.types')
local I=require('openmw.interfaces');local synth=require('openmw.synth');local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local MIDI=require(prefix..'client.midi');local Format=require(prefix..'shared.project')(require(prefix..'shared.schema'))
local Sequence=require(prefix..'shared.sequence');local Export=require(prefix..'client.midi-export')
local phase,start,at,role=0,nil,0,nil;local flags={}
local function log(s) print('[SynthArrangerToolsTest] '..(role or '?')..' '..s) end
local function activate() for _,list in ipairs{nearby.activators,nearby.items} do for _,o in ipairs(list) do
    if o.recordId=='fetcher_procedural_synth_v1' or o.recordId=='fetcher_procedural_keyboard_v1' then o:activateBy(self);return end
end end end
local function verify(w)
    local a,b=w.project.tracks[1],w.project.tracks[2]
    assert(#a.arrangement.clips==2 and #b.arrangement.clips==3 and #a.arrangement.regions==3 and #b.arrangement.regions==3)
    assert(a.arrangement.regions[1].offset==2 and a.arrangement.regions[3].clip==2 and b.arrangement.regions[3].clip==3)
    assert(a.arrangement.clips[1].notes.bytes~=a.arrangement.clips[2].notes.bytes)
    assert(a.arrangement.clips[2].notes.bytes==b.arrangement.clips[3].notes.bytes)
    assert(Sequence.compile(w.project,nil,MIDI,'song',Format).barGrid)
end
local function run()
    local now=core.getRealTime();start=start or now;role=role or types.NPC.record(self).name
    local api=I.ProceduralSynth;if not api then return end
    local state=api.status()[1]
    if role=='Synth B' then
        if state and state.nativePlaying and state.voices>0 and not flags.audio then
            flags.audio=true;assert(state.nativeFault==0);log('PASS observer hears offset/unique/transferred arrangement; strikes='..state.nativeStrikes..' fault='..state.nativeFault)
        end
        return
    end
    if role~='Synth A' or phase==99 then return end
    local w=api.workstation();local l=api.projectLibrary()
    if phase==0 and now-start>6 then
        if not w then activate() elseif l.ready and not l.pending then
            api.studio();local saved
            for _,e in ipairs(l.entries) do if e.name=='Arranger tools QA' then saved=e end end
            if saved then assert(l:open(saved.id,false,true));phase=80;at=now else
                assert(w:newSong(120,4,4,8));assert(w:setComposition{loop=true})
                w.noteLength=4;assert(w:addNote(1,60,0) and w:addNote(1,64,8));w:selectAllNotes(1);assert(w:captureClip(1));assert(w:duplicateRegion(1,1))
                assert(w:addPart('bass'));assert(w:addNote(2,36,32));w:selectAllNotes(2);assert(w:captureClip(2))
                w:selectRegion(1,1,false);w:selectRegion(2,1,true);assert(w:batchRegions('move',16))
                assert(w:batchRegions('duplicate') and w:batchRegions('unique'))
                assert(w:editClip(1,2));w:selectAllNotes(1);assert(w:editSelected(1,{pitch=2}) and w:leaveClip())
                assert(w:copyClip(1,2,2) and w:placeClip(2,3,96));assert(w:changeRegion(1,1,{offset=2}))
                verify(w);assert(l:save(true,'Arranger tools QA'));phase=1;at=now
            end
        end
    elseif w and l and not l.pending and now-at>1 then
        if phase==1 then
            flags.id=l.active;assert(not w.dirty);local bytes=Export.encode(w.project,nil,MIDI,Format)
            local file=synth.exportMidi('Arranger tools QA',bytes);local parsed=MIDI.parse(synth.readMidiExport(file.name));local count=0
            for _,list in pairs(MIDI.noteSpans(parsed)) do count=count+#list end
            assert(count==#Sequence.compile(w.project,nil,MIDI,'song',Format).bytes/11)
            log('PASS group/unique/transfer/offset tools and native MIDI export share note expansion')
            assert(l:open(flags.id,false,true));phase=2;at=now
        elseif phase==2 then
            verify(w);assert(w:play('song'));w.page='arrange';w.revision=w.revision+1;phase=3;at=now
            log('PASS real wire/SQLite named reload retains independent sources, source offsets and routing')
        elseif phase==3 and state and state.nativePlaying and state.nativePosition>2 and state.nativePosition<3.5 then
            w:selectRegion(1,1,false);w:selectRegion(2,1,true);local history=#w.undoStack
            assert(w:batchRegions('move',16) and #w.undoStack==history+1);assert(w:undo() and w:redo());assert(w:play('song'))
            phase=4;at=now;log('PASS cross-track move and Undo/Redo commit once and replay')
        elseif phase==4 and now-at>4 and state and state.nativePlaying then
            -- A second edit remains on the running native clock for bar replacement.
            w:selectRegion(1,1,false);w:selectRegion(2,1,true)
            assert(w:batchRegions('move',-4));phase=5;at=now
        elseif phase==5 and now-at>3 and state and state.nativePlaying then
            assert(state.nativeFault==0);assert(l:save(false));phase=6;at=now
            log('PASS live group edit replaces offset arrangement without a native fault')
        elseif phase==6 then
            verify(w);w.page='arrange';w.revision=w.revision+1;phase=99;log('PASS READY Arrange tools UI and restart checks')
        elseif phase==80 then
            verify(w);assert(w:play('song'));w.page='arrange';w.revision=w.revision+1;phase=99
            log('PASS copied/independent sources and offsets survive server/client restart')
            phase=81;at=now
        elseif phase==81 and state and state.nativePlaying and now-at>2 then
            assert(w:changeRegion(1,1,{offset=4}) and w.sequenceDirty);phase=82;at=now
        elseif phase==82 and state and state.nativePlaying and now-at>3 then
            assert(state.nativeFault==0 and not w.sequenceDirty);w.page='arrange';w.revision=w.revision+1;phase=99
            log('PASS live source-offset change queues and completes native replacement; READY Arrange UI')
        end
    end
    if now-start>120 then error('arranger-tools probe timeout phase '..phase) end
end
return {engineHandlers={onFrame=function() if phase==-1 then return end;local ok,err=pcall(run);if not ok then phase=-1;log('FAIL '..tostring(err)) end end}}
