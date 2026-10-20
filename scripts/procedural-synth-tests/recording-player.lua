local core=require('openmw.core');local nearby=require('openmw.nearby');local self=require('openmw.self');local types=require('openmw.types');local I=require('openmw.interfaces')
local Format=require('scripts.multiplayer.fetcher.procedural-synth.shared.project')(require('scripts.multiplayer.fetcher.procedural-synth.shared.schema'))
local phase,start,at=0,nil,0;local role;local flags={};local expected
local function log(s) print('[SynthRecordingTest] '..role..' '..s) end
local function run()
    local now=core.getRealTime();start=start or now;role=role or types.NPC.record(self).name
    local api=I.ProceduralSynth;if not api then return end
    local w=api.workstation();local state=api.status()[1]
    if role=='Synth B' then
        local p=api.performer();if p.invitation then api.readyInstrument('lute');api.respondToInvite(true) end
        for _,o in ipairs(state and state.outputs or {}) do if o.slot==1 and o.voices>0 and not flags.heard then flags.heard=true;log('PASS live recording monitored at consenting player instrument') end end
        if flags.heard and not state and not flags.closed then flags.closed=true;log('PASS Finish releases all recording/playback streams') end
        return
    end
    if role~='Synth A' or phase==20 then return end
    if phase==0 and now-start>6 then
        for _,list in ipairs{nearby.activators,nearby.items} do for _,o in ipairs(list) do
            if o.recordId=='fetcher_procedural_synth_v1' or o.recordId=='fetcher_procedural_keyboard_v1' then o:activateBy(self);phase=1;break end
        end;if phase==1 then break end end
    elseif phase==1 and w then
        api.studio();assert(api.selectMidi('midi/qa/recording-a.mid'));assert(api.playSelected());phase=2
    elseif phase==2 and w.project.midi=='midi/qa/recording-a.mid' and state.nativePlaying then
        assert(w.song.events[1].note==60);w.page='band';w.revision=w.revision+1
        assert(api.selectMidi('midi/qa/recording-b.mid'));assert(api.playSelected());phase=3
    elseif phase==3 and w.project.midi=='midi/qa/recording-b.mid' and state.nativePlaying then
        assert(w.song.events[1].note==72);log('PASS selected song B replaces A on Band without Finish')
        w:stop(true);assert(w:load(Format.new()));w:add('piano');w:length(32);w.project.name='Recording QA'
        w:setMember(1,{name='Player lute',level=1,mute=false,target={kind='player',characterId=1002,instrument='lute'}});w:setOutput(1,1)
        w.page='record';w.revision=w.revision+1;phase=4;at=now
    elseif phase==4 and state.availability[1]=='Ready' and now-at>2 then assert(w:startTake(1));phase=5;at=now
    elseif phase==5 and now-at>2.15 then assert(w:recordOn(60,0.8));assert(w:recordOn(64,0.7));phase=6;at=now
    elseif phase==6 and now-at>0.4 then
        w:recordOff(60);w:recordOff(64);assert(w:endTake(false));assert(#w:noteData(1)==2)
        assert(w:undo() and #w:noteData(1)==0 and w:redo() and #w:noteData(1)==2)
        log('PASS count-in chord recorded as one undoable quantized take')
        assert(w:play('clips'));phase=7;at=now
    elseif phase==7 and state.nativePlaying and now-at>0.4 then assert(w:startTake(1));assert(w:recordOn(60,0.9));phase=8;at=now
    elseif phase==8 and now-at>0.35 then
        w:recordOff(60);w:endTake(false);assert(#w:noteData(1)==3)
        assert(w:startTake(1));w:recordOn(67,1);w:endTake(true);assert(#w:noteData(1)==3)
        log('PASS native-clock overdub and Cancel preserve arrangement/routing')
        w:stop(true);expected=Format.project(w.project);assert(w:save());phase=9;at=now
    elseif phase==9 and not w.dirty and now-at>3 then w:load(Format.new());api.loadProject();phase=10;at=now
    elseif phase==10 and w.project.name=='Recording QA' and now-at>3 then
        assert(#w:noteData(1)==3 and w.project.tracks[1].output.slot==1,'take or player routing lost on SQLite load')
        for n,note in ipairs(w:noteData(1)) do assert(note.s==expected.tracks[1].notes[n].s and note.l==expected.tracks[1].notes[n].l,'quantized timing changed') end
        log('PASS recorded notes and band routing round trip through SQLite');api.close();phase=20;log('PASS complete')
    end
    if now-start>100 then error('recording probe timeout phase '..phase) end
end
return {engineHandlers={onFrame=function() if phase==-1 then return end;local ok,err=pcall(run);if not ok then phase=-1;log('FAIL '..tostring(err)) end end},
    eventHandlers={Synth_Message=function(d) if d.op=='error' or d.op=='scoreError' then log('ERROR '..tostring(d.message)) end end}}
