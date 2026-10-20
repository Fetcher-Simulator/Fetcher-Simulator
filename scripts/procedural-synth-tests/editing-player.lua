-- Disposable two-client MIDI edit/SQLite/native-band integration probe.
local core=require('openmw.core');local nearby=require('openmw.nearby');local self=require('openmw.self')
local types=require('openmw.types');local I=require('openmw.interfaces')
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local MIDI=require(prefix..'client.midi');local Format=require(prefix..'shared.project')(require(prefix..'shared.schema'))
local Sequence=require(prefix..'shared.sequence')
local phase,start,at=0,nil,0;local role;local flags={};local savedBytes;local firstSource
local function log(s) print('[SynthEditingTest] '..role..' '..s) end
local function be(n,size) local out={};for i=size,1,-1 do out[i]=string.char(n%256);n=math.floor(n/256) end;return table.concat(out) end
local function vlq(n) local out={string.char(n%128)};n=math.floor(n/128);while n>0 do table.insert(out,1,string.char(128+n%128));n=math.floor(n/128) end;return table.concat(out) end
local function fixtureSong()
    local tracks={}
    for lane=0,1 do
        local events={};local previous=0
        local function event(tick,bytes) events[#events+1]=vlq(tick-previous)..bytes;previous=tick end
        for beat=0,79 do
            if lane==0 and (beat==0 or beat==40) then event(beat*96,string.char(255,81,3)..be(beat==0 and 500000 or 750000,3)) end
            event(beat*96+(beat==0 and 1 or 0),string.char(144+lane,60+lane*7,100))
            event(beat*96+72,string.char(128+lane,60+lane*7,0))
        end
        event(80*96,string.char(255,47,0));local bytes=table.concat(events);tracks[#tracks+1]='MTrk'..be(#bytes,4)..bytes
    end
    return MIDI.parse('MThd'..be(6,4)..be(1,2)..be(2,2)..be(96,2)..table.concat(tracks))
end
local function output(state,slot) for _,o in ipairs(state.outputs or {}) do if o.slot==slot then return o end end end
local function run()
    local now=core.getRealTime();start=start or now;role=role or types.NPC.record(self).name
    local api=I.ProceduralSynth;if not api then return end
    local state=api.status()[1];local w=api.workstation()
    if role=='Synth B' then
        local own=api.performer()
        if own.invitation then api.readyInstrument('lute');api.respondToInvite(true) end
        if state and state.nativePlaying and output(state,1) and not flags.play then
            assert(output(state,1).kind=='player','edited part lost player emitter')
            flags.play=true;log('PASS edited MIDI arrives on consenting player lute alongside home output')
        end
        if flags.play and not state and not flags.done then flags.done=true;log('PASS Finish released edited song and player output') end
        return
    end
    if role~='Synth A' or phase==20 then return end
    if phase==0 and now-start>7 then
        for _,obj in ipairs(nearby.activators) do if obj.recordId=='fetcher_procedural_synth_v1' then obj:activateBy(self);phase=1;break end end
    elseif phase==1 and w then
        assert(w:load(Format.new()));local song=fixtureSong();assert(w:import(song,'midi/qa/editing.mid'))
        w.project.name='MIDI edit QA';local original=Sequence.compile(w.project,song,MIDI,'song',Format)
        assert(w:convertPart(1) and w:convertPart(2));local converted=Sequence.compile(w.project,song,MIDI,'song',Format)
        assert(converted.bytes==original.bytes,'conversion changed source note timing')
        log('PASS conversion preserves exact native score, tempo change and notes after bar 16')
        w:preset(1,'b:guitar-clean');w:selectNote(1,1);assert(w:editSelected(1,{step=1,pitch=2,length=1,velocity=0.5}))
        assert(w:duplicateNotes(1));assert(w:deleteNotes(1));assert(w:undo());assert(w:redo())
        assert(w:setMember(1,{name='Player lute',level=1,mute=false,target={kind='player',characterId=1002,instrument='lute'}}))
        assert(w:setOutput(2,1));assert(w:save());savedBytes={w.project.tracks[1].edited.bytes,w.project.tracks[2].edited.bytes}
        api.studio();w.page='piano';w.base=60;w.revision=w.revision+1;phase=2;at=now
    elseif phase==2 and state and state.availability[1]=='Ready' and not w.dirty and now-at>20 then
        assert(w:play('song'));phase=3;at=now
    elseif phase==3 and state and state.nativePlaying and output(state,1) and now-at>4 then
        firstSource=output(state,1).id;assert(output(state,0) and firstSource~=output(state,0).id)
        w:selectNote(1,1);assert(w:editSelected(1,{pitch=1}));phase=4;at=now
        log('PASS independent home/player outputs; live note edit queued')
    elseif phase==4 and state and state.nativePlaying and now-at>4 then
        assert(output(state,1).id==firstSource,'live edit changed band identity')
        w:stop(true);assert(w:save());savedBytes[1]=w.project.tracks[1].edited.bytes;phase=5;at=now
        log('PASS live replacement kept band destination; Stop/save succeeded')
    elseif phase==5 and not w.dirty and now-at>3 then
        assert(w:load(Format.new()));api.loadProject();phase=6;at=now
    elseif phase==6 and w.project.name=='MIDI edit QA' and now-at>3 then
        assert(not w.song and not w:needsMidi() and w.project.timeline.tempos[2].tempo==750000,'save lost tempo or still requires source')
        assert(w.project.tracks[1].edited.bytes==savedBytes[1] and w.project.tracks[2].edited.bytes==savedBytes[2],'saved note bytes changed')
        assert(w.project.tracks[2].output.slot==1 and w.project.tracks[1].presetId=='b:guitar-clean','save lost sound or routing')
        if state.availability[1]~='Ready' then w.cb.invite(1);return end
        assert(w:play('song'));phase=7;at=now
        log('PASS SQLite reload preserves edits, sound, player routing and tempo without source MIDI')
    elseif phase==7 and state and state.nativePlaying and output(state,1) and now-at>4 then
        w:stop(true);local notes={};for n=1,4096 do notes[n]={n=60+n%12,s=(n-1)*0.25,l=0.125,v=0.8} end
        assert(w:storeNotes(1,notes) and w:storeNotes(2,notes));assert(w:save());phase=8;at=now
    elseif phase==8 and not w.dirty and now-at>3 then
        assert(w:load(Format.new()));api.loadProject();phase=9;at=now
    elseif phase==9 and w.project.name=='MIDI edit QA' and now-at>3 then
        assert(#w:noteData(1)==4096 and #w:noteData(2)==4096,'maximum compact project did not round trip')
        log('PASS 8,192 edited notes round trip over network/SQLite without truncation')
        api.close();phase=20;log('PASS complete')
    end
    if now-start>100 then error('editing probe timed out at phase '..phase) end
end
return {engineHandlers={onFrame=function()
    if phase==-1 then return end
    local ok,err=pcall(run);if not ok then phase=-1;log('FAIL '..tostring(err)) end
end},eventHandlers={Synth_Message=function(data)
    if data.op=='error' or data.op=='scoreError' then log('ERROR '..tostring(data.message)) end
end}}
