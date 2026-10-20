local root='files/server/server-lua-packages/fetcher.procedural-synth/';local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=dofile(root..'shared/schema.lua');local factory=dofile(root..'shared/project.lua');local f=factory(schema)
local MIDI=dofile(root..'client/midi.lua');local Sequence=dofile(root..'shared/sequence.lua')
package.loaded[prefix..'shared.schema']=schema;package.loaded[prefix..'shared.project']=factory;package.loaded[prefix..'shared.sequence']=Sequence
package.loaded[prefix..'client.midi']=MIDI;package.loaded[prefix..'client.soundsets']=dofile(root..'client/soundsets.lua')
package.loaded[prefix..'client.arrangement']=dofile(root..'client/arrangement.lua')
local W=dofile(root..'client/workstation.lua');local checks=0;local now=100;local score
local function check(b,m) checks=checks+1;assert(b,m) end
local function copy(t) if type(t)~='table' then return t end;local out={};for k,v in pairs(t) do out[k]=copy(v) end;return out end
local function make() return W.new{configure=function() end,panic=function() end,message=function() end,on=function() return true end,off=function() end,
    now=function() return now end,progress=function() return true,0,0 end,schedule=function(p) score=p;return true end,stopSequence=function() end} end
local function u32(s,at) local a,b,c,d=s:byte(at,at+3);return a+b*256+c*65536+d*16777216 end
local function notes(plan)
    local out={};for at=1,#plan.bytes,11 do out[#out+1]={frame=u32(plan.bytes,at),gate=u32(plan.bytes,at+4),lane=plan.bytes:byte(at+8),n=plan.bytes:byte(at+9)} end;return out
end
local w=make();w:newSong(120,4,4,16);w:setOutput(1,2);w:setMixer(1,'bass',3)
w:addNote(1,60,2);w:addNote(1,64,4);w:addNote(1,67,24);w:selectNote(1,1);w:selectNote(1,2,true)
local history=#w.undoStack;check(w:captureClip(1),'selected notes become a reusable source and first placement')
local t=w.project.tracks[1];local a=t.arrangement
check(#a.clips==1 and #a.regions==1 and a.clips[1].length==16 and #w:noteData(1)==1,'capture removes only selected linear notes and preserves phrase timing')
check(f.decodeNotes(a.clips[1].notes)[1].s==2 and a.regions[1].s==0,'bar anchor preserves silence before the first captured note')
check(#w.undoStack==history+1 and t.output.slot==2 and t.mixer.bass==3,'capture is one undo action and keeps patch/mix/routing')
check(w:undo() and not w.project.tracks[1].arrangement and #w:noteData(1)==3 and w:redo(),'capture undo/redo does not duplicate or lose notes')
check(#Sequence.compile(w.project,nil,MIDI,'song',f).bytes==33,'converted phrase plays exactly once alongside remaining linear notes')
check(w:duplicateRegion(1,1) and #w.project.tracks[1].arrangement.clips==1,'duplicates link the existing source rather than copying note bytes')
check(w:changeRegion(1,2,{s=32,l=32,transpose=12}),'placements move, repeat and transpose independently')
local played=notes(Sequence.compile(w.project,nil,MIDI,'song',f))
check(#played==7 and played[4].n==72 and played[6].n==72,'lengthened placement repeats the shared phrase with transposition')
check(w:editClip(1,1) and #w:noteData(1)==2,'source editor selects source notes rather than the direct song layer')
w:selectAllNotes(1);check(w:editSelected(1,{pitch=1}),'source notes use the existing piano-roll editor')
played=notes(Sequence.compile(w.project,nil,MIDI,'song',f));check(played[1].n==61 and played[4].n==73 and played[6].n==73,'one source edit reaches all linked and repeated placements')
check(not w:addNote(1,127,0),'source edit that overflows a transposed placement is rejected atomically')
check(not w:addNote(1,60,16),'source notes cannot silently exceed its length')
check(w:playClip() and score.loop and #score.bytes==22 and score.bytes:byte(9)==1,'source audition compiles only that lane and source period')
check(not w:startTake(1),'recording into a shared source while transport plays requires Stop')
w:stop(true);now=200;check(w:startTake(1) and not w:leaveClip(),'armed source recording locks its destination')
now=202.2;w:recordOn(70,0.8);now=202.6;w:recordOff(70);check(w:endTake(false),'stopped source recording commits a take')
check(#w:noteData(1)==3 and #Sequence.compile(w.project,nil,MIDI,'song',f).bytes==110,'recorded source note appears in every linked repetition')
check(w:leaveClip() and #w:noteData(1)==1,'leaving source edit restores the independent linear layer')
local before=w.project.tracks[1].arrangement.clips[1].notes.bytes
check(not w:setClip(1,1,{length=4}) and w.project.tracks[1].arrangement.clips[1].notes.bytes==before,'source shortening never truncates existing notes')
check(w:changeRegion(1,2,{mute=true}) and #Sequence.compile(w.project,nil,MIDI,'song',f).bytes==44,'placement mute affects only that instance')
check(w:removeRegion(1,1) and #w.project.tracks[1].arrangement.clips==1,'removing placement retains the reusable source')
check(w:deleteClip(1,1) and #w.project.tracks[1].arrangement.regions==0 and #w:noteData(1)==1,'source deletion removes its placements and preserves linear notes')
check(w:undo() and #w.project.tracks[1].arrangement.clips==1,'source deletion and placement cleanup undo together')
for n=2,8 do check(w:newClip(1)==n,'bounded source slot can be created') end
check(not w:newClip(1),'ninth source rejected before mutation')
local saved=assert(f.project(w.project));local loaded=make();check(loaded:load(saved) and loaded:play('song'),'saved clip song plays without original file')
check(loaded.project.tracks[1].output.slot==2 and loaded.project.tracks[1].arrangement.clips[1].notes.bytes==before,'load preserves source bytes and band output')
local legacy=make();legacy:newSong(120,4,4,8);legacy:add('drum','b:snare','Snare',38);legacy:toggleStep(2,1)
legacy.project.swing=0.5;check(legacy:captureClip(2) and legacy.project.tracks[2].kind=='midi','step drum clip can become an arranged song part')
local hit=f.decodeNotes(legacy.project.tracks[2].arrangement.clips[1].notes)[1]
check(hit.n==38 and hit.s==1.5 and legacy.project.tracks[2].sourceRole=='snare' and legacy.project.tracks[2].steps:find('[1-9]')==nil,'step swing/gate and drum sound role are baked once, old overlay is cleared')
local p=f.new();p.midi='midi/source.mid';p.timeline={beats=4,tempos={{beat=0,tempo=500000},{beat=4,tempo=1000000}}}
p.tracks[1]=f.newTrack('midi','Source',16);p.tracks[1].edited=f.encodeNotes({},960)
p.tracks[1].arrangement={clips={{name='Tail',length=8,notes=f.encodeNotes({{n=60,s=2,l=6,v=1}},960)}},regions={{clip=1,s=32,l=11,transpose=0,mute=false}}}
check(f.project(p),'imported converted parts accept arrangements')
played=notes(Sequence.compile(p,nil,MIDI,'song',f))
check(#played==2 and played[1].frame==312000 and played[1].gate==72000 and played[2].gate==12000,'tempo map and partial final repetitions preserve exact note gates')
local bad=copy(p);bad.tracks[1].arrangement.regions[1].clip=9;check(not f.project(bad),'dangling clip references rejected')
bad=copy(p);bad.tracks[1].arrangement.clips[1].notes.bytes='bad';check(not f.project(bad),'malformed binary source notes rejected')
bad=copy(p);bad.tracks[1].arrangement.regions[1].s=0/0;check(not f.project(bad),'non-finite placement position rejected')
bad=copy(p);bad.tracks[1].arrangement.regions[1].l=100000;check(not f.project(bad),'placements beyond 30 minutes rejected even outside playback bounds')
bad=copy(p);bad.tracks[1].arrangement.clips[1].length=0.25;bad.tracks[1].arrangement.clips[1].notes=f.encodeNotes({{n=60,s=0,l=0.25,v=1},{n=64,s=0,l=0.25,v=1},{n=67,s=0,l=0.25,v=1}},960)
bad.tracks[1].arrangement.regions[1].s=0;bad.tracks[1].arrangement.regions[1].l=7000
check(not f.project(bad),'compact repeats cannot exceed the expanded-score budget')
bad.tracks[1].arrangement.regions[1].mute=true;check(f.project(bad),'muted placements cost no expanded notes')
bad.tracks[1].arrangement.regions[1].mute=false;bad.tracks[1].arrangement.clips[1].notes=f.encodeNotes({},960)
check(f.project(bad) and f.regionNoteCount(bad.tracks[1])==0,'long empty sources validate without iterating empty repetitions')
f.eachRegionNote(bad.tracks[1],function() error('empty source emitted a note') end)
local limits=f.new();limits.timeline={beats=256,tempos={{beat=0,tempo=500000}}}
for lane=1,4 do
    local track=f.newTrack('midi','Part',16);track.edited=f.encodeNotes({},960)
    track.arrangement={clips={{name='Empty',length=16,notes=f.encodeNotes({},960)}},regions={}}
    for n=1,64 do track.arrangement.regions[n]={clip=1,s=0,l=16,transpose=0,mute=false} end
    limits.tracks[lane]=track
end
check(f.project(limits),'256 placements fit the project bound')
limits.tracks[5]=copy(limits.tracks[1]);limits.tracks[5].arrangement.regions={limits.tracks[5].arrangement.regions[1]}
check(not f.project(limits),'257 placements rejected across track boundaries')
local dense=copy(p);dense.tracks[1].arrangement.regions={};local packed={}
for n=1,4096 do packed[n]={n=60,s=0,l=1,v=1} end
local clip={name='Dense',length=16,notes=f.encodeNotes(packed,960)}
dense.tracks[1].arrangement.clips={copy(clip),copy(clip)}
check(f.project(dense),'8192 saved source notes fit the existing total budget')
dense.tracks[1].edited=f.encodeNotes({{n=60,s=0,l=1,v=1}},960)
check(not f.project(dense),'direct and source notes share one saved-note budget')
local server=dofile('apps/openmw-server/scripts/procedural_synth_project.lua')(schema)
check(server.project(saved).tracks[1].arrangement.clips[1].notes.bytes==before,'server accepts identical compact arrangement data')
check(dofile('apps/openmw-server/scripts/procedural_synth_sequence.lua').compile(p,nil,MIDI,'song',server).bytes==Sequence.compile(p,nil,MIDI,'song',f).bytes,'server/client arrangement compilation matches')
print('PASS linked clips, capture, repeat/trim, transpose/mute, shared editing/recording, history, persistence and budgets; '..checks..' assertions')
