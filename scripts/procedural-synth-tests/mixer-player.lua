-- Optional isolated fixture probe; never distributed in the production package.
local core=require('openmw.core');local self=require('openmw.self');local nearby=require('openmw.nearby')
local types=require('openmw.types');local I=require('openmw.interfaces')
local schema=require('scripts.multiplayer.fetcher.procedural-synth.shared.schema')
local format=require('scripts.multiplayer.fetcher.procedural-synth.shared.project')(schema)
local role,start,phase,at=nil,0,0,0
local sawUpdate=false
local function log(s) print('[SynthMixerTest] '..tostring(role)..' '..s) end
local function run()
    local now=core.getRealTime()
    if not role then role=types.NPC.record(self).name;start=now;log('ready');return end
    local api=I.ProceduralSynth;if not api then return end
    local state=api.status()[1];local w=api.workstation()
    if role=='Synth A' then
        if phase==0 and now-start>8 then
            for _,o in ipairs(nearby.activators) do if o.recordId=='fetcher_procedural_synth_v1' then o:activateBy(self);phase=1;break end end
        elseif phase==1 and w and #w.catalog>=#schema.builtins then
            assert(w:load(format.new()));assert(w:addKit());assert(w:add('piano'));assert(w:preset(5,'b:guitar-clean'))
            for step=0,14,2 do w:toggleStep(1,step);w:toggleStep(3,step) end
            w:toggleStep(2,4);w:toggleStep(2,12);w:toggleStep(4,15)
            w.noteLength=4;for _,step in ipairs{0,4,8,12} do w:toggleNote(5,48,step) end
            for k,v in pairs{eqEnabled=1,highpass=180,bass=6,mid=-4,compEnabled=1,threshold=-40,ratio=6,makeup=3} do assert(w:setMixer(5,k,v)) end
            assert(w:undo() and w:redo());assert(w.project.tracks[5].mixer.ratio==6,'strip undo/redo failed')
            w.project.name='Mixer QA';w:touch();assert(w:save());phase=2;at=now
        elseif phase==2 and now-at>3 and not w.dirty then
            assert(w:setMixer(5,'highpass',999));api.loadProject();phase=3;at=now
        elseif phase==3 and now-at>1 and not w.dirty then
            assert(w.project.tracks[5].mixer.highpass==180 and w.project.tracks[5].mixer.ratio==6,'saved strip did not reload')
            w.selected=5;w.page='channel';api.studio();assert(w:play('clips'));phase=4;at=now
            log('PASS project save/load, mixer undo/redo; native five-lane loop started')
        elseif phase==4 and state and state.nativePlaying and state.nativePosition>8 then
            local lane;for _,c in ipairs(state.channels) do if c.id==5 then lane=c end end
            assert(lane and lane.rms>0 and lane.peak>0 and lane.reduction>1 and state.masterRms>0,'native meters missing')
            for n=1,80 do w:setMixer(5,'bass',-6+n/80) end;w.revision=w.revision+1
            phase=5;at=now;log('PASS native track/master meters and gain reduction; rapid drag final bass -5 queued')
        elseif phase==5 and now-at>25 then
            assert(state.nativePlaying and state.nativeFault==0,'mixer edit interrupted score')
            w:stop(true);phase=6;at=now
        elseif phase==6 and now-at>0.5 then
            assert(not state.nativePlaying and state.voices==0,'Stop failed')
            phase=7;log('PASS rapid mixer edits preserve native playback and Stop; processor UI ready')
        elseif phase>0 and phase<7 and now-start>100 then error('mixer probe timed out') end
    elseif role=='Synth B' and state then
        local lane;for _,c in ipairs(state.channels) do if c.id==5 then lane=c end end
        if phase==0 and state.nativePlaying and lane and lane.rms>0 and lane.reduction>1 then
            assert(lane.mixer.highpass==180 and lane.mixer.compEnabled==1 and lane.mixer.ratio==6,'listener strip differs')
            assert(lane.reduction>1,'listener compressor not active')
            phase=1;log('PASS late join has saved EQ/compressor and native RMS / gain reduction')
        end
        if phase==1 and lane and lane.mixer.bass==-5 and not sawUpdate then sawUpdate=true;log('PASS rapid edits deliver final bass -5 to observer') end
        if phase==1 and sawUpdate and not state.nativePlaying and state.voices==0 then phase=2;log('PASS observer Stop releases all processed lanes') end
    end
end
return {engineHandlers={onFrame=function()
    if phase==-1 then return end
    local ok,err=pcall(run);if not ok then phase=-1;log('FAIL '..tostring(err)) end
end},eventHandlers={Synth_Message=function(data)
    if data.op=='scoreError' or data.op=='error' then log('ERROR '..tostring(data.message)) end
end}}
