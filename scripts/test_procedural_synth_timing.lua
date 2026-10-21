local root='files/server/server-lua-packages/fetcher.procedural-synth/';local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=dofile(root..'shared/schema.lua');local format=dofile(root..'shared/project.lua')(schema)
local MIDI=dofile(root..'client/midi.lua');local Sequence=dofile(root..'shared/sequence.lua')
package.loaded[prefix..'shared.schema']=schema;package.loaded[prefix..'shared.project']=dofile(root..'shared/project.lua')
package.loaded[prefix..'client.midi']=MIDI;package.loaded[prefix..'shared.sequence']=Sequence
local SoundSets=dofile(root..'client/soundsets.lua');package.loaded[prefix..'client.soundsets']=SoundSets
local W=dofile(root..'client/workstation.lua');local Export=dofile(root..'client/midi-export.lua')
local checks=0;local function check(ok,msg) checks=checks+1;assert(ok,msg) end
local function near(a,b) return math.abs(a-b)<1e-7 end
local function copy(t) if type(t)~='table' then return t end;local out={};for k,v in pairs(t) do out[k]=copy(v) end;return out end
local messages={};local now=10;local cb={configure=function() end,panic=function() end,stopSequence=function() end,
    now=function() return now end,message=function(m) messages[#messages+1]=m end,schedule=function() return true end,progress=function() return true,4,0 end}
local w=W.new(cb);assert(w:newSong(120,4,4,8));w.noteLength=4;assert(w:addNote(1,60,32));local notes=w.project.tracks[1].edited.bytes
check(w:editTempo(8,60) and w:editTempo(14,80),'insert bounded tempo changes')
check(w:editMeter(8,3,4) and w:editMeter(14,7,8),'insert meter changes')
local p=w.project
check(p.timeline.beats==28 and #p.timeline.tempos==3 and #p.timeline.meters==2,'song length derives from its meter map')
for bar,beat in ipairs{0,4,8,11,14,17.5,21,24.5,28} do check(near(format.barStart(p,bar),beat),'bar positions follow changing meters') end
check(format.barAt(p,14)==5 and near(format.barPosition(p,15.75),5.5),'bar ruler inverse/fractional positions')
check(near(format.seconds(p.timeline,28),20.5) and near(w:beatAt(7,'song'),11),'tempo forward/inverse clock across changes')
check(p.tracks[1].edited.bytes==notes,'timing edits preserve PPQ note data')
check(w:setTempo(100) and #w.project.timeline.tempos==3 and w.project.timeline.tempos[2].tempo==1000000,'header edits initial tempo without flattening later changes')
check(w:undo() and w.project.bpm==120,'initial tempo Undo restores complete map')
p=w.project;local plan=Sequence.compile(p,nil,MIDI,'song',format,SoundSets.programPatch)
check(plan.length==984000,'native score duration follows saved map')
local valid,grid=Sequence.validateBarGrid(plan.barGrid,plan.length)
check(valid and grid[3]==192000 and grid[4]==336000 and grid[#grid]==984000,'musical bar frames compile into transport plan')
check(Sequence.nextBoundary(grid,plan.length,false,4.1,.25)==7,'replacement waits for a musical bar after tempo/meter change')
check(Sequence.nextBoundary(grid,plan.length,true,20.6,.25)==22.5,'looped replacement uses next-cycle bar grid')
check(not Sequence.nextBoundary(grid,plan.length,false,20.4,.25),'finite song ending has no invented later bar')
check(not Sequence.validateBarGrid('bad',plan.length) and not Sequence.validateBarGrid(plan.barGrid,'bad'),'malformed grid/time types rejected without throwing')
local badGrid=plan.barGrid:sub(1,4)..plan.barGrid:sub(1,4)..plan.barGrid:sub(9)
check(not Sequence.validateBarGrid(badGrid,plan.length),'duplicate bar frames rejected')
assert(w:setComposition{loop=true,loopStart=3,loopEnd=5});local first,last=format.songRange(w.project)
check(first==8 and last==17.5,'loop bars resolve through current meter map')
local crop=Sequence.compile(w.project,nil,MIDI,'song',format,SoundSets.programPatch)
check(crop.length==414000 and near(w:playbackBeat(0),0),'cropped native duration follows tempo changes')
w.mode='song';check(w:playbackBeat(0)==8 and near(w:playbackBeat(3),11) and w:playbackBeat(8.625)==8,'cropped/looped playhead maps back onto authored bars')
local full=copy(w.project);full.composition.loop=false
local bytes=Export.encode(full,nil,MIDI,format);local song=MIDI.parse(bytes)
check(#song.tempos==3 and #song.meters==3 and song.meters[2].beat==8 and song.meters[3].denominator==8,'SMF tempo/time-signature conductor round trip')
local imported=W.new(cb);check(imported:import(song,'midi/timing.mid') and #imported.project.timeline.meters==3,'imported meter metadata is snapshotted for rulers')
check(imported:convertPart(1) and imported:editTempo(8,90),'imported tempo map is editable after note conversion')
check(imported:editMeter(9,5,8),'mid-bar meter change starts a partial/new bar')
local ip=imported.project;check(format.barStart(ip,format.barAt(ip,9))==9 and near(format.barPosition(ip,8.5),3.5),'partial-bar start and inverse mapping agree')
local gen=imported.generation;local invalid=copy(song);invalid.meters={{beat=0,numerator=3,denominator=3}}
check(not imported:import(invalid,'bad.mid') and imported.generation==gen and imported.project.midi=='midi/timing.mid','unsupported imported meter fails atomically without replacing current song')
check(imported:setTempo(110) and math.abs(imported.project.bpm-110)<.001,'imported header tempo follows the edited initial tempo')
local original=copy(w.project);assert(w:setComposition{bars=2,loopStart=1,loopEnd=2})
check(w.project.timeline.beats==8 and #w.project.timeline.tempos==3 and #w.project.timeline.meters==2,'shortening preserves later tempo/meter points')
assert(w:setComposition{bars=8});check(w.project.timeline.beats==28,'extending reveals preserved timing map')
check(w:editTempo(14,nil,true) and #w.project.timeline.tempos==2 and w:undo(),'tempo deletion is undoable')
check(w:editMeter(14,nil,nil,true) and #w.project.timeline.meters==1 and w:undo(),'meter deletion is undoable')
local before=w.generation
check(not w:editTempo(0,nil,true) and not w:editMeter(0,nil,nil,true) and not w:editMeter(10,3,3),'initial/unsupported meters reject safely')
check(not w:editTempo(8,0/0) and not w:editTempo(100000,60) and not w:editTempo(99,nil,true) and w.generation==before,'invalid/no-op timing edits preserve history')
local saved=assert(format.project(w.project));local loaded=W.new(cb);check(loaded:load(saved) and loaded.project.timeline.beats==28,'map save/load')
assert(loaded:setComposition{loop=false})
check(loaded:play('song') and loaded:editTempo(8,100) and not loaded.playing,'timing edit stops obsolete native transport')
local candidate=copy(saved);candidate.timeline.meters={{beat=4,numerator=3,denominator=4},{beat=4,numerator=4,denominator=4}}
check(not format.project(candidate),'server rejects duplicate meter times')
candidate=copy(saved);candidate.timeline.tempos[2].beat=-1;check(not format.project(candidate),'server rejects invalid tempo order')
-- Count-in at a 7/8 passage uses 3.5 quarter notes at 80 BPM.
w:stop(true);w.rollStart=56;w.selected=1
check(w:startTake(1) and near(w.recording.starts-now,2.625),'recording count-in uses local meter and tempo')
now=w.recording.starts+.75;check(near(w:recordClock(),60),'stopped recording integrates the saved map from its origin')
w:endTake(true)
local f=assert(io.open(root..'shared/project.lua','rb'));local shared=f:read('*a');f:close();f=assert(io.open('apps/openmw-server/scripts/procedural_synth_project.lua','rb'));check(shared==f:read('*a'),'server/client timing model parity');f:close()
print('PASS tempo/meter editor, ruler transforms, bar-grid replacement, crop/loop, SMF round trip, recording, history, persistence and bounds; '..checks..' assertions')
