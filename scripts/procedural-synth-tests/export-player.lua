local core=require('openmw.core');local self=require('openmw.self');local nearby=require('openmw.nearby');local types=require('openmw.types')
local I=require('openmw.interfaces');local synth=require('openmw.synth');local MIDI=require('scripts.multiplayer.fetcher.procedural-synth.client.midi')
local phase,start,at=0,nil,0;local role;local flags={}
local function log(s) print('[SynthExportTest] '..(role or '?')..' '..s) end
local function activate()
    for _,list in ipairs{nearby.activators,nearby.items} do for _,o in ipairs(list) do
        if o.recordId=='fetcher_procedural_synth_v1' or o.recordId=='fetcher_procedural_keyboard_v1' then o:activateBy(self);return true end
    end end
end
local function run()
    local now=core.getRealTime();start=start or now;role=role or types.NPC.record(self).name
    local api=I.ProceduralSynth;if not api then return end
    if role=='Synth B' then
        local state=api.status()[1]
        if state and state.nativePlaying and state.voices>0 and not flags.heard then flags.heard=true;log('PASS round-trip MIDI audible on observer') end
        return
    end
    if role~='Synth A' or phase==20 then return end
    local w=api.workstation();local state=api.status()[1]
    if phase==0 and now-start>6 then if activate() then phase=1 end
    elseif phase==1 and w then
        api.studio();assert(w:newSong(120,6,8,8));w.project.name='MIDI Export QA'
        assert(w:addNote(1,60,0) and w:addNote(1,64,4));w:selectAllNotes(1);assert(w:captureClip(1))
        assert(w:changeRegion(1,1,{l=32}));assert(w:renameTrack(1,'Renamed keyboard'))
        assert(w:addPart('snare'));assert(w:addNote(2,38,4));assert(w:renameTrack(2,'Renamed snare'))
        assert(w:exportMidi());local names=synth.listMidiExports();flags.name=names[#names];assert(flags.name)
        local bytes=synth.readMidiExport(flags.name);local parsed=MIDI.parse(bytes)
        assert(parsed.names[2]=='Renamed keyboard' and parsed.names[3]=='Renamed snare' and #MIDI.parts(parsed)==2)
        local ok=pcall(synth.readMidiExport,'../player_storage.bin');assert(not ok)
        local bad=pcall(synth.exportMidi,'bad','not MIDI');assert(not bad)
        log('PASS native export/read, linked notes, track names, percussion and restricted paths')
        assert(w:save());phase=2;at=now
    elseif phase==2 and not w.dirty and now-at>2 then
        assert(api.importMidi('midi/__studio_exports__/'..flags.name));phase=3;at=now
    elseif phase==3 and w.project.midi:find('__studio_exports__',1,true) and now-at>1 then
        assert(w:play('song'));phase=4;at=now
    elseif phase==4 and state.nativePlaying and now-at>3 then
        log('PASS exported MIDI immediately selectable/importable and playing on native clock');w:stop(true);api.loadProject();phase=5;at=now
    elseif phase==5 and w.project.name=='MIDI Export QA' and now-at>2 then
        assert(w.project.tracks[1].name=='Renamed keyboard');assert(w:add('drum'));assert(w:toggleStep(3,0))
        w.bar=0;assert(w:duplicateDrumBar());assert(w.project.length==32 and w.project.tracks[3].steps:sub(1,16)==w.project.tracks[3].steps:sub(17,32))
        assert(w:undo() and w.project.length==16);assert(w:redo() and w.project.length==32)
        log('PASS renamed project reload and drum bar clone/undo/redo');w.selected=1;w.page='piano';w.revision=w.revision+1;phase=20
        log('PASS complete; Piano open for fullscreen and mouse verification')
    end
    if now-start>90 then error('export probe timeout phase '..phase) end
end
return {engineHandlers={onFrame=function() if phase==-1 then return end;local ok,err=pcall(run);if not ok then phase=-1;log('FAIL '..tostring(err)) end end}}
