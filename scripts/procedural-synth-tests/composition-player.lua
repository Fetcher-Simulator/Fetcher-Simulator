local core=require('openmw.core');local nearby=require('openmw.nearby');local self=require('openmw.self');local types=require('openmw.types');local I=require('openmw.interfaces')
local Format=require('scripts.multiplayer.fetcher.procedural-synth.shared.project')(require('scripts.multiplayer.fetcher.procedural-synth.shared.schema'))
local phase,start,at=0,nil,0;local role;local flags={};local expected
local function log(s) print('[SynthCompositionTest] '..(role or '?')..' '..s) end
local function run()
    local now=core.getRealTime();start=start or now;role=role or types.NPC.record(self).name
    local api=I.ProceduralSynth;if not api then return end
    local w=api.workstation();local state=api.status()[1]
    if role=='Synth B' then
        local p=api.performer();if p.invitation then api.readyInstrument('lute');api.respondToInvite(true) end
        for _,o in ipairs(state and state.outputs or {}) do if o.slot==1 and o.voices>0 and not flags.heard then flags.heard=true;log('PASS source-free song heard at consenting player instrument') end end
        if flags.heard and not state and not flags.closed then flags.closed=true;log('PASS Finish releases remote song streams') end
        return
    end
    if role~='Synth A' or phase==20 then return end
    if phase==0 and now-start>6 then
        for _,list in ipairs{nearby.activators,nearby.items} do for _,o in ipairs(list) do
            if o.recordId=='fetcher_procedural_synth_v1' or o.recordId=='fetcher_procedural_keyboard_v1' then o:activateBy(self);phase=1;break end
        end;if phase==1 then break end end
    elseif phase==1 and w then
        api.studio();assert(w:newSong(120,3,4,8));w.project.name='Composition QA'
        assert(w:addNote(1,60,12));assert(w:addPart('snare'));assert(w:addNote(2,38,16))
        assert(w:setMember(1,{name='Player lute',level=1,mute=false,target={kind='player',characterId=1002,instrument='lute'}}))
        assert(w:setOutput(1,1) and w:setOutput(2,1));assert(w:setComposition{loop=true,loopStart=2,loopEnd=3})
        log('PASS new song, independent drum voice, meter and loop region created');phase=2;at=now
    elseif phase==2 and state.availability[1]=='Ready' and now-at>2 then
        assert(api.playSelected());phase=3;at=now
    elseif phase==2 and state.availability[1]=='Invite required' and now-at>5 then
        w.cb.invite(1);at=now
    elseif phase==3 and state.nativePlaying and now-at>7 then
        assert(w.beat>=3 and w.beat<9,'region playhead outside authored timeline')
        if w.beat>8 and w.beat<8.7 then
            assert(w:startTake(1));assert(w:recordOn(67,0.8));phase=4;at=now
            log('PASS selected region loops on the native clock without a MIDI file')
        end
    elseif phase==4 and not w.recording and now-at>0.3 then
        local notes=w:noteData(1);assert(#notes==2 and notes[2].s+notes[2].l==36,'loop-wrap capture lost or exceeded region end')
        log('PASS native loop-wrap overdub commits/releases held notes')
        w:stop(true);assert(w:setComposition{bars=2,loopEnd=2});assert(#w:noteData(1)==2)
        assert(w:setComposition{bars=8,loopEnd=3});expected=Format.project(w.project);assert(w:save());phase=5;at=now
    elseif phase==5 and not w.dirty and now-at>3 then
        assert(w:load(Format.new()));api.loadProject();phase=6;at=now
    elseif phase==6 and w.project.name=='Composition QA' and now-at>3 then
        assert(w.project.midi=='' and w.project.composition.numerator==3 and w.project.composition.loopStart==2)
        assert(w.project.tracks[1].edited.bytes==expected.tracks[1].edited.bytes and w.project.tracks[2].output.slot==1)
        log('PASS authored notes, meter, bounds and band routing round trip through SQLite')
        assert(api.playSelected());phase=7;at=now
    elseif phase==7 and state.nativePlaying and now-at>2 then
        log('PASS saved source-free song replays after load')
        assert(w:newSong(240,1,16,16));assert(w:addNote(1,60,0));assert(w:setComposition{loop=true});assert(api.playSelected());phase=11;at=now
    elseif phase==11 and state.nativePlaying and now-at>2 then
        log('PASS shortest supported meter uploads and plays with a 3000-frame quantum')
        assert(w:newSong(40,12,2,2));assert(w:addNote(1,60,0));assert(api.playSelected());phase=12;at=now
    elseif phase==12 and state.nativePlaying and now-at>2 then
        log('PASS longest supported bar uploads and plays with a 1728000-frame quantum')
        api.close();phase=8;at=now
    elseif phase==8 and now-at>1 then
        -- Leave the Song page available for a visual layout check after cleanup.
        for _,list in ipairs{nearby.activators,nearby.items} do for _,o in ipairs(list) do
            if o.recordId=='fetcher_procedural_synth_v1' or o.recordId=='fetcher_procedural_keyboard_v1' then o:activateBy(self);phase=9;break end
        end;if phase==9 then break end end
    elseif phase==9 and w then
        api.studio();api.loadProject();phase=10;at=now
    elseif phase==10 and w.project.name=='Composition QA' and now-at>2 then
        w.page='song';w.revision=w.revision+1;phase=20;log('PASS complete; Song page open for visual review')
    end
    if now-start>110 then error('composition probe timeout phase '..phase) end
end
return {engineHandlers={onFrame=function() if phase==-1 then return end;local ok,err=pcall(run);if not ok then phase=-1;log('FAIL '..tostring(err)) end end},
    eventHandlers={Synth_Message=function(d) if d.op=='error' or d.op=='scoreError' then log('ERROR '..tostring(d.message)) end end}}
