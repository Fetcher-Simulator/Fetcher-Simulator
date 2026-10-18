-- Local two-client send/return and persistence probe.
local core=require('openmw.core');local self=require('openmw.self');local nearby=require('openmw.nearby')
local types=require('openmw.types');local I=require('openmw.interfaces')
local schema=require('scripts.multiplayer.fetcher.procedural-synth.shared.schema')
local format=require('scripts.multiplayer.fetcher.procedural-synth.shared.project')(schema)
local role,start,phase,at=nil,0,0,0;local updated=false
local function log(s) print('[SynthReturnsTest] '..tostring(role)..' '..s) end
local function run()
    local now=core.getRealTime()
    if not role then role=types.NPC.record(self).name;start=now;log('ready');return end
    local api=I.ProceduralSynth;if not api then return end
    local state=api.status()[1];local w=api.workstation()
    if role=='Synth A' then
        if phase==0 and now-start>8 then
            for _,o in ipairs(nearby.activators) do if o.recordId=='fetcher_procedural_synth_v1' then o:activateBy(self);phase=1;break end end
        elseif phase==1 and w and #w.catalog>=#schema.builtins then
            assert(w:load(format.new()));assert(w:add('piano'));assert(w:preset(1,'b:pluck'))
            w.noteLength=1;for _,step in ipairs{0,4,8,12} do w:toggleNote(1,60+step,step) end
            assert(w:setMixer(1,'delaySend',0.8));assert(w:setMixer(1,'reverbSend',0.7))
            assert(w:setEffect('delayTime',375));assert(w:setEffect('reverbDecay',2.8))
            assert(w:undo() and w:redo());w.project.name='Returns QA';w:touch();assert(w:save());phase=2;at=now
        elseif phase==2 and now-at>3 and not w.dirty then
            assert(w:setEffect('delayTime',999));api.loadProject();phase=3;at=now
        elseif phase==3 and now-at>1 and not w.dirty then
            assert(w.project.effects.delayTime==375 and w.project.tracks[1].mixer.reverbSend==0.7,'return project round trip failed')
            w.page='returns';api.studio();assert(w:play('clips'));phase=4;at=now
            log('PASS saved sends/returns, undo/redo and reload; native playback started')
        elseif phase==4 and state and state.nativePosition>8 then
            assert(state.delayPeak>0 and state.reverbPeak>0 and state.masterRms>0,'native return meters silent')
            w.project.master=0.35;w:configureAll();w:touch('master',true);phase=5;at=now
            log('PASS both native returns active; post-return master changed to 0.35')
        elseif phase==5 and now-at>25 then
            assert(state.nativePlaying and state.effects.delayTime==375 and state.master==0.35,'live master stopped transport')
            w:stop(true);phase=6;at=now
        elseif phase==6 and now-at>4 then
            assert(not state.nativePlaying and state.voices==0 and state.masterRms<0.00001,'Stop left wet audio')
            phase=7;log('PASS Stop clears wet tails; UI ready for keyboard and dropdown review')
        elseif phase>0 and phase<7 and now-start>100 then error('return probe timed out') end
    elseif role=='Synth B' and state then
        if phase==0 and state.nativePlaying and state.delayPeak>0 and state.reverbPeak>0 then
            local lane=state.channels[1]
            assert(state.effects.delayTime==375 and state.effects.reverbDecay==2.8 and lane.mixer.delaySend==0.8,'listener send/return settings differ')
            phase=1;log('PASS late listener gets sends, shared return controls and wet audio')
        end
        if phase==1 and state.master==0.35 and not updated then updated=true;log('PASS observer receives post-return master 0.35') end
        if phase==1 and updated and not state.nativePlaying and state.voices==0 and state.masterRms<0.00001 then
            phase=2;log('PASS observer Stop drains wet output')
        end
    end
end
return {engineHandlers={onFrame=function()
    if phase==-1 then return end
    local ok,err=pcall(run);if not ok then phase=-1;log('FAIL '..tostring(err)) end
end},eventHandlers={Synth_Message=function(data)
    if data.op=='scoreError' or data.op=='error' then log('ERROR '..tostring(data.message)) end
end}}
