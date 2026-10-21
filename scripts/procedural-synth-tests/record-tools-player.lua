local core=require('openmw.core');local self=require('openmw.self');local nearby=require('openmw.nearby');local types=require('openmw.types')
local I=require('openmw.interfaces');local synth=require('openmw.synth');local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local MIDI=require(prefix..'client.midi');local Format=require(prefix..'shared.project')(require(prefix..'shared.schema'))
local Export=require(prefix..'client.midi-export')
local phase,start,at,role=0,nil,0,nil;local flags={}
local function log(s) print('[SynthRecordToolsTest] '..(role or '?')..' '..s) end
local function activate() for _,list in ipairs{nearby.activators,nearby.items} do for _,o in ipairs(list) do
    if o.recordId=='fetcher_procedural_synth_v1' or o.recordId=='fetcher_procedural_keyboard_v1' then o:activateBy(self);return end
end end end
local function run()
    local now=core.getRealTime();start=start or now;role=role or types.NPC.record(self).name
    local api=I.ProceduralSynth;if not api then return end;local state=api.status()[1]
    if role=='Synth B' then
        if now-start>8 and now-start<10 and state and not state.nativePlaying and not flags.silent then
            flags.silent=true;log('PASS observer has no native click score during local count-in')
        end
        if state and state.nativePlaying and state.voices>0 and not flags.audio then
            flags.audio=true;assert(state.nativeFault==0);log('PASS observer hears recorded musical part on native song clock; fault=0')
        end
        return
    end
    if role~='Synth A' or phase==99 then return end
    local w=api.workstation();local l=api.projectLibrary();local r=w and w.recorder
    if phase==0 and now-start>6 then
        if not w then activate() elseif l.ready and not l.pending then
            api.studio();assert(synth.version>=15 and r)
            local saved;for _,e in ipairs(l.entries) do if e.name=='Recording tools QA' then saved=e end end
            if saved then assert(l:open(saved.id,false,true));phase=80;at=now else
                assert(w:newSong(120,4,4,2));assert(w:setComposition{loop=true});w.snap=0
                assert(r:set('countIn',2) and r:set('metronome',true) and r:set('latency',80))
                local devices=synth.midiDevices();log('PASS native MIDI backend enumerates '..#devices..' connected input(s); backend='..tostring(synth.midiAvailable))
                assert(not pcall(synth.openMidiInput,4294967295));log('PASS unavailable MIDI device rejects safely without opening a port')
                assert(w:startTake(1));flags.starts=w.recording.starts;flags.clock=r.clock;phase=1;at=now
            end
        end
    elseif w and l and not l.pending then
        if phase==1 and now>flags.starts+.23 then
            assert(r.clock.playing and r.clock.strikes>=8 and r.clock.fault==0)
            assert(w:recordOn(60,.8));phase=2;at=now
            log('PASS two-bar audible native count-in reaches capture origin without a fault')
        elseif phase==2 and now-at>.32 then
            w:recordOff(60);assert(w:endTake(false));assert(flags.clock.closed and #w:noteData(1)==1)
            local note=w:noteData(1)[1];assert(note.s>=0 and note.s<4 and note.l>0)
            assert(l:save(true,'Recording tools QA'));phase=3;at=now
            log('PASS compensated native-clock take commits one note and closes count-in stream')
        elseif phase==3 and now-at>1 then
            local midi=MIDI.parse(Export.encode(w.project,nil,MIDI,Format));local count=0
            for _,notes in pairs(MIDI.noteSpans(midi)) do count=count+#notes end
            assert(count==1);assert(w:play('song'));w.page='recordsetup';w.revision=w.revision+1;phase=4;at=now
            log('PASS MIDI export contains musical take only; local clicks never enter the document')
        elseif phase==4 and now-at>3 and state and state.nativePlaying then
            assert(r.clockKind=='play' and r.clock.playing and r.clock.fault==0)
            log('PASS playback click joins audible native song clock')
            assert(r:beginCalibration());phase=5;at=now
        elseif phase==5 and now-at>2 then
            assert(r.calibrating and r.clock.playing and r.clock.strikes>=4 and r.clock.fault==0);r:stopClock()
            phase=99;w.page='recordsetup';w.revision=w.revision+1;log('PASS native tap-calibration click starts/stops locally; READY Recording setup UI')
        elseif phase==80 and now-at>1 then
            assert(#w:noteData(1)==1)
            if r.settings.countIn~=2 or r.settings.latency~=80 or not r.settings.metronome then
                flags.prepare=true;assert(r:set('countIn',2) and r:set('latency',80) and r:set('metronome',true))
            end
            assert(r.settings.countIn==2 and r.settings.latency==80 and r.settings.metronome)
            assert(w:play('song'));w.page='recordsetup';w.revision=w.revision+1;phase=81;at=now
            log(flags.prepare and 'PASS setting up clean-exit preference persistence check' or 'PASS musical take and local recording preferences survive server/client restart')
        elseif phase==81 and now-at>3 and state and state.nativePlaying then
            assert(r.clockKind=='play' and r.clock.playing and r.clock.fault==0);phase=99
            if flags.prepare then log('PASS native song/click replay; requesting clean engine exit for local-settings persistence');core.quit()
            else log('PASS restarted song/click playback; READY Recording setup UI') end
        end
    end
    if now-start>100 then error('record-tools probe timeout phase '..phase) end
end
return {engineHandlers={onFrame=function() if phase==-1 then return end;local ok,err=pcall(run);if not ok then phase=-1;log('FAIL '..tostring(err)) end end}}
