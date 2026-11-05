local root='files/server/server-lua-packages/fetcher.procedural-synth/'
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local function module(name) local m=dofile(root..name:gsub('%.','/')..'.lua');package.loaded[prefix..name]=m;return m end
local Schema=module('shared.schema');local F=module('shared.project')(Schema);local MIDI=module('client.midi')
module('client.soundsets');module('client.arrangement');local Sequence=module('shared.sequence');local W=module('client.workstation')
local checks=0;local function check(ok,why) checks=checks+1;assert(ok,why) end
local scheduled={};local function make()
 return W.new{configure=function() end,panic=function() end,stopSequence=function() end,message=function() end,
  now=function() return 100 end,schedule=function(plan,replace) scheduled[#scheduled+1]={plan=plan,replace=replace};return true end}
end
local w=make();check(w:newSong(120,4,4,16),'create authored song')
w.project.tracks[1].edited=F.encodeNotes({{n=60,s=0,l=64,v=.8}},960);w:touch()
check(w:seek(8) and w:play('song'),'play from the stopped edit cursor')
check(scheduled[#scheduled].plan.startFrame==192000 and not scheduled[#scheduled].replace,'seek preserves full score and supplies native chase offset')
check(w:seek(10) and scheduled[#scheduled].plan.startFrame==240000 and w.awaiting,'playing seek schedules the same absolute position for all listeners')
local history=#w.undoStack
check(w:setSongLoop(3,6,true),'set loop by bar boundaries')
check(w.project.composition.loopStart==3 and w.project.composition.loopEnd==5 and w.playing,'exclusive right handle maps to inclusive saved loop end and resumes playback')
check(#w.undoStack==history+1 and scheduled[#scheduled].plan.length==288000 and scheduled[#scheduled].plan.startFrame==48000,'one loop edit is one Undo with cursor relative to cropped loop')
check(w:undo() and not w.project.composition.loop and w:redo() and w.project.composition.loop,'loop range supports Undo/Redo')
check(not w:setSongLoop(6,6,true) and not w:setSongLoop(0,4,true) and not w:setSongLoop(1,258,true),'invalid authored bar bounds keep the document')
w.recording={};check(not w:seek(4) and not w:setSongLoop(1,4,true),'timing changes cannot disrupt a recording');w.recording=nil
local imported=make();imported.project.midi='midi/private.mid';imported.project.tracks[1]=F.newTrack('midi','Imported',16)
imported.project.tracks[1].edited=F.encodeNotes({{n=60,s=16,l=24,v=.8}},960)
imported.song={beats=16,duration=8,division=96,events={},tempos={{beat=0,time=0,tempo=500000}},meters={{beat=8,numerator=3,denominator=4}}}
imported:touch()
check(imported:setSongLoop(2,4,true) and imported.project.midi=='midi/private.mid','imported MIDI retains its source while acquiring independent loop markers')
local first,last=F.songRange(imported.project)
check(first==4 and last==11,'loop bars follow imported meter changes')
local saved=assert(F.project(imported.project));local plan=Sequence.compile(saved,imported.song,MIDI,'song',F)
check(plan.loop and plan.length==168000 and saved.loopRange.first==2 and saved.loopRange.last==4,'imported loop range persists and compiles with the real tempo/meter map')
saved.loopRange.last=5000;check(not F.project(saved),'oversized saved loop is rejected')
local refs={};for n=1,4 do refs[n]={path='sound/fx/slices.wav',offset=(n-1)*.5,duration=.5,root=60,oneShot=true,hash=string.rep('a',32)} end
local slices=make();slices:add('piano');slices.project.tracks[1].notes={{n=62,s=0,l=4,v=.8}};slices:touch()
local original=assert(F.project(slices.project));history=#slices.undoStack
check(slices:addSampleSlices(refs,'Phrase') and #slices.project.tracks==5 and slices.page=='drums','chops create separate sequencer lanes')
check(slices.project.tracks[1].notes[1].n==62 and slices.project.tracks[2].params.sample.offset==0 and slices.project.tracks[5].params.sample.offset==1.5,'original music is preserved and slices retain exact file ranges')
check(#slices.undoStack==history+1 and slices:undo() and #slices.project.tracks==1 and slices:redo() and #slices.project.tracks==5,'batch slicing is one reversible edit')
for n=6,32 do slices.project.tracks[n]=F.newTrack('piano','Full',16) end
local before=#slices.project.tracks;check(not slices:addSampleSlices(refs,'Overflow') and #slices.project.tracks==before,'full project cannot receive a partial slice batch')
print('PASS shared seek offsets, authored/imported bar loops, tempo/meter mapping, persistence, recording guards and atomic sample chops; '..checks..' assertions')
