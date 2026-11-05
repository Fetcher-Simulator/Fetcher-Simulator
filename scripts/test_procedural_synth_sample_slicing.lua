local root='files/server/server-lua-packages/fetcher.procedural-synth/'
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local function module(name) local value=dofile(root..name:gsub('%.','/')..'.lua');package.loaded[prefix..name]=value;return value end
local Schema=module('shared.schema');local F=module('shared.project')(Schema)
module('client.midi');module('client.soundsets');module('client.speech-catalog');module('client.voice-text');module('shared.voice-transcripts')
local Samples=module('client.samples');local W=module('client.workstation');local Sequence=module('shared.sequence')
local count=0;local function check(ok,why) count=count+1;assert(ok,why) end
local hash=string.rep('a',32);local status='Ready';local calls=0;local result={points={.2,.65},found=2,hash=hash}
local synth={version=40,sampleInfo=function() return {status=status,duration=1.2,hash=hash} end,
 sampleTransients=function(path,offset,duration,sensitivity,spacing,reverse,snap)
  calls=calls+1;check(path=='sound/fx/phrase.wav' and offset==0 and duration==1.2,'analysis stays within the selected excerpt')
  check(sensitivity==.7 and spacing==.12 and reverse and snap,'analysis gets exact controls and displayed direction');return result
 end}
local b=Samples.new(synth,{},{},{});b:select('sound/fx/phrase.wav');b.duration=1.2;b.reverse=true
local detected=assert(b:detect(.7,.12,true));check(#detected.points==2 and detected.points~=result.points,'suggestions are an editable copy')
check(not b:detect(0/0,.12,true) and not b:detect(.5,.001,true) and calls==1,'invalid controls cannot run analysis')
status='Loading';check(not b:detect(.7,.12,true) and calls==1,'loading excerpt keeps drafts untouched');status='Ready'
result={points={.2,.65},hash=string.rep('b',32)};check(not b:detect(.7,.12,true),'analysis cannot apply from a different file identity')
result={points={.2,.205},hash=hash};check(not b:detect(.7,.12,true),'bad suggested boundaries fail before project creation')
result={points={},found=0,hash=hash};check(#assert(b:detect(.7,.12,true)).points==0,'no attacks is a valid result')
synth.sampleTransients=nil;local _,err=b:detect(.7,.12,true);check(err:find('API 40',1,true),'older client gets an explicit upgrade message')
local function make()
 return W.new{configure=function() end,panic=function() end,message=function() end,stopSequence=function() end}
end
local refs={};local elapsed=0
for n,duration in ipairs{.17,.23,.6} do refs[n]={path='sound/fx/phrase.wav',offset=elapsed,duration=duration,root=60,oneShot=true,hash=hash,reverse=true};elapsed=elapsed+duration end
local w=make();w:add('piano');w.project.tracks[1].notes={{n=62,s=0,l=4,v=.8}};w.project.tracks[1].level=.42;w:touch();w.cursorBeat=8
local history=#w.undoStack
check(w:addSampleSlices(refs,'Phrase',true) and w.page=='arrange' and #w.project.tracks==4,'sequence creates arranged sample parts at the playhead')
check(#w.undoStack==history+1 and w.project.tracks[1].kind=='piano' and w.project.tracks[1].notes[1].n==62,'original part is preserved and creation is one Undo')
local saved=assert(F.project(w.project));check(saved.composition.bars==3 and saved.length==16,'song expands independently of shared pattern bars')
local function unpack32(s,at) local a,b,c,d=s:byte(at,at+3);return a+b*256+c*65536+d*16777216 end
local function frames(project)
 local plan=Sequence.compile(project,nil,nil,'song',F);local times={}
 for at=1,#plan.bytes,11 do local lane,note=plan.bytes:byte(at+8,at+9);if lane>1 and note<128 then times[lane]=unpack32(plan.bytes,at) end end
 return times
end
local times=frames(saved)
for n,frame in ipairs{192000,200160,211200} do
 check(times[n+1]==frame,'original slice onset is retained to an audio frame')
 local t=saved.tracks[n+1];check(t.kind=='midi' and #t.arrangement.clips==1 and #t.arrangement.regions==1 and #F.decodeNotes(t.edited)==0,'source clip does not duplicate flat notes')
 check(t.params.sample.reverse and t.params.sample.hash==hash and t.level==.42,'reference, direction and mixing survive project validation')
end
check(w:undo() and #w.project.tracks==1 and not w.project.timeline and w:redo() and #w.project.tracks==4,'Undo/Redo restores the entire created phrase and timing map')
local meter=make();assert(meter:newSong(120,3,4,8));meter.project.timeline.tempos={{beat=0,tempo=500000},{beat=4,tempo=1000000}};meter.cursorBeat=3.8
check(meter:addSampleSlices(refs,'Tempo',true),'phrase can cross a tempo change')
times=frames(meter.project)
for n,t in ipairs(meter.project.tracks) do if n>1 then
 local r=t.arrangement.regions[1];local expected=1.9+refs[n-1].offset
 check(math.abs(times[n]/48000-expected)<1/48000+.000001,'original seconds are preserved through tempo-map conversion')
end end
local before=F.project(meter.project);meter.recording={};check(not meter:addSampleSlices(refs,'Recording',true) and #meter.project.tracks==#before.tracks,'recording blocks all slice mutations');meter.recording=nil
for n=#meter.project.tracks+1,31 do meter.project.tracks[n]=F.newTrack('piano','Full',16) end
check(not meter:addSampleSlices(refs,'Full',true) and #meter.project.tracks==31,'insufficient slots never create a partial phrase')
local long=make();long:add('piano');long.project.bpm=240
local tooLong={};for n,duration in ipairs{29,.1} do tooLong[n]={path=refs[1].path,offset=0,duration=duration,root=60,oneShot=true,hash=hash} end
check(not long:addSampleSlices(tooLong,'Long',true) and #long.project.tracks==1 and not long.project.timeline,'source clip limits keep the original project intact')
local pattern=make();pattern:add('piano');pattern.project.length=256;pattern.project.tracks[1].notes={{n=60,s=200,l=4,v=.8}};pattern:touch()
check(pattern:addSampleSlices(refs,'Short phrase',true) and pattern.project.composition.bars==16
 and pattern.project.tracks[1].notes[1].s==200,'adding a short phrase cannot shrink an existing long pattern')
print('PASS automatic slice controls/readiness/identity, exact-time phrase placement, tempo maps, persistence, history and atomic limits; '..count..' assertions')
