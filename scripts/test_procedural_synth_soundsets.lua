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
    for _,role in ipairs{'keys','mallet','organ','guitar','bass','strings','brass','reed','pipe','lead','pad','fx','pluck','kick','snare','hat','openhat','tom','pedalhat','rim','clap','crash','ride','ridebell','cowbell','tambourine','shaker','percussion'} do
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
check(byRole.keys.presetId=='b:studio-keys' and byRole.guitar.presetId=='b:guitar-nylon' and byRole.strings.presetId=='b:studio-strings','GM nylon and other melodic parts receive their own sound family')
check(byRole.bass.presetId=='b:bass-acoustic' and byRole.bass.params.tune1==0,'GM acoustic bass uses the written octave')
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
for note,role in pairs{[37]='rim',[39]='clap',[44]='pedalhat',[49]='crash',[51]='ride',[52]='crash',[53]='ridebell',[54]='tambourine',[55]='crash',[56]='cowbell',[57]='crash',[59]='ride',[70]='shaker',[76]='percussion'} do
 check(sets.role{channel=9,note=note}==role,'GM percussion identity '..note)
 local state,id=sets.assignment('rock',role,true,note)
 check(state.drumType>0 and id~='b:rock-snare' and format.instrumentFor(role,47)=='drum','percussion keeps its sound and drummer despite channel kit program '..note)
end
local state,id=sets.programPatch({soundSet='thrash'},{sourceRole='ride',note=51,instrument='drum'},47)
check(id=='b:kit-ride' and state.drumType==4,'percussion program changes cannot turn the kit into a melodic GM instrument')
for _,style in ipairs{'balanced','classic-rock','thrash','alternative-metal','funk','acoustic','orchestra'} do
 for program,id in pairs{[24]='guitar-nylon',[25]='guitar-steel',[27]='guitar-clean',[33]='bass-finger',[34]='bass-picked',[36]='bass-slap',[37]='bass-pop'} do
  local role=sets.role{channel=0,program=program};local state,preset=sets.assignment(style,role,true,nil,'lute',program)
  check(preset=='b:'..id and (role~='bass' or state.tune1==0),'GM family survives style '..style..' / '..program)
 end
end
local f=W.new(w.cb);check(f:newSong(120,4,4,8),'full kit test starts an authored song')
local count=#f.project.tracks;local history=#f.undoStack
check(f:addKit(6) and #f.project.tracks==count+10 and #f.undoStack==history+1,'full kit adds ten lanes atomically in one undo step')
local notes={};for _,t in ipairs(f.project.tracks) do if t.kind=='drum' then notes[t.note]=t end end
check(notes[50].params.bodyFrequency==220 and notes[43].params.bodyFrequency==105 and notes[51].presetId=='b:kit-ride' and notes[49].presetId=='b:kit-crash','kit tom pitches and cymbal identities differ')
local Layout=dofile(root..'shared/band-layout.lua');f.project.leaderPlays=false
local plan=Layout.plan(f.project);local drummer=plan.routes[2]
for n=2,#f.project.tracks do check(plan.routes[n]==drummer and plan[drummer].instrument=='drum','complete kit shares one physical drummer') end
check(f:undo() and #f.project.tracks==count and f:redo() and #f.project.tracks==count+10,'whole-kit undo/redo restores every lane')
while #f.project.tracks<23 do f.project.tracks[#f.project.tracks+1]=format.newTrack('piano','Capacity',f.project.length) end
local history=#f.undoStack;check(not f:addKit(8) and #f.project.tracks==23 and #f.undoStack==history,'insufficient track space cannot install a partial kit')
local familySong={tracks=16,division=96,names={},programs={},tempos={},duration=1,beats=2,events={}}
for program=24,39 do
 local n=program-23;local part=n..':0';familySong.programs[part]=program
 for _,kind in ipairs{'on','off'} do familySong.events[#familySong.events+1]={kind=kind,part=part,track=n,channel=0,note=48,velocity=.8,tick=kind=='on' and 0 or 96,time=kind=='on' and 0 or .5} end
end
table.sort(familySong.events,function(a,b)return a.time==b.time and a.track<b.track or a.time<b.time end)
f=W.new(w.cb);check(f:import(familySong,'midi/families.mid'),'all sixteen GM guitar/bass programs import')
for n,t in ipairs(f.project.tracks) do check(t.sourceProgram==n+23 and f:convertPart(n),'conversion retains original MIDI program identity') end
local snapshot=assert(format.project(f.project));f:load(snapshot);f.song=nil
check(f:applySoundSet('funk') and f.project.tracks[1].presetId=='b:guitar-nylon' and f.project.tracks[2].presetId=='b:guitar-steel' and f.project.tracks[11].presetId=='b:bass-picked' and f.project.tracks[13].presetId=='b:bass-slap','source-free saved arrangements keep distinct families when changing sound sets')
local bad=assert(format.project(snapshot));bad.tracks[1].sourceProgram=128;check(not format.project(bad),'invalid saved program identity is rejected')
bad.tracks[1].sourceProgram=24.5;check(not format.project(bad),'fractional saved program identity is rejected')
print('PASS GM guitar/bass families, full kits, percussion routing, saved identities, live sound sets and undo; '..checks..' assertions')
