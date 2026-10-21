local root='files/server/server-lua-packages/fetcher.procedural-synth/';local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=dofile(root..'shared/schema.lua');local factory=dofile(root..'shared/project.lua');local format=factory(schema)
package.loaded[prefix..'shared.schema']=schema;package.loaded[prefix..'shared.project']=factory
package.loaded[prefix..'client.midi']=dofile(root..'client/midi.lua');package.loaded[prefix..'client.soundsets']=dofile(root..'client/soundsets.lua')
local W=dofile(root..'client/workstation.lua');local now,playing,time=100,false,0;local on,off=0,0
local checks=0;local function check(b,m) checks=checks+1;assert(b,m) end
local function make()
    return W.new{now=function() return now end,configure=function() end,panic=function() end,message=function() end,
        on=function() on=on+1;return true end,off=function() off=off+1 end,progress=function() return playing,time,0 end,
        schedule=function() return true end,stopSequence=function() end}
end
local w=make();w:add('piano');local history=#w.undoStack;local generation=w.generation
check(w:startTake(1) and w:recordStatus():find('Count in'),'stopped take supplies a visual count-in')
check(not w:recordOn(60,1) and on==0,'count-in cannot capture early notes')
now=102.14;check(w:recordOn(60,0.7) and w:recordOn(64,0.9),'chord input accepted after count-in')
check(not w:recordOn(60,1),'key repeat does not create duplicate held notes')
now=102.42;w:recordOff(60);now=102.6;w:recordOff(64)
check(#w.project.tracks[1].notes==0 and w.generation==generation,'active take does not mutate saved data/history')
check(not w:undo(),'history cannot change the recording destination under an active take')
check(w:endTake(false) and #w.undoStack==history+1,'whole take is one undo action')
local notes=w:noteData(1);check(notes[1].s==1 and notes[1].l==2 and notes[2].s==1 and notes[2].l==4,'onsets and gates quantize to the frozen grid')
check(notes[1].v==0.7 and w:undo() and #w:noteData(1)==0 and w:redo() and #w:noteData(1)==2,'velocity and atomic take undo/redo persist')
now=110;history=#w.undoStack;w:startTake(1);now=113;w:recordOn(67,1);w:endTake(true)
check(off>=3 and #w.undoStack==history and #w:noteData(1)==2,'Cancel releases held audio without musical edits')
w:load(format.new());w:add('piano');w.snap=0.5;w.playing=true;w.native=true;w.mode='clips';w.awaiting=false;playing=true;time=0.3
check(w:startTake(1) and not w:recordStatus():find('Count in'),'overdub begins at native audible position')
check(w:recordOn(60,1),'native-clock input')
time=0.7;w:recordOff(60);w:endTake(false)
check(w:noteData(1)[1].s==2.5 and w:noteData(1)[1].l==3,'native clock determines capture despite unchanged wall time')
w:endTake(true);time=1.8;w:startTake(1);w:recordOn(72,1);time=2.05;w:recordFrame()
check(not w.recording and w:noteData(1)[2].s==14.5 and w:noteData(1)[2].s+w:noteData(1)[2].l==16,'loop boundary finalizes held gates inside the clip')
w:stop(true);playing=false;now=200;w.rollStart=32
check(not w:startTake(1),'stopped clip recording cannot silently wrap a view beyond the loop')
w.rollStart=0;w.cb.canMonitor=function() return false end
check(not w:startTake(1),'unavailable physical band output cannot silently record unheard monitoring')
w.cb.canMonitor=nil;w:startTake(1);now=203;w:recordOn(65,1);w:stop(true)
check(not w.recording and not w.playing and #w:noteData(1)==3,'Stop keeps and releases a pending take')
local p=format.new();p.midi='midi/record.mid';p.timeline={beats=80,tempos={{beat=0,tempo=500000},{beat=4,tempo=1000000}}}
p.tracks[1]=format.newTrack('midi','Edited',16);p.tracks[1].edited=assert(format.encodeNotes({},96));w:load(p)
w.rollStart=20;now=300;check(w:startTake(1),'edited MIDI can record beyond clip loop')
now=304.3;w:recordOn(60,0.8);now=305.1;w:recordOff(60);w:endTake(false)
check(w:noteData(1)[1].s==21 and w:noteData(1)[1].l==3.5,'stopped song take follows saved tempo map from view start')
check(format.project(w.project).tracks[1].edited.bytes==w.project.tracks[1].edited.bytes,'recorded song serializes through existing project validation')
local q=make();q:add('piano');now=400;q:startTake(1);now=403;q:recordOn(60,1)
q.cb.canMonitor=function() return false end;q:recordFrame()
check(not q.recording and #q:noteData(1)==1,'lost instrument readiness commits/releases the partial take')
q.cb.canMonitor=nil;now=410;q:startTake(1);now=413;q:recordOn(62,1);q:recordOff(62)
for n=2,128 do q.project.tracks[1].notes[n]={n=60,s=0,l=1,v=1} end
check(not q:endTake(false) and q.failedTake and #q.failedTake.notes==1,'failed commit retains a bounded recoverable take')
q.project.tracks[1].notes={};check(q:recoverTake() and #q:noteData(1)==1 and not q.failedTake,'Retry commits the preserved take after capacity is freed')
q.playing=true;q.native=true;q.mode='clips';playing=false;q.awaiting=false;q:startTake(1);q.playing=false;q:recordFrame()
check(not q.recording,'transport ending before capture does not leave a permanently armed take')
print('PASS quantized takes, count-in, native overdub, chords, loop/end cleanup, cancel, history and saved MIDI timing; '..checks..' assertions')
