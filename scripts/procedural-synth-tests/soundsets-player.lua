local core=require('openmw.core');local self=require('openmw.self');local nearby=require('openmw.nearby')
local types=require('openmw.types');local I=require('openmw.interfaces')
local format=require('scripts.multiplayer.fetcher.procedural-synth.shared.project')(require('scripts.multiplayer.fetcher.procedural-synth.shared.schema'))
local sets=require('scripts.multiplayer.fetcher.procedural-synth.client.soundsets')
local phase,start,at,role,index=0,nil,0,nil,1
local seen={}
local function log(s) print('[SynthSoundSetTest] '..tostring(role)..' '..s) end
local function song()
    local s={tracks=6,division=96,names={},programs={},tempos={},duration=80,beats=160,events={}}
    local order=0
    for n,entry in ipairs{{0,60,0},{1,36,32},{2,64,30},{9,36},{9,38},{9,42}} do
        local channel,pitch=entry[1],entry[2];local part=n..':'..channel..(channel==9 and ':'..pitch or '')
        s.programs[part]=entry[3]
        for hit=0,79 do for _,kind in ipairs{'on','off'} do
            order=order+1;local time=hit+(kind=='off' and 0.2 or 0)
            s.events[#s.events+1]={kind=kind,part=part,track=n,channel=channel,note=pitch,velocity=0.7,time=time,tick=time*192,order=order}
        end end
    end
    table.sort(s.events,function(a,b)return a.time==b.time and a.order<b.order or a.time<b.time end)
    return s
end
local function run()
    local now=core.getRealTime();start=start or now;role=role or types.NPC.record(self).name
    local api=I.ProceduralSynth;if not api then return end
    local state=api.status()[1];local w=api.workstation()
    if role=='Synth A' then
        if phase==0 and now-start>6 then
            for _,o in ipairs(nearby.activators) do if o.recordId=='fetcher_procedural_synth_v1' then phase=1;o:activateBy(self);break end end
        elseif phase==1 and w then
            assert(w:load(format.new()));assert(w:import(song(),'midi/soundset-qa.mid'))
            assert(w.project.tracks[1].presetId=='b:studio-keys' and w.project.tracks[2].params.tune1==0 and w.project.tracks[3].presetId=='b:guitar-metal','GM defaults differ')
            w.project.name='Sound Set QA';w.project.tracks[1].level=0.31;w:touch();api.studio();assert(w:play('song'))
            phase=2;at=now;log('PASS program-aware defaults; six-part native song started')
        elseif phase==2 and state and state.nativePlaying and now-at>4 then
            index=index+1
            if index<=#sets.entries then
                assert(w:applySoundSet(sets.entries[index].id))
                assert(w.playing and w.native and not w.sequenceDirty and w.project.tracks[1].level==0.31 and #w.project.tracks==6,'set changed transport or mix')
                at=now;log('PASS live set '..sets.entries[index].name)
            else assert(w:save());phase=3;at=now end
        elseif phase==3 and now-at>3 and not w.dirty then api.loadProject();phase=4;at=now
        elseif phase==4 and now-at>4 and not w.dirty then
            assert(w.project.soundSet==sets.entries[#sets.entries].id and w.project.tracks[3].presetId=='b:guitar-metal' and w.project.tracks[1].level==0.31,'saved set/snapshot/mix mismatch')
            local details='native='..tostring(state.nativePlaying)..' UI='..tostring(w.playing)..' voices='..state.voices..' pos='..state.nativePosition
            for _,c in ipairs(state.channels) do details=details..' lane'..c.id..'='..c.voices end
            assert(not state.nativePlaying and state.voices==0,'load left scheduled voices: '..details)
            phase=5;log('PASS character database sound-set round trip and Stop; Mixer ready')
        elseif phase>0 and phase<5 and now-start>100 then error('sound set probe timed out') end
    elseif role=='Synth B' and state then
        for _,c in ipairs(state.channels) do if c.id==3 and c.instrumentType==1 then
            if c.driveType==1 and c.drive==22 and not seen.rock then seen.rock=true;log('PASS observer Rock guitar state') end
            if c.driveType==2 and c.drive==32 and not seen.metal then seen.metal=true;log('PASS observer Metal guitar state') end
            if seen.metal and c.driveType==0 and not seen.clean then seen.clean=true;log('PASS observer clean guitar state after cycling') end
        end end
        if seen.rock and seen.metal and seen.clean and not state.nativePlaying and state.voices==0 and phase==0 then phase=1;log('PASS observer playback survived bank cycling and stopped on project load') end
    end
end
return {engineHandlers={onFrame=function()
    if phase==-1 then return end
    local ok,err=pcall(run);if not ok then phase=-1;log('FAIL '..tostring(err)) end
end},eventHandlers={Synth_Message=function(data)
    if data.op=='scoreError' or data.op=='error' then log('ERROR '..tostring(data.message)) end
end}}
