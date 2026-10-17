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
    configure=function(n,p,level) config[n]={params=copy(p),level=level} end,
    on=function(lane,note,velocity) events[#events+1]={'on',lane,note,velocity} end,
    off=function(lane,note) events[#events+1]={'off',lane,note} end,
    panic=function() events[#events+1]={'panic'} end,
    save=function(p) saved=copy(p) end,
    importMidi=function(path) messages[#messages+1]=path end,
    message=function(message) messages[#messages+1]=message end,
}
check(#w.catalog==8,'builtins available before asynchronous personal catalog response')
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
check(config[1].level==0 and math.abs(config[2].level-0.56)<0.00001,'solo changes mix without overwriting timbres')
w:setTrack(2,'solo',false);w:setTrack(1,'mute',true)
check(config[1].level==0 and w.project.tracks[1].level==0.8,'mute preserves fader setting')
w:setTrack(1,'mute',false);w:setTrack(2,'level',0.3)
check(math.abs(config[2].level-0.21)<0.00001,'independent mixer level')
check(not w:setTrack(2,'level',0/0) and not w:setTrack(2,'level',2),'invalid level rejected')
w:play('clips');w:frame(0)
check(events[#events-1][1]=='on' and events[#events-1][2]==1 and events[#events][2]==2,'drum and piano start together')
for _=1,4 do w:frame(0.125) end
local pianoReleased=false;for _,e in ipairs(events) do if e[1]=='off' and e[2]==2 then pianoReleased=true end end
check(not pianoReleased,'long piano note stays held across drum steps')
for _=1,4 do w:frame(0.125) end
check(events[#events][1]=='off' and events[#events][2]==2,'piano duration releases its own channel')
w:stop();check(events[#events][1]=='panic','stop releases all channels')
w:length(32);check(#w.project.tracks[1].steps==32,'bar length expands pattern without dropping first bar')
w:toggleNote(2,60,2);check(#w.project.tracks[2].notes==0,'click within a note removes it')
w:toggleNote(2,64,16);w:toggleStep(1,20)
w:length(16);check(#w.project.tracks[2].notes==0 and #w.project.tracks[1].steps==16,'shortening clips trims out-of-range notes')
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
check(events[1][2]==1 and events[2][2]==2 and config[1].params.wave1==0 and config[2].params.wave1==3,'MIDI parts play through different presets')
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
print('PASS DAW clips, drum swing, piano durations, mix, multitimbral MIDI, persistence and validation; '..checks..' assertions')
