-- Disposable local fixture: native guitar/effects, catalog, observers and SQLite.
local core=require('openmw.core')
local nearby=require('openmw.nearby')
local self=require('openmw.self')
local types=require('openmw.types')
local I=require('openmw.interfaces')
local schema=require('scripts.multiplayer.fetcher.procedural-synth.shared.schema')
local format=require('scripts.multiplayer.fetcher.procedural-synth.shared.project')(schema)
local role,start,phase,at=nil,0,0,0
local saved,loaded=false,false
local heard={}
local lateChecked=false
local function log(text) print('[SynthGuitarTest] '..tostring(role)..' '..text) end
local function run()
    local now=core.getRealTime()
    if not role then role=types.NPC.record(self).name;start=now;log('ready');return end
    local api=I.ProceduralSynth;if not api then return end
    local state=api.status()[1]
    if state then
        for _,channel in ipairs(state.channels) do
            if channel.voices>0 and channel.peak>0 then
                heard[channel.id]=true
                if channel.id==1 then assert(channel.instrumentType==1 and channel.cabinet==1,'clean guitar configuration missing') end
                if channel.id==2 then assert(channel.instrumentType==1 and channel.driveType==2 and channel.cabinet==3,'metal effects configuration missing') end
            end
        end
    end
    if role=='Synth A' then
        local w=api.workstation()
        if phase==0 and now-start>8 then
            for _,o in ipairs(nearby.activators) do if o.recordId=='fetcher_procedural_synth_v1' then o:activateBy(self);phase=1;break end end
        elseif phase==1 and w and #w.catalog>=#schema.builtins then
            assert(w:load(format.new()));assert(w:add('piano'));assert(w:preset(1,'b:guitar-clean'))
            assert(w:add('piano'));assert(w:preset(2,'b:guitar-metal'))
            assert(w:undo() and w.project.tracks[2].presetId~='b:guitar-metal');assert(w:redo())
            w.noteLength=4
            for _,note in ipairs{60,64,67} do w:toggleNote(1,note,0) end
            w.noteLength=2
            for _,step in ipairs{4,6,8,10,12,14} do w:toggleNote(2,40,step) end
            w.project.name='Guitar effects QA';w:touch();api.studio();assert(w:play('clips'))
            phase=2;at=now;log('PASS complete genre catalog; independent clean and metal tracks; preset undo/redo')
        elseif phase==2 and now-at>7 then
            assert(heard[1] and heard[2],'native guitar track silent: clean='..tostring(heard[1])..' metal='..tostring(heard[2]))
            w:stop(true);assert(api.saveProject());phase=3;at=now
            log('PASS both native guitar/effects tracks played; saving')
        elseif phase==3 and saved then
            api.loadProject();phase=4
        elseif phase==4 and loaded then
            assert(w.project.tracks[1].params.instrumentType==1 and w.project.tracks[1].params.cabinet==1)
            assert(w.project.tracks[2].params.driveType==2 and w.project.tracks[2].params.drive==32)
            assert(w.project.tracks[2].params.stringMute==0.7 and w.project.tracks[2].params.cabinet==3)
            assert(w:play('clips'));phase=5;at=now;log('PASS SQLite guitar/effects snapshots round trip; ready for late listener')
        elseif phase==5 and now-at>30 then
            w:stop(true);phase=6;at=now
        elseif phase==6 and now-at>0.4 then
            assert(state.voices==0,'Stop left guitar voices running');api.sound(2);phase=7;log('PASS Stop; guitar editor ready for UI inspection')
        elseif phase>0 and phase<5 and now-start>60 then error('guitar probe timed out') end
    elseif role=='Synth B' and state and heard[1] and heard[2] and not lateChecked then
        lateChecked=true;log('PASS listener received both guitar voices and independent effects configurations')
    end
end
return {engineHandlers={onFrame=function()
    if phase==-1 then return end
    local ok,err=pcall(run);if not ok then phase=-1;log('FAIL '..tostring(err)) end
end},eventHandlers={Synth_Message=function(data)
    if data.op=='dawSaved' then saved=true elseif data.op=='dawProject' then loaded=true
    elseif data.op=='error' then log('ERROR '..tostring(data.message)) end
end}}
