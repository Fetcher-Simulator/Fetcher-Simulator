-- Isolated fixture only. Deliberately stall Lua for 1.5 s while audio must advance.
local core=require('openmw.core');local self=require('openmw.self');local nearby=require('openmw.nearby')
local types=require('openmw.types');local I=require('openmw.interfaces')
local schema=require('scripts.multiplayer.fetcher.procedural-synth.shared.schema')
local format=require('scripts.multiplayer.fetcher.procedural-synth.shared.project')(schema)
local role,start,phase,at=nil,0,0,0
local seen={}
local stoppedAt
local function log(s) print('[SynthTimingTest] '..tostring(role)..' '..s) end
local function run()
    local now=core.getRealTime()
    if not role then role=types.NPC.record(self).name;start=now;log('ready');return end
    local api=I.ProceduralSynth;if not api then return end
    local state=api.status()[1];local w=api.workstation()
    if state then for _,c in ipairs(state.channels) do if c.voices>0 and c.peak>0 then seen[c.id]=true end end end
    if role=='Synth A' then
        if phase==0 and now-start>8 then
            for _,o in ipairs(nearby.activators) do if o.recordId=='fetcher_procedural_synth_v1' then o:activateBy(self);phase=1;break end end
        elseif phase==1 and w and #w.catalog>=#schema.builtins then
            assert(w:load(format.new()));assert(w:addKit());assert(w:add('piano'));assert(w:preset(5,'b:guitar-crunch'))
            for step=0,14,2 do w:toggleStep(1,step) end
            for _,step in ipairs{4,12} do w:toggleStep(2,step) end
            for step=0,14,2 do w:toggleStep(3,step) end
            w:toggleStep(4,15);w.noteLength=2
            for _,step in ipairs{0,4,8,12} do w:toggleNote(5,48,step) end
            api.studio();assert(w:play('clips'));phase=2;at=now
        elseif phase==2 and state and state.nativePlaying and state.nativePosition>3 then
            local before=state.nativePosition;local strikes=state.nativeStrikes;local finish=core.getRealTime()+1.5
            while core.getRealTime()<finish do end
            local after=api.status()[1]
            assert(after.nativePlaying and after.nativePosition-before>1.3,'native clock stopped with Lua')
            assert(after.nativeStrikes>strikes+3,'audio-thread notes were not dispatched during the stall')
            phase=3;at=core.getRealTime();log('PASS 1.5 s Lua stall: native advanced '..(after.nativePosition-before)..' s with '..(after.nativeStrikes-strikes)..' scheduled strikes')
        elseif phase==3 and now-at>3 then
            assert(w.playing and state.nativePlaying,'game-frame guard stopped native playback')
            w:toggleStep(2,8);phase=4;at=now;log('live snare edit queued for a bar boundary')
        elseif phase==4 and now-at>25 then
            assert(w.playing and state.nativePlaying and state.nativeFault==0,'late join or replacement stopped native playback')
            for n=1,5 do assert(seen[n],'silent lane '..n) end
            w:stop(true);phase=5;at=now;log('PASS shared-clock five-track playback, live replacement and late-join window; Stop sent')
        elseif phase==5 and now-at>0.2 then
            assert(not state.nativePlaying and state.voices==0,'Stop left scheduled voices')
            phase=6;at=now
            for _,path in ipairs(api.midiSongs()) do if path:lower():find('greensleeves.mid',1,true) then assert(api.importMidi(path));break end end
        elseif phase==6 and w and w.song and #w.project.tracks>=2 then
            assert(w:play('song'));phase=7;at=now
        elseif phase==7 and now-at>7 then
            assert(state.nativePlaying and w.playing and state.nativeFault==0,'native MIDI playback failed')
            w:stop(true);phase=8;log('PASS native MIDI tempo map plus clips; Studio ready')
        elseif phase>0 and phase<8 and now-start>100 then error('timing probe timed out') end
    elseif role=='Synth B' then
        if state and state.nativePlaying and phase==0 and seen[1] and seen[5] then
            assert(math.abs(state.phaseError)<0.2,'late listener phase error too large: '..state.phaseError)
            phase=1;log('PASS late listener entered active native score; phase error '..state.phaseError..' s / clock RTT '..state.clockRtt..' s')
        elseif phase==1 and stoppedAt and now-stoppedAt>0.2 and now-stoppedAt<0.5 then
            assert(not state.nativePlaying and state.voices==0,'listener Stop failed');phase=2;log('PASS observer Stop cancels native queued notes')
        end
    end
end
return {engineHandlers={onFrame=function()
    if phase==-1 then return end
    local ok,err=pcall(run);if not ok then phase=-1;log('FAIL '..tostring(err)) end
end},eventHandlers={Synth_Message=function(data)
    if data.op=='scoreError' or data.op=='error' then log('ERROR '..tostring(data.message)) end
    if data.op=='transportStop' then stoppedAt=core.getRealTime() end
end}}
