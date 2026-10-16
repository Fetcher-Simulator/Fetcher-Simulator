-- Optional disposable fixture probe; never included in the production package.
local core=require('openmw.core')
local nearby=require('openmw.nearby')
local self=require('openmw.self')
local types=require('openmw.types')
local I=require('openmw.interfaces')
local schema=require('scripts.multiplayer.fetcher.procedural-synth.shared.schema')
local start,role,phase,playing,lastLog,received=0,nil,0,0,0,0
local observed=false
local function log(message) print('[SynthLiveTest] '..tostring(role)..' '..message) end
local function activate()
    for _,obj in ipairs(nearby.activators) do
        if obj.recordId=='fetcher_procedural_synth_v1' then
            local p=self.position
            log(string.format('activate position=%.1f,%.1f,%.1f',p.x,p.y,p.z))
            obj:activateBy(self); log('activate invoked'); return true
        end
    end
    return false
end
return {
    engineHandlers={onFrame=function()
        local now=core.getRealTime()
        if not role then role=types.NPC.record(self).name; start=now; log('probe ready'); return end
        if not I.ProceduralSynth then return end
        local status=I.ProceduralSynth.status()
        if now-lastLog>1 then
            lastLog=now
            for _,s in ipairs(status) do log(string.format('audio id=%d editing=%s voices=%d peak=%.5f seq=%d cutoff=%.1f wave=%d',
                s.id,tostring(s.editing),s.voices,s.peak,s.seq,s.cutoff,s.wave1)) end
        end
        local api=I.ProceduralSynth
        if role=='Synth B' then
            if status[1] and not observed then
                log('initial listener snapshot voices='..status[1].voices..' receivedNotes='..received)
                observed=true
            end
            if phase==0 and (received>=5 or (received==0 and observed)) and #status==0 then activate();phase=1
            elseif phase==1 and status[1] and status[1].editing then
                api.noteOn(55);playing=now;phase=2;log('PASS acquired after abrupt owner disconnect')
            elseif phase==2 and now-playing>1 then api.noteOff(55);api.close();phase=3;log('PASS observer takeover complete') end
            return
        end
        if role~='Synth A' then return end
        if phase==0 and now-start>12 then
            if activate() then phase=1 else log('activator unavailable'); start=now end
        elseif phase==1 and status[1] and status[1].editing then
            log('editor open; playing C major')
            api.noteOn(60);api.noteOn(64);api.noteOn(67);playing=now;phase=2
        elseif phase==2 and now-playing>2 then api.setParameter('wave1',2);api.setParameter('cutoff',1200);log('live edit');phase=3
        elseif phase==3 and now-playing>5 then api.noteOff(60);api.noteOff(64);api.noteOff(67);log('notes released');phase=4
        elseif phase==4 and now-playing>6 then
            api.presetCommand('saveAs',{name='Integration Tone',params=schema.state({wave1=2,cutoff=1234})});phase=5
        elseif phase==5 and now-playing>7 then api.presetCommand('load',{presetId='b:pad'});phase=6
        elseif phase==6 and now-playing>8 then api.presetCommand('load',{presetId='p:1'});phase=7
        elseif phase==7 and now-playing>10 then api.close();log('editor closed');phase=8
        elseif phase==8 and now-playing>13 then activate();phase=9
        elseif phase==9 and status[1] and status[1].editing then
            log('reacquired after close'); api.noteOn(72);phase=10;playing=now
        elseif phase==10 and now-playing>2 then api.noteOff(72);api.close();phase=11;log('PASS live sequence complete')
        elseif phase==11 and now-playing>6 then activate();phase=12
        elseif phase==12 and status[1] and status[1].editing then
            api.noteOn(76);phase=13;log('READY abrupt disconnect: held note 76')
        end
    end},
    eventHandlers={Synth_Message=function(data)
        if data.op=='on' then received=received+1;log('remote note-on '..data.note..' count='..received)
        elseif data.op=='catalog' then
            local count=0;for _,p in ipairs(data.entries) do if p.name=='Integration Tone' then count=count+1 end end
            log('catalog personal Integration Tone count='..count)
        elseif data.op=='preset' then log('loaded '..tostring(data.presetId)..' cutoff='..data.params.cutoff)
        elseif data.op=='saved' then log('saved '..tostring(data.presetId))
        elseif data.op=='error' then log('ERROR '..data.message) end
    end},
}
