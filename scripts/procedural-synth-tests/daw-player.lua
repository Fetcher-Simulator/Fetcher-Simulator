-- Disposable fixture only: actual engine voices, two clients and SQLite project.
local core=require('openmw.core')
local nearby=require('openmw.nearby')
local self=require('openmw.self')
local types=require('openmw.types')
local I=require('openmw.interfaces')
local role,start,phase,played,last,saved,loaded,heard=nil,0,0,0,0,false,false,false
local seen={}
local function log(s) print('[SynthDawTest] '..tostring(role)..' '..s) end
local function activate()
    for _,o in ipairs(nearby.activators) do if o.recordId=='fetcher_procedural_synth_v1' then o:activateBy(self);return true end end
end
local function run()
    local now=core.getRealTime()
    if not role then role=types.NPC.record(self).name;start=now;log('ready');return end
    local api=I.ProceduralSynth;if not api then return end
    local state=api.status()[1]
    if state then
        if state.voices>0 and state.peak>0 then heard=true end
        for _,c in ipairs(state.channels or {}) do if c.voices>0 and c.peak>0 then seen[c.id]=true end end
        if now-last>=2 then
            last=now;log(string.format('tracks=%d channels=%d voices=%d peak=%.4f transport=%s time=%.2f visible=%s',state.tracks,#state.channels,state.voices,state.peak,tostring(state.transport),state.transportTime,tostring(state.editorVisible)))
        end
    end
    if role=='Synth A' then
        local w=api.workstation()
        if phase==0 and now-start>12 then if activate() then phase=1 end
        elseif phase==1 and state and state.editing then
            for _,path in ipairs(api.midiSongs()) do if path:lower():find('greensleeves.mid',1,true) then
                assert(api.importMidi(path));phase=2;break
            end end
            assert(phase==2,'Greensleeves not installed')
        elseif phase==2 and w and #w.project.tracks>=2 and #w.catalog>=8 then
            assert(w:preset(1,'b:flute'));assert(w:preset(2,'b:bass'))
            w.project.name='DAW Integration';assert(api.saveProject())
            api.studio();assert(w:play('song'));played=now;phase=3
            log('started '..#w.project.tracks..' MIDI parts with independent flute and bass')
        elseif phase==3 and now-played>10 then
            assert(state.transport and state.transportTime>3 and heard,'DAW audio/transport not active')
            api.hide();phase=4;log('PASS independent MIDI parts; Studio hidden')
        elseif phase==4 and now-played>22 then
            assert(state.transport and not state.editorVisible,'Hidden DAW lost transport')
            assert(api.show());phase=5;log('PASS hidden transport and reopen')
        elseif phase==5 and not w.playing and now-played>20 then
            assert(w.time>=40.5 and saved,'Song completion or project persistence failed')
            assert(w:add('drum'));local kick=w.selected;w:preset(kick,'b:kick');w.project.tracks[kick].note=36
            for _,step in ipairs{0,4,8,12} do w:toggleStep(kick,step) end
            assert(w:add('drum'));local snare=w.selected;w:preset(snare,'b:snare')
            for _,step in ipairs{4,12} do w:toggleStep(snare,step) end
            assert(w:add('piano'));w.noteLength=4;w:toggleNote(w.selected,60,0);w:toggleNote(w.selected,67,8)
            w:length(32);w:toggleStep(kick,20);w:toggleNote(w.selected,64,16)
            w:length(256);w:toggleStep(kick,255);w:toggleNote(w.selected,72,255)
            for length=16,256,16 do assert(w:length(length)) end
            assert(w.project.tracks[kick].steps:sub(21,21)~='0' and w.project.tracks[kick].steps:sub(256,256)~='0')
            w:length(16);assert(api.saveProject())
            log('PASS bar 2 and bar 16 retained after cycling all loop lengths; saving with a short loop')
            w.page='drums';api.studio();assert(w:play('clips'));played=now;phase=6
            log('PASS complete MIDI at real elapsed time; drum/piano clips started')
        elseif phase==6 and now-played>5 then
            assert(w.playing and heard,'Clip transport failed');w:stop();api.loadProject();loaded=false;phase=7;played=now
            log('PASS drum machine and piano clips')
        elseif phase==7 and loaded then
            assert(w.project.name=='DAW Integration' and w.project.tracks[1].presetId=='b:flute' and w.project.tracks[2].presetId=='b:bass','Saved routing lost')
            assert(not w.playing,'Project load autoplays')
            assert(w.project.length==16 and #w.project.tracks==8,'Clip project lost on server round trip')
            w:length(256)
            assert(w.project.tracks[6].steps:sub(21,21)~='0' and w.project.tracks[6].steps:sub(256,256)~='0','Saved hidden drum bars lost')
            local notes=w.project.tracks[8].notes
            assert(notes[#notes-1].s==16 and notes[#notes].s==255,'Saved hidden piano bars lost')
            w.bar=15;w.page='piano';w.selected=8;w.revision=w.revision+1;api.close();phase=8
            log('PASS hidden bars restored from server SQLite and session released')
        elseif phase>0 and phase<8 and now-start>100 then error('DAW probe timeout') end
    elseif role=='Synth B' then
        if phase==0 and seen[1] and seen[2] and heard then phase=1;log('PASS listener hears independent channel 1 and 2 voices') end
        if phase==1 and not state then phase=2;log('PASS listener cleanup') end
    end
end
return {engineHandlers={onFrame=function()
    if phase==-1 then return end
    local ok,err=pcall(run);if not ok then phase=-1;log('FAIL '..tostring(err)) end
end},eventHandlers={Synth_Message=function(data)
    if data.op=='dawSaved' then saved=true;log('server acknowledged project save')
    elseif data.op=='dawProject' then loaded=true
    elseif data.op=='error' then log('ERROR '..tostring(data.message)) end
end}}
