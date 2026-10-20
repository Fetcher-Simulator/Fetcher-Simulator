-- Disposable port-25567 integration probe; never installed in normal setup.
local core=require('openmw.core');local self=require('openmw.self');local nearby=require('openmw.nearby')
local types=require('openmw.types');local I=require('openmw.interfaces');local synth=require('openmw.synth');local vfs=require('openmw.vfs')
local Samples=require('scripts.multiplayer.fetcher.procedural-synth.client.samples')
local schema=require('scripts.multiplayer.fetcher.procedural-synth.shared.schema')
local phase,start,at=0,nil,0;local role;local flags={};local refs={}
local paths={'sound/vo/d/m/hlo_dm001.mp3','sound/fetcher/synth/qa-archive.wav','music/explore/mx_explore_1.mp3'}
local function log(s) print('[SynthSampleTest] '..(role or '?')..' '..s) end
local function output(state,slot) for _,o in ipairs(state and state.outputs or {}) do if o.slot==slot then return o end end end
local function activate()
    for _,list in ipairs{nearby.activators,nearby.items} do for _,o in ipairs(list) do
        if o.recordId=='fetcher_procedural_synth_v1' or o.recordId=='fetcher_procedural_keyboard_v1' then o:activateBy(self);return true end
    end end
end
local function run()
    local now=core.getRealTime();start=start or now;role=role or types.NPC.record(self).name
    local api=I.ProceduralSynth;if not api then return end
    local state=api.status()[1];local w=api.workstation()
    if phase==9 and output(state,1) and output(state,1).peak>.0001 then flags.continued=true end
    if role=='Synth B' then
        if api.performer().invitation then api.readyInstrument('lute');api.respondToInvite(true) end
        for slot=1,2 do local out=output(state,slot)
            if out and out.voices>0 and out.peak>0.0001 and not flags[slot] then
                flags[slot]=true;log('PASS matching sample audible at '..(slot==1 and 'consenting player lute' or 'owned NPC drum'))
            end
        end
        if flags[1] and not state and not flags.closed then flags.closed=true;log('PASS Finish releases sample band outputs') end
        return
    end
    if role~='Synth A' or phase==30 then return end
    if phase==0 and now-start>6 then if activate() then phase=1;at=now end
    elseif phase==1 and w then
        assert(synth.version>=12);api.studio();assert(w:newSong(120,4,4,8));w.project.name='Sample QA'
        local ready=true
        for n,path in ipairs(paths) do
            assert(vfs.fileExists(path),'missing QA source '..path)
            local info=synth.sampleInfo(path,n==3 and 30 or 0,1.2)
            if info.status~='Ready' then assert(info.status=='Loading',info.error);ready=false
            else refs[n]=assert(schema.sample{path=path,offset=n==3 and 30 or 0,duration=1.2,root=60,oneShot=n~=3,hash=info.hash}) end
        end
        if not ready then return end
        log('PASS /s voice, archive-only WAV and bounded music excerpt decoded through installed VFS')
        local b=Samples.new(synth,vfs,self);b.path=paths[1];b.offset=0;b.duration=1.2;assert(b:audition());flags.preview=b
        phase=2;at=now
    elseif phase==2 and now-at>1.5 then
        flags.preview:stop();assert(w:setSample(1,refs[1]));assert(w:addNote(1,60,0) and w:addNote(1,64,0) and w:addNote(1,67,4))
        w:selectAllNotes(1);assert(w:captureClip(1));assert(w:changeRegion(1,1,{l=64}))
        assert(w:add('piano'));assert(w:setSample(2,refs[2]));assert(w:addNote(2,60,2))
        assert(w:add('piano'));assert(w:setSample(3,refs[3]));assert(w:addNote(3,60,8))
        assert(w:setMember(1,{name='Sample player',level=1,mute=false,target={kind='player',characterId=1002,instrument='lute'}}))
        assert(w:setOutput(1,1));w.cb.hire(2);phase=3;at=now
    elseif phase==3 and w.project.band[2].target and state.availability[1]=='Ready' then
        local t=w.project.band[2].target
        assert(w:setMember(2,{name='Sample NPC',level=1,mute=false,target={kind='npc',mpNum=t.mpNum,refId=t.refId,instrument='drum'}}))
        assert(w:setOutput(2,2));assert(w:setComposition{loop=true,loopStart=1,loopEnd=4});phase=4;at=now
    elseif phase==3 and state.availability[1]=='Invite required' and now-at>5 then w.cb.invite(1);at=now
    elseif phase==4 and state.availability[2]=='Ready' and now-at>1 then assert(w:play('song'));phase=5;at=now
    elseif phase==5 and state.nativePlaying and now-at>5 then
        assert(output(state,1).voices>0 or state.voices>0);log('PASS sample chord/linked clips on native clock with player/NPC outputs')
        w:stop(true);w.selected=1;assert(w:editClip(1,1));w.page='record';w.revision=w.revision+1
        assert(w:startTake(1));phase=6;at=now
    elseif phase==6 and now-at>2.2 then assert(w:recordOn(72,.8));phase=7;at=now
    elseif phase==7 and now-at>.4 then
        w:recordOff(72);assert(w:endTake(false));assert(#w:noteData(1)==4);assert(w:leaveClip())
        log('PASS recorded sample note retained in linked source');assert(w:save());phase=8;at=now
    elseif phase==8 and not w.dirty and now-at>2 then
        local changed=schema.state(w.project.tracks[2].params);changed.sample.hash=string.rep('b',32)
        w.project.tracks[2].params=changed;w:configure(2)
        assert(w:add('piano'));w.project.tracks[4].params=schema.state{sample={path='sound/missing-synth-qa.wav',offset=0,duration=1,root=60,oneShot=true,hash=string.rep('c',32)}}
        assert(w:addNote(4,60,0));w:configure(4);phase=81;at=now
    elseif phase==81 and w.cb.sampleStatus(w.project.tracks[4].params.sample).status=='Unavailable' then
        assert(w:play('song'));phase=9;at=now
    elseif phase==9 and state.nativePlaying and now-at>4 then
        assert(output(state,2).voices==0 and flags.continued,'unavailable part interrupted another output')
        log('PASS mismatch and missing sample silent; matching player part continues');w:stop(true);api.loadProject();phase=10;at=now
    elseif phase==10 and #w.project.tracks==3 and now-at>2 then
        assert(w.project.tracks[1].params.sample.hash==refs[1].hash and #w.project.tracks[1].arrangement.clips==1)
        log('PASS sample references, identity, clip notes and band routes round trip through server SQLite')
        api.sound(1);api.presetCommand('saveAs',{name='Sample voice QA',params=w.project.tracks[1].params});api.studio()
        assert(w:play('song'));phase=11;at=now
    elseif phase==11 and state.nativePlaying and now-at>3 then
        flags.switch=1;phase=110;at=now
    elseif phase==110 then
        local duration=.1+flags.switch*.001;local info=synth.sampleInfo(paths[1],0,duration)
        assert(info.status=='Loading' or info.status=='Ready',info.error)
        if info.status=='Ready' then
            local b=Samples.new(synth,vfs,self);b.path=paths[1];b.offset=0;b.duration=duration;assert(b:audition())
            flags.preview=b;phase=111;at=now
        end
    elseif phase==111 and now-at>.05 then
        flags.preview:stop();flags.switch=flags.switch+1
        if flags.switch<=40 then phase=110;return end
        log('PASS forty excerpt loads/switches/previews while native band transport continues');phase=112
    elseif phase==112 then
        w:stop(true);local songs=api.midiSongs();for _,path in ipairs(songs) do if path:find('greensleeves',1,true) then api.importMidi(path);flags.midi=path;break end end
        assert(flags.midi,'QA MIDI missing');phase=12;at=now
    elseif phase==12 and w.song and w.project.midi==flags.midi and now-at>1 then
        assert(w:setSample(1,refs[1]));assert(w:play('song'));phase=13;at=now
    elseif phase==13 and state.nativePlaying and now-at>4 then
        log('PASS imported MIDI plays assigned voice sample');api.close();phase=14;at=now
    elseif phase==14 and now-at>1 then if activate() then phase=15 end
    elseif phase==15 and w then api.studio();api.loadProject();phase=16;at=now
    elseif phase==16 and w.project.name=='Sample QA' and now-at>2 then
        w.selected=1;w.page='samples';w.revision=w.revision+1;phase=30;log('PASS complete; Samples page open for visual review')
    end
    if now-start>180 then error('sample probe timeout phase '..phase) end
end
return {engineHandlers={onFrame=function() if phase==-1 then return end;local ok,err=pcall(run);if not ok then phase=-1;log('FAIL '..tostring(err)) end end},
    eventHandlers={Synth_Message=function(d) if d.op=='error' or d.op=='scoreError' then log('ERROR '..tostring(d.message)) end end}}
