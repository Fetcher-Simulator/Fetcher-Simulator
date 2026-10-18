local root='files/server/server-lua-packages/fetcher.procedural-synth/'
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=dofile(root..'shared/schema.lua')
local format=dofile(root..'shared/project.lua')(schema)
local midi=dofile(root..'client/midi.lua')
package.loaded[prefix..'shared.schema']=schema
package.loaded[prefix..'shared.project']=dofile(root..'shared/project.lua')
package.loaded[prefix..'client.midi']=midi
local Workstation=dofile(root..'client/workstation.lua')
local checks=0
local function check(v,m) checks=checks+1;assert(v,m) end
local events,config,messages,saved={},{},{},nil
local function copy(t) if type(t)~='table' then return t end;local out={};for k,v in pairs(t) do out[k]=copy(v) end;return out end
local w=Workstation.new{
    currentParams=function() return schema.state({wave1=2}) end,
    configure=function(n,p,level,mixer) config[n]={params=copy(p),level=level,mixer=copy(mixer)} end,
    on=function(lane,note,velocity) events[#events+1]={'on',lane,note,velocity} end,
    off=function(lane,note) events[#events+1]={'off',lane,note} end,
    panic=function() events[#events+1]={'panic'} end,
    save=function(p) saved=copy(p) end,
    importMidi=function(path) messages[#messages+1]=path end,
    message=function(message) messages[#messages+1]=message end,
}
check(#w.catalog==#schema.builtins and #w.catalog>=40,'banks available before asynchronous personal catalog response')
w:setCatalog(schema.builtins)
check(w:add('drum') and w.project.tracks[1].presetId=='b:kick','new drum uses native kick preset')
check(w:audition(1),'drum pad auditions while transport is stopped');w:frame(0.12)
check(events[#events][1]=='off' and not w.playing,'pad audition releases without running transport')
w:toggleStep(1,0);w:toggleStep(1,4)
w:add('piano');w.noteLength=8;w:toggleNote(2,60,0)
check(w.project.tracks[1].steps:sub(1,1)~='0' and w.project.tracks[2].notes[1].l==8,'editable drum/piano clips')
w:preset(2,'b:bass')
check(config[2].params.tune1==-12,'saved timbre assigned per track')
w:setTrack(2,'solo',true)
check(config[1].level==0 and math.abs(config[2].level-0.8)<0.00001,'solo changes mix without overwriting timbres')
w:setTrack(2,'solo',false);w:setTrack(1,'mute',true)
check(config[1].level==0 and w.project.tracks[1].level==0.8,'mute preserves fader setting')
w:setTrack(1,'mute',false);w:setTrack(2,'level',0.3)
check(math.abs(config[2].level-0.3)<0.00001,'independent mixer level')
check(not w:setTrack(2,'level',0/0) and not w:setTrack(2,'level',2),'invalid level rejected')
w:play('clips');w:frame(0)
check(events[#events-1][1]=='on' and events[#events-1][2]==1 and events[#events][2]==2,'drum and piano start together')
for _=1,4 do w:frame(0.125) end
local pianoReleased=false;for _,e in ipairs(events) do if e[1]=='off' and e[2]==2 then pianoReleased=true end end
check(not pianoReleased,'long piano note stays held across drum steps')
for _=1,4 do w:frame(0.125) end
check(events[#events][1]=='off' and events[#events][2]==2,'piano duration releases its own channel')
w:stop();check(events[#events][1]=='panic','stop releases all channels')
w:length(32);check(#w.project.tracks[1].steps==256,'bar length expands pattern without dropping first bar')
w:toggleNote(2,60,2);check(#w.project.tracks[2].notes==0,'click within a note removes it')
w:toggleNote(2,64,16);w:toggleStep(1,20)
w:length(256);w:toggleNote(2,72,255);w:toggleStep(1,255)
w:length(16);check(#w.project.tracks[2].notes==2 and #w.project.tracks[1].steps==256,'shortening only changes playback loop')
w:save();check(w:load(saved),'hidden bars save/load with a short loop')
for length=32,256,16 do check(w:length(length),'all sixteen bar lengths accepted') end
check(w.project.tracks[2].notes[1].s==16 and w.project.tracks[2].notes[2].s==255 and w.project.tracks[1].steps:sub(21,21)~='0' and w.project.tracks[1].steps:sub(256,256)~='0','bar 2 and bar 16 survive cycling and persistence')
check(not w:length(272) and not w:length(17) and not w:length(0/0),'invalid loop lengths rejected')
events={};w:play('clips');w.beat=63.75;w.step=254;w:frame(0)
local lastDrum,lastPiano=false,false
for _,e in ipairs(events) do if e[1]=='on' and e[2]==1 then lastDrum=true elseif e[1]=='on' and e[2]==2 and e[3]==72 then lastPiano=true end end
check(lastDrum and lastPiano,'last step of bar 16 plays drum and piano on their own channels')
w:stop();w:length(16);events={};w:play('clips');w:frame(0)
local hiddenPiano=false;for _,e in ipairs(events) do if e[1]=='on' and e[2]==2 then hiddenPiano=true end end
check(not hiddenPiano,'notes in retained hidden bars do not leak into a short playback loop')
w:stop()
local legacy=copy(saved);legacy.tracks[1].steps=legacy.tracks[1].steps:sub(1,16);legacy.tracks[2].steps=legacy.tracks[2].steps:sub(1,16);legacy.tracks[2].notes={}
local migrated=format.project(legacy);check(migrated and #migrated.tracks[1].steps==256,'existing one-to-four-bar saves migrate without losing their steps')
w.project.swing=0.5;w:toggleStep(1,1);events={};w:play('clips');w:frame(0)
w:frame(0.125)
local on=0;for _,e in ipairs(events) do if e[1]=='on' then on=on+1 end end
check(on==1,'swing delays the odd sixteenth')
w:frame(0.0625);check(events[#events][1]=='on','swung step arrives at delayed beat')
w:frame(1);check(not w.playing and events[#events][1]=='panic','frame stall releases transport')
w:save();check(saved and saved.version==1 and saved.tracks[2].presetId=='b:bass','project contains complete timbres and patterns')
local valid=copy(saved);valid.tracks[1].output={kind='otherPlayer',id=99}
check(not format.project(valid),'unimplemented physical routes cannot be forged')
valid=copy(saved);valid.tracks[1].notes={[2]={n=60,s=0,l=1,v=1}}
check(not format.project(valid),'sparse note arrays rejected')
valid=copy(saved);valid.bpm=math.huge;check(not format.project(valid),'nonfinite transport rejected')
valid=copy(saved);valid.version=2;check(not w:load(valid),'future project rejected')
check(w:load(saved) and not w.dirty,'project round trip restores work without autoplay')
-- Keep independent equal pitches on imported parts, including a single SMF
-- track with several channels, with a MIDI tempo map driving the transport.
local song={tracks=2,division=96,names={[1]='Strings',[2]='Bass'},programs={},tempos={{beat=0,time=0,tempo=500000},{beat=1,time=0.5,tempo=1000000}},duration=1.5,beats=2,events={
    {kind='on',part='1:0',track=1,channel=0,note=60,velocity=1,tick=0,time=0},
    {kind='on',part='2:0',track=2,channel=0,note=60,velocity=0.7,tick=0,time=0},
    {kind='off',part='1:0',track=1,channel=0,note=60,tick=96,time=0.5},
    {kind='off',part='2:0',track=2,channel=0,note=60,tick=192,time=1.5},
}}
w.project.tracks={};w:import(song,'midi/test.mid');w:preset(1,'b:flute');w:preset(2,'b:bass')
events={};w:play('song');w:frame(0)
local starts={};for _,e in ipairs(events) do if e[1]=='on' then starts[#starts+1]=e end end
check(starts[1][2]==1 and starts[2][2]==2 and config[1].params.wave1==0 and config[2].params.wave1==3,'MIDI parts play through different presets')
for _=1,4 do w:frame(0.125) end
check(events[#events][1]=='off' and events[#events][2]==1,'first equal pitch ends independently')
w:frame(0.25);check(math.abs(w.beat-1.25)<0.001,'MIDI tempo change drives musical playhead')
for _=1,3 do w:frame(0.25) end
check(not w.playing and events[#events][1]=='panic','multitimbral MIDI completes with cleanup')
w.project.tracks[3]=format.newTrack('drum','Preview after song',16);w:configure(3)
check(w:audition(3),'pad can audition after MIDI completion')
w:stop();check(events[#events][1]=='panic' and #w.preview==0,'completed MIDI object cannot swallow pad stop/cleanup')
w:setTrack(1,'level',0.2);w:save();local restored=copy(saved)
w:load(restored);check(w.project.tracks[1].level==0.2 and w.project.tracks[2].presetId=='b:bass','project retains MIDI routing and sound snapshots')
local long=copy(song);long.duration=5;long.tempos={};long.events[4].time=5;long.events[4].tick=960
w.song=long;w.project.loop=false;w.project.tracks[3].steps='9'..string.rep('0',15)
events={};w:play('song');for _=1,32 do w:frame(0.125) end
local drumHits=0;for _,event in ipairs(events) do if event[1]=='on' and event[2]==3 then drumHits=drumHits+1 end end
check(w.playing and drumHits==1,'non-looping clips finish once while full MIDI keeps playing')
w:stop()
for _=#w.project.tracks+1,16 do w:add('piano') end
check(not w:add('drum') and #w.project.tracks==16,'track capacity enforced')
local many=copy(song);many.events={};for n=1,17 do many.events[n]={kind='on',part=n..':0',track=n,channel=0,note=60,tick=0,time=0,velocity=1} end
local before=w.project.midi
check(not w:import(many,'midi/too-many.mid') and w.project.midi==before,'oversized MIDI import preserves existing project')
local rejected
rejected=Workstation.new{configure=function() end,on=function() rejected:stop();return false end,off=function() end,panic=function() end,message=function() end}
rejected:add('drum');check(not rejected:audition(1),'rejected audition does not retain an off callback')
rejected:frame(0.2);check(#rejected.preview==0,'rate rejection cannot leave a stale audition counter')
-- History stores musical data, not UI state or audible transport position.
local h=Workstation.new{configure=function() end,prune=function() end,panic=function() end,
    currentParams=function() return schema.state({}) end,message=function() end,save=function() end,importMidi=function() end}
check(h:add('drum','b:snare','Snare',38) and #h.undoStack==1,'adding a configured drum lane is one undo action')
check(h.project.tracks[1].params.drumType==2,'new snare uses dedicated native voice')
h:toggleStep(1,0);check(h:undo() and h.project.tracks[1].steps:sub(1,1)=='0','undo removes a drum step')
check(h:redo() and h.project.tracks[1].steps:sub(1,1)~='0','redo restores step velocity')
h:undo();h:toggleStep(1,4);check(not h:redo(),'new edits discard the abandoned redo branch')
h:add('piano');h:length(32);h:toggleNote(2,64,16);h:length(16)
check(h:undo() and h.project.length==32 and h.project.tracks[2].notes[1].s==16,'undo shortening restores loop length and retains later notes')
check(h:undo() and #h.project.tracks[2].notes==0,'undo removes a piano note')
check(h:redo() and h.project.tracks[2].notes[1].n==64,'redo restores complete piano note')
h:remove(1);check(h:undo() and #h.project.tracks==2 and h.project.tracks[1].name=='Snare','undo deletion restores instrument and routing')
h.project.tracks[1].params.bodyFrequency=200;h:touch('sound:1:bodyFrequency')
h.project.tracks[1].params.bodyFrequency=210;h:touch('sound:1:bodyFrequency')
check(h:undo() and h.project.tracks[1].params.bodyFrequency==190,'continuous sound edits coalesce into one undo gesture')
check(h:redo() and h.project.tracks[1].params.bodyFrequency==210,'redo restores the final sound setting')
for _=1,130 do h:toggleStep(1,1) end
check(#h.undoStack==64,'history memory is bounded to 64 edits')
local before=h.generation;h:undo();check(h.dirty and h.generation>before and not h.playing,'undo is a fresh unsaved edit and stops transport safely')
h:load(format.new());check(h:addKit() and #h.project.tracks==4 and #h.undoStack==1,'complete four-voice kit is one undo action')
check(h.project.tracks[4].params.drumType==4 and h.project.tracks[4].note==46,'kit includes an open hat with standard MIDI mapping')
check(h:undo() and #h.project.tracks==0 and h:redo() and #h.project.tracks==4,'undo and redo restore the whole kit atomically')
local percussion={tracks=1,division=96,names={},programs={},tempos={},duration=1,beats=2,events={}}
for _,note in ipairs{38,46,42} do percussion.events[#percussion.events+1]={kind='on',part='1:9:'..note,track=1,channel=9,note=note,velocity=1,tick=0,time=0} end
h:load(format.new());check(h:import(percussion,'midi/drums.mid'),'GM percussion imports')
check(h.project.tracks[1].params.drumType==2 and h.project.tracks[2].params.drumType==4 and h.project.tracks[3].params.drumType==3,'GM snare, open and closed hats select the correct fixed-tuning engines')
local restored=format.project(h.project);h:load(restored)
check(#h.undoStack==0 and #h.redoStack==0 and not h.dirty,'loading another document starts fresh editing history')
h:load(format.new());check(h:addKit(4) and h.project.tracks[1].presetId=='b:trap-kick' and h.project.tracks[3].presetId=='b:trap-hat','genre kit assigns its own drum starting sounds')
check(h:undo() and #h.project.tracks==0 and h:redo() and h.project.tracks[2].presetId=='b:trap-snare','genre kit creation remains one undo action')
h:load(format.new());h:add('piano');h:preset(1,'b:guitar-crunch')
local guitar=format.project(h.project);h:load(guitar)
check(h.project.tracks[1].params.instrumentType==1 and h.project.tracks[1].params.driveType==1 and h.project.tracks[1].params.cabinet==2,'project retains the complete guitar insert')
h.project.tracks[1].params.drive=30;h:touch('sound:drive');h:undo()
check(h.project.tracks[1].params.drive==22,'effects edits participate in project undo')
print('PASS DAW clips, drum swing, piano durations, mix, multitimbral MIDI, persistence and validation; '..checks..' assertions')

local mix=Workstation.new{configure=function(n,p,l,m) config[n]={mixer=copy(m)} end,message=function() end,panic=function() end}
mix:add('piano');mix.wall=10
check(mix:setMixer(1,'bass',6) and config[1].mixer.bass==6,'strip config passed independently of patch')
check(mix:undo() and mix.project.tracks[1].mixer.bass==0,'strip undo restores state')
check(mix:redo() and mix.project.tracks[1].mixer.bass==6,'strip redo restores state')
check(mix:preset(1,'b:guitar-clean') and mix.project.tracks[1].mixer.bass==6,'loading sound preserves mix')
check(not mix:setMixer(1,'ratio',0) and not mix:setMixer(1,'eqEnabled',0.5),'invalid strip edit rejected')
local doc=format.project(mix.project);check(doc.tracks[1].mixer.bass==6,'project serialization includes mix')
print('PASS mixer patch independence, validation, project round trip, undo and redo')

check(mix:setEffect('delayTime',375) and mix:undo() and mix.project.effects.delayTime==250,'global return undo')
check(mix:redo() and mix.project.effects.delayTime==375,'global return redo')
check(mix:setMixer(1,'reverbSend',0.6) and format.project(mix.project).tracks[1].mixer.reverbSend==0.6,'send saved independently of sound')
check(not mix:setEffect('delayFeedback',1),'unsafe feedback refused')
print('PASS shared sends/returns persistence, migration, undo and feedback validation')
