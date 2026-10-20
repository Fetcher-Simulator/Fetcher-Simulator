local core=require('openmw.core');local nearby=require('openmw.nearby');local self=require('openmw.self');local types=require('openmw.types');local I=require('openmw.interfaces')
local Format=require('scripts.multiplayer.fetcher.procedural-synth.shared.project')(require('scripts.multiplayer.fetcher.procedural-synth.shared.schema'))
local phase,start,at=0,nil,0;local role;local flags={};local expected
local function log(s) print('[SynthArrangementTest] '..(role or '?')..' '..s) end
local function activate()
    for _,list in ipairs{nearby.activators,nearby.items} do for _,o in ipairs(list) do
        if o.recordId=='fetcher_procedural_synth_v1' or o.recordId=='fetcher_procedural_keyboard_v1' then o:activateBy(self);return true end
    end end
end
local function run()
    local now=core.getRealTime();start=start or now;role=role or types.NPC.record(self).name
    local api=I.ProceduralSynth;if not api then return end
    local w=api.workstation();local state=api.status()[1]
    if role=='Synth B' then
        local p=api.performer();if p.invitation then api.readyInstrument('lute');api.respondToInvite(true) end
        for _,o in ipairs(state and state.outputs or {}) do if o.slot==1 and o.voices>0 and not flags.heard then flags.heard=true;log('PASS arranged source heard at consenting player instrument') end end
        if flags.heard and not state and not flags.closed then flags.closed=true;log('PASS Finish releases arranged playback outputs') end
        return
    end
    if role~='Synth A' or phase==20 then return end
    if phase==0 and now-start>6 then if activate() then phase=1 end
    elseif phase==1 and w then
        api.studio();assert(w:newSong(120,4,4,8));w.project.name='Arrangement QA'
        assert(w:addNote(1,60,0) and w:addNote(1,64,4));w:selectAllNotes(1);assert(w:captureClip(1))
        assert(w:duplicateRegion(1,1));assert(w:changeRegion(1,2,{s=32,l=32,transpose=12}))
        assert(w:addPart('snare') and w:addNote(2,38,4));w:selectAllNotes(2);assert(w:captureClip(2));assert(w:changeRegion(2,1,{l=64}))
        assert(w:setMember(1,{name='Player lute',level=1,mute=false,target={kind='player',characterId=1002,instrument='lute'}}))
        assert(w:setOutput(1,1));assert(w:setComposition{loop=true,loopStart=1,loopEnd=4});w.page='arrange';w.selected=1;w.arrangeClip=1;w.revision=w.revision+1
        log('PASS linked melodic/drum clips, repeat, move and transpose created');phase=2;at=now
    elseif phase==2 and state.availability[1]=='Ready' and now-at>2 then
        assert(api.playSelected());phase=3;at=now
    elseif phase==2 and state.availability[1]=='Invite required' and now-at>5 then w.cb.invite(1);at=now
    elseif phase==3 and state.nativePlaying and now-at>8 then
        log('PASS arranged parts loop on the shared native clock through distinct outputs')
        assert(w:editClip(1,1));w:selectAllNotes(1);assert(w:editSelected(1,{pitch=1}));assert(w:play('song'));phase=4;at=now
    elseif phase==4 and state.nativePlaying and now-at>2 then
        w:selectAllNotes(1);assert(w:editSelected(1,{pitch=1}));phase=5;at=now
    elseif phase==5 and state.nativePlaying and now-at>3 then
        assert(w:noteData(1)[1].n==62);log('PASS shared source edit replaces the running arranged score')
        w:stop(true);w.rollStart=0;w.page='record';w.revision=w.revision+1;assert(w:startTake(1));phase=6;at=now
    elseif phase==6 and now-at>2.2 then assert(w:recordOn(67,0.8));phase=7;at=now
    elseif phase==7 and now-at>0.4 then
        w:recordOff(67);assert(w:endTake(false));assert(#w:noteData(1)==3);assert(w:leaveClip())
        log('PASS stopped recording updates the reusable source for every placement')
        local dense={};for n=1,4094 do dense[n]={n=60,s=0,l=1,v=0.8} end
        local data=assert(Format.encodeNotes(dense,960))
        local id=assert(w:newClip(1));assert(w:setClip(1,id,{name='Unused capacity A',notes=data}))
        id=assert(w:newClip(1));assert(w:setClip(1,id,{name='Unused capacity B',notes=data}))
        assert(Format.savedNotes(w.project)==8192);expected=Format.project(w.project);assert(w:save());phase=8;at=now
    elseif phase==8 and not w.dirty and now-at>3 then
        assert(w:load(Format.new()));api.loadProject();phase=9;at=now
    elseif phase==9 and w.project.name=='Arrangement QA' and now-at>3 then
        local t=w.project.tracks[1]
        assert(Format.savedNotes(w.project)==8192 and t.arrangement.clips[1].notes.bytes==expected.tracks[1].arrangement.clips[1].notes.bytes)
        assert(#t.arrangement.regions==2 and t.arrangement.regions[2].transpose==12 and t.output.slot==1)
        log('PASS 8192 source notes, linked placements and band routing round trip through native wire/SQLite')
        assert(api.playSelected());phase=10;at=now
    elseif phase==10 and state.nativePlaying and now-at>3 then
        log('PASS saved clip arrangement replays without a source MIDI file');api.close();phase=11;at=now
    elseif phase==11 and now-at>1 then if activate() then phase=12 end
    elseif phase==12 and w then api.studio();api.loadProject();phase=13;at=now
    elseif phase==13 and w.project.name=='Arrangement QA' and now-at>2 then
        w.page='arrange';w.selected=1;w.arrangeClip=1;w.arrangeBar=0;w.revision=w.revision+1;phase=20;log('PASS complete; arranger open for visual review')
    end
    if now-start>150 then error('arrangement probe timeout phase '..phase) end
end
return {engineHandlers={onFrame=function() if phase==-1 then return end;local ok,err=pcall(run);if not ok then phase=-1;log('FAIL '..tostring(err)) end end},
    eventHandlers={Synth_Message=function(d) if d.op=='error' or d.op=='scoreError' then log('ERROR '..tostring(d.message)) end end}}
