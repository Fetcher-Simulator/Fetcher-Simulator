local root='files/server/server-lua-packages/fetcher.procedural-synth/';local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=dofile(root..'shared/schema.lua');local factory=dofile(root..'shared/project.lua');local f=factory(schema)
local MIDI=dofile(root..'client/midi.lua');local Sequence=dofile(root..'shared/sequence.lua')
package.loaded[prefix..'shared.sequence']=Sequence;package.loaded[prefix..'shared.schema']=schema;package.loaded[prefix..'shared.project']=factory
package.loaded[prefix..'client.midi']=MIDI;package.loaded[prefix..'client.soundsets']=dofile(root..'client/soundsets.lua')
package.loaded[prefix..'client.arrangement']=dofile(root..'client/arrangement.lua')
local W=dofile(root..'client/workstation.lua');local Export=dofile(root..'client/midi-export.lua')
local function copy(t) if type(t)~='table' then return t end;local o={};for k,v in pairs(t) do o[k]=copy(v) end;return o end
local checks=0;local function check(b,m) checks=checks+1;assert(b,m) end
local function make() return W.new{configure=function() end,panic=function() end,message=function() end,schedule=function() return true end,stopSequence=function() end} end
local w=make();assert(w:newSong(120,4,4,16));assert(w:addNote(1,60,0));w:selectAllNotes(1);assert(w:captureClip(1))
assert(w:duplicateRegion(1,1));assert(w:addPart('bass'));assert(w:setOutput(2,2));assert(w:setMixer(2,'bass',3))
assert(w:addNote(2,36,32));w:selectAllNotes(2);assert(w:captureClip(2))
local function region(track,id) return w.project.tracks[track].arrangement.regions[id] end
w:selectRegion(1,1,false);w:selectRegion(2,1,true)
check(#w:selectedRegions()==2,'Shift selection spans tracks')
local h=#w.undoStack;local gen=w.generation
check(w:batchRegions('move',16) and region(1,1).s==16 and region(2,1).s==48 and region(1,2).s==16,'group move changes only selected placements')
check(#w.undoStack==h+1 and w.generation==gen+1,'group commit is one history/generation action')
check(w:undo() and region(1,1).s==0 and region(2,1).s==32 and w:redo(),'group history restores every track')
w:selectRegion(1,1,false);w:selectRegion(2,1,true)
check(w:batchRegions('move',-100) and region(1,1).s==0 and region(2,1).s==32,'left clamp preserves inter-track spacing')
local before=region(2,1).s;gen=w.generation
check(not w:batchRegions('move',1e10) and region(2,1).s==before and w.generation==gen,'over-budget group move is atomic')
check(not w:batchRegions('pitch',100) and region(1,1).transpose==0,'invalid pitch on any selected region preserves whole group')
w:selectRegion(2,1,true);check(#w:selectedRegions()==1,'Shift-click toggles selected placement off')
w:selectRegion(1,1,true);check(#w:selectedRegions()==0 and not w:batchRegions('remove'),'empty explicit selection cannot fall back to stale primary')
w:selectAllRegions();check(#w:selectedRegions()==3,'Select all spans both track pages')
h=#w.undoStack;assert(w:batchRegions('duplicate'))
check(#w:selectedRegions()==3 and #w.undoStack==h+1 and #w.project.tracks[1].arrangement.clips==1,'group duplication links original sources in one edit')
check(region(1,3).s==48 and region(2,2).s==80,'duplicate preserves spacing after complete selection span')
check(w:batchRegions('unique') and #w.project.tracks[1].arrangement.clips==3 and #w.project.tracks[2].arrangement.clips==2,'each selected placement receives an independent source')
local source=w.project.tracks[1].arrangement.clips[1].notes.bytes
assert(w:editClip(1,region(1,3).clip));w:selectAllNotes(1);assert(w:editSelected(1,{pitch=2}));assert(w:leaveClip())
check(w.project.tracks[1].arrangement.clips[1].notes.bytes==source,'unique-source note editing preserves original linked copies')
local destination=copy(w.project.tracks[2]);h=#w.undoStack
check(w:copyClip(1,region(1,3).clip,2) and w.selected==2 and w.arrangeClip==3,'copy source selects destination library')
check(w.project.tracks[2].output.slot==2 and w.project.tracks[2].mixer.bass==3 and w.project.tracks[2].presetId==destination.presetId,'source transfer retains destination instrument/mix/band route')
check(#w.project.tracks[2].arrangement.regions==2 and #w.undoStack==h+1,'copying source creates no unsolicited placement')
assert(w:placeClip(2,3,96));local transferred=w.project.tracks[2].arrangement.clips[3].notes.bytes
assert(w:editClip(1,region(1,3).clip));w:selectAllNotes(1);assert(w:editSelected(1,{pitch=1}));assert(w:leaveClip())
check(w.project.tracks[2].arrangement.clips[3].notes.bytes==transferred,'transferred source is a snapshot independent of source track')
w:selectRegion(1,1,false);w:selectRegion(2,1,true);h=#w.undoStack
check(w:batchRegions('remove') and #w.undoStack==h+1 and #w:selectedRegions()==0,'multi-track removal resets selection as one edit')
check(#w.project.tracks[1].arrangement.regions==3 and #w.project.tracks[2].arrangement.regions==2 and w:undo(),'descending index deletion and Undo keep unselected placements')
-- Capacity failures must not leave earlier clones in another track behind.
for lane=1,2 do while #w.project.tracks[lane].arrangement.clips<8 do assert(w:newClip(lane)) end end
w:selectRegion(1,1,false);w:selectRegion(2,1,true);gen=w.generation
check(not w:batchRegions('unique') and w.generation==gen and region(1,1).clip==1,'source-capacity rejection changes neither group nor history')
check(not w:copyClip(1,1,2) and w.generation==gen,'full destination rejects source transfer')
w.recording={};check(not w:batchRegions('move',4) and w.generation==gen,'active take locks group edits');w.recording=nil
-- Phase offsets chase a held gate at the start, repeat on wrap and crop at end.
local p=f.new();p.timeline={beats=64,tempos={{beat=0,tempo=500000},{beat=4,tempo=1000000}}}
local t=f.newTrack('midi','Offsets',16);t.edited=f.encodeNotes({},960);p.tracks[1]=t
t.arrangement={clips={{name='Held phrase',length=16,notes=f.encodeNotes({{n=60,s=0,l=6,v=1},{n=64,s=8,l=4,v=.8}},960)}},
    regions={{clip=1,s=16,l=20,offset=4,transpose=0,mute=false}}}
p=assert(f.project(p));t=p.tracks[1]
local emitted={};f.eachRegionNote(t,function(n,s,l,v) emitted[#emitted+1]={n=n,s=s,l=l,v=v} end)
check(#emitted==3 and f.regionNoteCount(t)==3,'offset count includes initial held note and clipped last repetition')
check(emitted[1].s==16 and emitted[1].l==2 and emitted[2].s==20 and emitted[3].s==28 and emitted[3].l==6,'offset chase/wrap gates retain quarter-note phase')
local plan=Sequence.compile(p,nil,MIDI,'song',f);local parsed=MIDI.parse(Export.encode(p,nil,MIDI,f))
local spans=MIDI.noteSpans(parsed);local exported=spans[MIDI.parts(parsed)[1].id]
check(#plan.bytes==33 and #exported==3 and exported[1].s==16 and exported[1].l==2,'native score and MIDI export share offset expansion')
local loaded=make();check(loaded:load(p) and region(1,1)~=nil and loaded.project.tracks[1].arrangement.regions[1].offset==4,'offset survives saved project load')
check(loaded:play('song') and loaded:changeRegion(1,1,{offset=8}) and loaded.sequenceDirty,'live source offset edits queue native score replacement')
for _,offset in ipairs{-1,16,math.huge,false,'4'} do local bad=copy(p);bad.tracks[1].arrangement.regions[1].offset=offset;check(not f.project(bad),'invalid source offset rejected') end
local legacy=copy(p);legacy.tracks[1].arrangement.regions[1].offset=nil
check(f.project(legacy).tracks[1].arrangement.regions[1].offset==0,'old placements migrate to zero offset')
for offset=0,15,.25 do for length=.25,34,2.25 do
    local q=copy(p);local r=q.tracks[1].arrangement.regions[1];r.offset=offset;r.l=length;q=assert(f.project(q))
    local count=0;f.eachRegionNote(q.tracks[1],function(_,s,l) count=count+1;assert(s>=16 and s<16+length and l>=0 and s+l<=16+length+1e-8) end)
    check(count==f.regionNoteCount(q.tracks[1]),'offset expansion and bounded preflight agree at partial loops')
end end
local mixed=make();assert(mixed:newSong(120,4,4,8));mixed:addNote(1,60,16);mixed:selectAllNotes(1);mixed:captureClip(1)
mixed:addPart('bass');mixed:addNote(2,36,32);mixed:selectAllNotes(2);mixed:captureClip(2);assert(mixed:editMeter(8,3,4))
mixed:selectRegion(1,1,false);mixed:selectRegion(2,1,true)
check(mixed:moveRegions(1) and mixed.project.tracks[1].arrangement.regions[1].s==28 and mixed.project.tracks[2].arrangement.regions[1].s==44,'mixed-meter nudge uses primary bar while preserving group quarter-note spacing')
local server=dofile('apps/openmw-server/scripts/procedural_synth_project.lua')(schema)
check(server.project(p).tracks[1].arrangement.regions[1].offset==4 and Sequence.compile(p,nil,MIDI,'song',server).bytes==plan.bytes,'server/client offset schema and compilation parity')
print('PASS multi-track selection/groups, unique sources, transfers, history/atomic bounds, offset chase/repeat/export/persistence; '..checks..' assertions')
