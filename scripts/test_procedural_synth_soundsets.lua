local root='files/server/server-lua-packages/fetcher.procedural-synth/';local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=dofile(root..'shared/schema.lua');package.loaded[prefix..'shared.schema']=schema
local format=dofile(root..'shared/project.lua')(schema);package.loaded[prefix..'shared.project']=dofile(root..'shared/project.lua')
local MIDI=dofile(root..'client/midi.lua');package.loaded[prefix..'client.midi']=MIDI
local sets=dofile(root..'client/soundsets.lua');package.loaded[prefix..'client.soundsets']=sets
package.loaded[prefix..'shared.sequence']=dofile(root..'shared/sequence.lua')
local W=dofile(root..'client/workstation.lua');local checks=0
local function check(v,m) checks=checks+1;assert(v,m) end
for program=0,127 do check(type(sets.role{channel=0,program=program})=='string','all GM programs mapped') end
check(sets.role{channel=0,program=0,name='Bass'}=='keys','explicit program beats a misleading track name')
check(sets.role{channel=1,name='Bass'}=='bass' and sets.role{channel=2,name='Violin'}=='strings','names help when program metadata is absent')
for _,s in ipairs(sets.entries) do
    for _,role in ipairs{'keys','mallet','organ','guitar','bass','strings','brass','reed','pipe','lead','pad','fx','pluck','kick','snare','hat','openhat','tom'} do
        local state,id=sets.assignment(s.id,role,true,50)
        check(state and schema.state(state) and id,'every role in every set resolves to a valid built-in')
    end
end
local scheduled=0;local message
local w=W.new{configure=function() end,prune=function() end,effects=function() end,panic=function() end,
    stopSequence=function() end,progress=function() return true,0,0 end,schedule=function() scheduled=scheduled+1;return true end,
    message=function(s) message=s end,save=function() end,importMidi=function() end}
local song={tracks=5,division=96,names={},programs={},tempos={},duration=2,beats=4,events={}}
for n,entry in ipairs{{0,60,0},{1,36,32},{2,60,24},{3,64,40},{9,36},{9,42},{9,50}} do
    local ch,pitch,program=entry[1],entry[2],entry[3];local part=n..':'..ch..(ch==9 and ':'..pitch or '')
    song.programs[part]=program
    for _,kind in ipairs{'on','off'} do song.events[#song.events+1]={kind=kind,part=part,track=n,channel=ch,note=pitch,velocity=0.8,tick=kind=='on' and 0 or 96,time=kind=='on' and 0 or 0.5} end
end
table.sort(song.events,function(a,b)return a.time<b.time end)
check(w:import(song,'midi/test.mid'),'GM import succeeds')
local byRole={};for n,t in ipairs(w.project.tracks) do byRole[sets.role(MIDI.parts(song)[n])]=t end
check(byRole.keys.presetId=='b:studio-keys' and byRole.guitar.presetId=='b:guitar-clean' and byRole.strings.presetId=='b:studio-strings','melodic parts no longer copy the manual patch')
check(byRole.bass.presetId=='b:jazz-bass' and byRole.bass.params.tune1==0,'MIDI bass uses the written octave')
check(byRole.tom.params.drumType==1 and byRole.tom.params.bodyFrequency==220,'GM high tom uses a pitched body instead of snare noise')
local track=w.project.tracks[1];track.level=0.27;track.mute=true;track.mixer.reverbSend=0.6;w:touch('mix',true)
local part=track.part;local note=track.note;local effects=w.project.effects
check(w:play('song') and w.native,'native song starts')
local history=#w.undoStack;local count=#w.project.tracks
check(w:applySoundSet('rock') and w.project.soundSet=='rock' and #w.undoStack==history+1,'whole arrangement is one undo action')
check(w.playing and w.native and scheduled==1 and not w.sequenceDirty,'live set change does not stop or recompile the native score')
check(#w.project.tracks==count and track.part==part and track.note==note and track.level==0.27 and track.mute and track.mixer.reverbSend==0.6 and w.project.effects==effects,'notes, routes, levels, mute, sends and returns preserved')
check(w:undo() and w.project.soundSet=='balanced' and w:redo() and w.project.soundSet=='rock','undo/redo restores all sound snapshots')
local saved=format.project(w.project);check(saved and saved.soundSet=='rock' and w:load(saved),'set selection and instrument snapshots round-trip')
local old={};for k,v in pairs(saved) do if k~='soundSet' then old[k]=v end end
check(format.project(old).soundSet=='','old projects retain snapshots and migrate to Custom')
w:preset(1,'b:flute');check(w.project.soundSet=='','individual preset edit marks set Custom')
check(w:cycleSoundSet(1) and w.project.soundSet=='balanced','cycle from Custom starts Balanced')
check(w:import(song,'midi/test.mid') and w.project.soundSet=='balanced','same-file import retains assignments')
local before=w.project.tracks[1].presetId;w:preset(1,'b:glass-bell');w:import(song,'midi/test.mid')
check(w.project.tracks[1].presetId=='b:glass-bell','manual per-part sound survives same-file reimport')
local copy={};for k,v in pairs(saved) do copy[k]=v end;copy.soundSet={}
check(not format.project(copy),'malformed marker rejected')
w.song=nil;check(not w:applySoundSet('metal') and message,'missing imported MIDI metadata cannot partially replace a project')
print('PASS GM roles, seven complete sound sets, live audition, mix preservation, migration and undo; '..checks..' assertions')
