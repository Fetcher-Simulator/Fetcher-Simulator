-- Disposable local fixture: native drum voices, history, hat choke and persistence.
local core=require('openmw.core')
local nearby=require('openmw.nearby')
local self=require('openmw.self')
local types=require('openmw.types')
local I=require('openmw.interfaces')
local format=require('scripts.multiplayer.fetcher.procedural-synth.shared.project')(require('scripts.multiplayer.fetcher.procedural-synth.shared.schema'))
local role,start,phase,at,last=nil,0,0,0,0
local saved,loaded,heard=false,false,{}
local openSeen,chokeSeen=false,false
local lastGate,tailCheckAt=nil,nil
local function log(s) print('[SynthDrumTest] '..tostring(role)..' '..s) end
local function run()
    local now=core.getRealTime()
    if not role then role=types.NPC.record(self).name;start=now;log('ready');return end
    local api=I.ProceduralSynth;if not api then return end
    local state=api.status()[1]
    if state then
        local channels={}
        for _,c in ipairs(state.channels) do channels[c.id]=c;if c.voices>0 and c.peak>0 then heard[c.id]=true end end
        if channels[4] and channels[4].voices>0 then openSeen=true end
        if openSeen and channels[3] and channels[3].voices>0 and channels[4] and channels[4].voices==0 then chokeSeen=true end
        if tailCheckAt and now>tailCheckAt then
            assert(channels[4] and channels[4].voices==0,'listener Stop left a tail after note-off')
            tailCheckAt=nil;log('PASS listener Stop releases a one-shot tail after note-off')
        end
        if now-last>2 then last=now;log('voices='..state.voices..' peak='..state.peak..' phase='..phase) end
    end
    if role=='Synth A' then
        local w=api.workstation()
        if phase==0 and now-start>10 then
            for _,o in ipairs(nearby.activators) do if o.recordId=='fetcher_procedural_synth_v1' then o:activateBy(self);phase=1;break end end
        elseif phase==1 and w then
            assert(w:load(format.new()));assert(w:addKit());assert(#w.project.tracks==4)
            assert(w:undo() and #w.project.tracks==0);assert(w:redo() and #w.project.tracks==4)
            w:toggleStep(2,4);assert(w:undo());assert(w.project.tracks[2].steps:sub(5,5)=='0');assert(w:redo())
            w:toggleStep(2,12)
            for _,n in ipairs{0,8} do w:toggleStep(1,n) end
            for _,n in ipairs{0,2,4,6,8,10,12} do w:toggleStep(3,n) end
            w:toggleStep(4,14)
            w.project.name='Native drum QA';w.project.bpm=110;w:touch()
            w.page='drums';w.selected=2;api.studio();assert(w:audition(4));at=now;phase=2
            log('PASS configured four native drum voices; kit and step undo/redo')
        elseif phase==2 and now-at>0.08 then
            assert(w:audition(3));phase=3;at=now
        elseif phase==3 and now-at>0.3 then
            assert(chokeSeen,'closed hat did not choke the open hat');assert(w:audition(4));phase=31;at=now
            log('PASS native open-hat choke')
        elseif phase==31 and now-at>0.2 then w:stop(true);phase=32;at=now
        elseif phase==32 and now-at>0.12 then
            for _,c in ipairs(state.channels) do if c.id==4 then assert(c.voices==0,'Stop left a one-shot tail after note-off') end end
            assert(w:play('clips'));phase=4;at=now
            log('PASS Stop releases one-shot tails after their MIDI gates; pattern started')
        elseif phase==4 and now-at>6 then
            for n=1,4 do assert(heard[n],'native drum lane '..n..' was silent') end
            w:stop();assert(api.saveProject());phase=5;at=now
            log('PASS all four drum voices played; saving native parameters')
        elseif phase==5 and saved then api.loadProject();phase=6
        elseif phase==6 and loaded then
            assert(#w.project.tracks==4 and w.project.tracks[2].params.bodyFrequency==190 and w.project.tracks[2].params.noiseDecay==0.24)
            assert(w.project.tracks[4].params.drumType==4 and not w.playing)
            w.selected=2;w.page='drums';api.studio();phase=7
            log('PASS server SQLite round trip; Studio ready for numeric/editor UI checks')
        elseif phase>0 and phase<7 and now-start>60 then error('drum probe timed out') end
    elseif role=='Synth B' and phase==0 and heard[1] and heard[2] and heard[3] and heard[4] and chokeSeen then
        phase=1;log('PASS listener heard all native drum types and applied the same hat choke')
    end
end
return {engineHandlers={onFrame=function()
    if phase==-1 then return end
    local ok,err=pcall(run);if not ok then phase=-1;log('FAIL '..tostring(err)) end
end},eventHandlers={Synth_Message=function(data)
    if role=='Synth B' and data.op=='off' and data.channel==4 then lastGate=core.getRealTime() end
    if role=='Synth B' and data.op=='panic' and lastGate and core.getRealTime()-lastGate<0.5 then tailCheckAt=core.getRealTime()+0.1;lastGate=nil end
    if data.op=='dawSaved' then saved=true
    elseif data.op=='dawProject' then loaded=true
    elseif data.op=='error' then log('ERROR '..tostring(data.message)) end
end}}
