-- Disposable two-client MIDI + procedural arm-pose probe.
local core=require('openmw.core')
local nearby=require('openmw.nearby')
local self=require('openmw.self')
local types=require('openmw.types')
local I=require('openmw.interfaces')
local mp=require('openmw.mp')
local util=require('openmw.util')
local anim=require('openmw.animation')
local role,start,phase,played,last,heard=nil,0,0,0,0,false
local function log(s) print('[SynthMidiTest] '..tostring(role)..' '..s) end
local function activate()
    for _,o in ipairs(nearby.activators) do
        if o.recordId=='fetcher_procedural_synth_v1' then o:activateBy(self);return true end
    end
end
return {engineHandlers={onFrame=function()
    local now=core.getRealTime()
    if not role then role=types.NPC.record(self).name;start=now;log('ready');return end
    local api=I.ProceduralSynth
    if not api then return end
    local state=api.status()[1]
    if state and now-last>1 then
        last=now
        log(string.format('editing=%s midi=%s voices=%d peak=%.5f bones=%d handError=%.3f',
            tostring(state.editing),tostring(state.midi),state.voices,state.peak,state.poseBones,state.poseError))
        if state.voices>0 and state.peak>0 then heard=true end
        local actor=mp.getPlayerObject(state.owner)
        if actor then
            local shoulder=anim.getBoneTransform(actor,'Bip01 R UpperArm',true)
            local hand=anim.getBoneTransform(actor,'Bip01 R Hand',true)
            if shoulder and hand then
                local s,h=shoulder*util.vector3(0,0,0),hand*util.vector3(0,0,0)
                log(string.format('world shoulder=(%.1f,%.1f,%.1f) hand=(%.1f,%.1f,%.1f) actorZ=%.1f',s.x,s.y,s.z,h.x,h.y,h.z,actor.position.z))
            end
        end
    end
    if role=='Synth A' then
        if phase==0 and now-start>12 then if activate() then phase=1 end
        elseif phase==1 and state and state.editing then
            local songs=api.midiSongs();log('catalog songs='..#songs)
            for _,path in ipairs(songs) do
                if path:lower():find('greensleeves.mid',1,true) then
                    assert(api.playMidi(path));phase=2;played=now;log('requested Greensleeves');break
                end
            end
            assert(phase==2,'No Greensleeves MIDI installed')
        elseif phase==2 and now-played>3 then
            assert(state and state.midi and heard and state.poseBones==6,'MIDI/pose did not start')
            log('PASS MIDI playback and six-bone pose active')
            api.hide();assert(not api.status()[1].editorVisible);phase=3
        elseif phase==3 and now-played>14 then
            assert(state and state.midi and state.editing and not state.editorVisible,'Hidden performance lost session')
            assert(api.show());assert(api.status()[1].midi and api.status()[1].editorVisible)
            log('PASS hidden MIDI heartbeat and reopen without interruption');phase=4
        elseif phase==4 and now-played>46 then
            assert(state and not state.midi,'MIDI did not finish')
            api.close();phase=5;log('PASS MIDI completed; editor closed')
        end
    elseif role=='Synth B' then
        if state and state.poseBones==6 and state.poseError<0.5 and heard and phase==0 then phase=1;log('PASS listener audio and performer pose') end
        if phase==1 and not state then phase=2;log('PASS listener cleanup') end
    end
end},eventHandlers={Synth_Message=function(data)
    if data.op=='error' then log('ERROR '..tostring(data.message)) end
end}}
