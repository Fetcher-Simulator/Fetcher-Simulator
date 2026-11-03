local root='files/server/server-lua-packages/fetcher.procedural-synth/';local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=dofile(root..'shared/schema.lua');local factory=dofile(root..'shared/project.lua');local F=factory(schema)
local MIDI=dofile(root..'client/midi.lua');local Sequence=dofile(root..'shared/sequence.lua')
package.loaded[prefix..'shared.schema']=schema;package.loaded[prefix..'shared.project']=factory
package.loaded[prefix..'client.midi']=MIDI;package.loaded[prefix..'client.soundsets']=dofile(root..'client/soundsets.lua')
package.loaded[prefix..'shared.sequence']=Sequence
package.loaded[prefix..'client.controller-map']=dofile(root..'client/controller-map.lua')
local W=dofile(root..'client/workstation.lua');local R=dofile(root..'client/recorder.lua')
local checks=0;local function check(ok,msg) checks=checks+1;assert(ok,msg) end
local function le(s,at) local a,b,c,d=s:byte(at,at+3);return a+b*256+c*65536+d*16777216 end
local function records(plan,code) local out={};for at=1,#plan.bytes,11 do if plan.bytes:byte(at+9)==code then out[#out+1]={frame=le(plan.bytes,at),value=le(plan.bytes,at+4),mode=plan.bytes:byte(at+10)} end end;return out end
local position=1;local configured={};local w=W.new{configure=function(n,p,level,mix,audible) configured[n]={level=level,audible=audible,mix=mix} end,
    now=function() return 100 end,panic=function() end,message=function() end,stopSequence=function() end,
    schedule=function() return true end,progress=function() return true,position,0 end}
assert(w:newSong(120,4,4,8));assert(w:addNote(1,60,0));assert(w:setTrack(1,'level',.7));assert(w:setMixer(1,'delaySend',.2))
check(w:automationValue(1,'trackLevel',0)==.7 and w:automationValue(1,'delaySend',0)==.2,'mix automation fallback reads saved fader and sends')
check(F.automationSpecs.trackLevel.code==214 and F.automationSpecs.delaySend.code==215 and F.automationSpecs.reverbSend.code==216
    and F.automationSpecs.pitchBend.code==183 and F.automationSpecs.stringSelect.code==213,'new lane IDs append without shifting synth or MIDI controls')
for _,name in ipairs{'trackLevel','delaySend','reverbSend'} do
    check(not w:setAutomation(1,name,0,-.1,false) and not w:setAutomation(1,name,0,1.1,false),'mix curves enforce finite unit ranges')
end
w.playing=true;w.native=true;w.mode='song';w.beat=2
local history=#w.undoStack;assert(w:beginAutomationWrite());position=1.5;w.beat=3
assert(w:setTrack(1,'level',.1));assert(w:setMixer(1,'delaySend',.8));assert(w:setMixer(1,'reverbSend',.6))
w:endAutomationWrite();check(#w.undoStack==history+1,'mouse fader and send movements form one Auto take')
local t=w.project.tracks[1]
check(t.automation.trackLevel[1].s==0 and t.automation.trackLevel[1].v==.7 and t.automation.trackLevel[2].s==12 and t.automation.trackLevel[2].v==.1,'first fader capture atomically seeds its initial value before the move')
check(t.automation.delaySend[1].v==.2 and t.automation.delaySend[2].v==.8 and t.automation.reverbSend[1].v==0,'send capture preserves initial targets and authored new values')
check(w:automationValue(1,'trackLevel',5)==.7,'new lane does not change pre-recording fader value')
local project=assert(F.project(w.project));local plan=Sequence.compile(project,nil,MIDI,'song',F)
check(Sequence.validate(plan.bytes,plan.length,plan.loop),'portable mix curves validate on the shared server path')
for code=214,216 do local points=records(plan,code);check(#points==2 and points[1].frame==0 and points[2].frame==72000 and points[2].mode==128,'fader/send lanes use native score time and continuous ramp flags') end
local bad=plan.bytes:gsub(string.char(214,128),string.char(217,128),1)
check(not Sequence.validate(bad,plan.length,plan.loop),'unknown future wire control is rejected')
local parsed=MIDI.parse(dofile(root..'client/midi-export.lua').encode(project,nil,MIDI,F))
check(#MIDI.parts(parsed)==1 and MIDI.editableSpans(parsed,MIDI.parts(parsed)[1].id)[1].n==60,'MIDI export keeps notes while Studio-specific mix lanes remain private')
assert(w:undo());check(not (w.project.tracks[1].automation or {}).trackLevel and w.project.tracks[1].level==.7,'Undo restores curves and base mix together')
assert(w:redo());check(w.project.tracks[1].automation.reverbSend[2].v==.6,'Redo restores whole mix take')
assert(w:setTrack(1,'mute',true));check(configured[1].level==.1 and configured[1].audible==false,'Mute publishes a separate gate without erasing absolute fader')
assert(w:setTrack(1,'mute',false));check(configured[1].audible==true,'Unmute restores audibility')
-- Crop and interpolate at a loop start, without generating instrument-program snapshots for mix lanes.
w:stop(true);project=assert(F.project(w.project));project.composition.loop=true;project.composition.loopStart=2;project.composition.loopEnd=3
project.tracks[1].automation.trackLevel={{s=0,v=0,linear=true},{s=64,v=1,linear=false}}
project.tracks[1].automation.program={{s=0,v=25,linear=false}};project.tracks[1].followPrograms=true
local SoundSets=package.loaded[prefix..'client.soundsets'];plan=Sequence.compile(project,nil,MIDI,'song',F,SoundSets.programPatch)
local fader=records(plan,214)
check(fader[1].frame==0 and math.abs(fader[1].value-16384)<=1,'loop crop chases fader interpolation at the native start')
for at=1,#plan.bytes,11 do if plan.bytes:byte(at+9)>=214 then check(plan.bytes:byte(at+10)~=129,'program snapshots never turn mixer controls into synth parameters') end end
-- Hardware fader/send capture follows a fixed track and applies latency exactly once.
assert(w:newSong(120,4,4,8));local saved={};local r=R.new(w,{}, {getRealTime=function() return 100 end},{},
    {get=function(_,k) return saved[k] end,set=function(_,k,v) saved[k]=v end},function() end);w.recorder=r
assert(r.controllers:edit(1,{cc=20,target='level',track=1,pickup=false}));assert(r.controllers:edit(2,{cc=21,target='mixer:delaySend',track=1,pickup=false}))
assert(r:setMapping('enabled',true));assert(r:set('latency',50));w.native=true;w.playing=true;w.mode='song';w.beat=2;position=1
assert(r:writeMappings());position=1.25
for _,cc in ipairs{20,21} do assert(r:event{status=176,channel=1,a=cc,b=127,age=.075}) end
assert(r:writeMappings());t=w.project.tracks[1]
for _,name in ipairs{'trackLevel','delaySend'} do local points=t.automation[name]
    check(#points==3 and points[3].s==9 and points[3].v==1,'hardware level/send write compensates age and latency once, with one point per gesture')
end
-- Mixer-page arming uses the same stale-event boundary as mapping-page arming.
position=2;assert(w:beginAutomationWrite());local before=#t.automation.trackLevel;local previous=t.level
assert(r:event{status=176,channel=1,a=20,b=10,age=.1})
check(#t.automation.trackLevel==before and t.level==previous,'pre-arm buffered controller events cannot alter or enter a mix take')
w:endAutomationWrite();check(not r.mappingWriting and not r.mappingStart and not w.automationWriteTime,'any End write button clears the shared capture boundary')
print('PASS absolute mix automation, native IDs/ranges, atomic capture baselines, mouse/hardware timing, mute gate, loop chase, program isolation, persistence, Undo/Redo and MIDI export; '..checks..' assertions')
