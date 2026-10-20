-- Imported MIDI editing must preserve sound/routing, source timing and saved intent.
local root='files/server/server-lua-packages/fetcher.procedural-synth/'
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=dofile(root..'shared/schema.lua');local factory=dofile(root..'shared/project.lua');local format=factory(schema)
local MIDI=dofile(root..'client/midi.lua');local Sequence=dofile(root..'shared/sequence.lua')
package.loaded[prefix..'shared.schema']=schema;package.loaded[prefix..'shared.project']=factory
package.loaded[prefix..'client.midi']=MIDI;package.loaded[prefix..'shared.sequence']=Sequence
package.loaded[prefix..'client.soundsets']=dofile(root..'client/soundsets.lua')
local W=dofile(root..'client/workstation.lua');local checks=0
local function check(v,msg) checks=checks+1;assert(v,msg) end
local function near(a,b) return math.abs(a-b)<0.00001 end
local function copy(v) if type(v)~='table' then return v end;local out={};for k,x in pairs(v) do out[k]=copy(x) end;return out end
local song={division=480,tracks=2,names={'Keys','Bass'},programs={},beats=80,duration=60,tempos={{beat=0,time=0,tempo=500000},{beat=40,time=20,tempo=1000000}},events={
    {tick=1,time=1/960,kind='on',channel=0,track=1,part='1:0',note=60,velocity=100/127},
    {tick=240,time=0.25,kind='control',channel=0,track=1,part='1:0',note=64,velocity=127},
    {tick=480,time=0.5,kind='off',channel=0,track=1,part='1:0',note=60},
    {tick=960,time=1,kind='control',channel=0,track=1,part='1:0',note=64,velocity=0},
    {tick=19200,time=20,kind='on',channel=1,track=2,part='2:1',note=48,velocity=90/127},
    {tick=19680,time=21,kind='off',channel=1,track=2,part='2:1',note=48},
    {tick=36480,time=56,kind='on',channel=0,track=1,part='1:0',note=72,velocity=80/127},
    {tick=36960,time=57,kind='off',channel=0,track=1,part='1:0',note=72},
}}
local messages,queued,imported,saved={},0,0,nil
local cb={configure=function() end,panic=function() end,band=function() end,prune=function() end,
    message=function(m) messages[#messages+1]=m end,save=function(p) saved=copy(p) end,
    importMidi=function() imported=imported+1 end,schedule=function(plan) queued=queued+1;return true end,
    stopSequence=function() end,progress=function() return true,1,0 end}
local w=W.new(cb);check(w:import(song,'midi/edit.mid'),'source import')
w:preset(1,'b:guitar-crunch');w:setMixer(1,'bass',5);w:setOutput(1,2)
w.project.band[2].target={kind='player',characterId=1002,instrument='lute'};w:touch()
local original=Sequence.compile(w.project,song,MIDI,'song',format)
local before=#w.undoStack
check(w:convertPart(1) and #w.undoStack==before+1,'conversion is one undo action')
check(w.project.tracks[1].presetId=='b:guitar-crunch' and w.project.tracks[1].mixer.bass==5 and w.project.tracks[1].output.slot==2 and w.project.band[2].target.characterId==1002,'conversion preserves complete sound, mix and actor routing')
local data=w:noteData(1)
check(#data==2 and near(data[1].s,1/120) and near(data[1].l,959/120),'off-grid onset and sustain release retain exact source PPQ')
check(data[2].s==304 and data[2].l==4,'later bars survive the sixteen-bar clip limit')
local converted=Sequence.compile(w.project,song,MIDI,'song',format)
check(converted.bytes==original.bytes and converted.length==original.length,'conversion produces identical native PCM note timing/gates')
check(w:undo() and not w.project.tracks[1].edited and w:redo() and w.project.tracks[1].edited,'conversion undo/redo restores source-vs-edited identity')
w:selectNote(1,2);check(w:editSelected(1,{step=4,pitch=-2,length=2}),'move/resize selected notes')
check(w:noteData(1)[2].s==308 and w:noteData(1)[2].n==70 and w:noteData(1)[2].l==6,'move and resize change only selected note')
check(w:noteData(1)[1].s==data[1].s and w:undo() and w:noteData(1)[2].n==72,'other notes unchanged and gesture undoes atomically')
w:selectNote(1,1);w:selectNote(1,2,true)
check(w:editSelected(1,{velocity=0.5}) and near(w:noteData(1)[1].v,64/127) and near(w:noteData(1)[2].v,64/127),'velocity edits apply to the selection at MIDI precision')
local bytes=w.project.tracks[1].edited.bytes
check(not w:editSelected(1,{pitch=1000}) and w.project.tracks[1].edited.bytes==bytes,'invalid group transform is atomic')
local earliest=w:copyNotes(1);check(near(earliest,1/120),'clipboard retains relative timing')
check(w:pasteNotes(1,320) and #w:noteData(1)==4 and #w:selectedNotes(1)==2,'paste beyond original end selects its copied notes')
local extended=Sequence.compile(w.project,song,MIDI,'song',format)
check(extended.length>original.length,'edited notes beyond source end extend the song without clipping')
check(w:deleteNotes(1) and #w:noteData(1)==2,'delete removes the pasted selection')
w:selectAllNotes(1);check(w:deleteNotes(1) and #w:noteData(1)==0 and w.project.tracks[1].edited.bytes=='','empty edited parts retain an explicit override')
local empty=Sequence.compile(w.project,song,MIDI,'song',format)
check(#empty.bytes==11 and empty.bytes:byte(9)==2,'deleting a converted part never replays its source notes')
check(w:undo() and #w:noteData(1)==2,'undo restores deleted notes')
check(w:convertPart(2),'second part converts independently')
check(w:save() and saved.timeline.tempos[2].tempo==1000000,'tempo map and binary edits save with the project')
local loaded=W.new(cb);local count=imported
check(loaded:load(saved) and imported==count and not loaded.song and loaded:hasSong(),'fully edited project loads without reopening the original MIDI')
check(loaded:play('song') and near(loaded:beatAt(21),41),'fully edited project follows its saved tempo map')
local roundtrip=Sequence.compile(loaded.project,nil,MIDI,'song',format)
local sourced=Sequence.compile(w.project,song,MIDI,'song',format)
check(roundtrip.bytes==sourced.bytes and roundtrip.length==sourced.length,'saved edits render identically without the source file')
check(loaded:applySoundSet('balanced') and loaded.project.tracks[2].sourceRole=='bass' and loaded.project.tracks[2].params.tune1==0,'sound-set role and MIDI octave compensation survive source-free reload')
loaded:stop(true);loaded:selectNote(1,1);loaded:editSelected(1,{pitch=1});loaded:play('song')
local was=queued;loaded.awaiting=false;loaded:selectNote(1,1);loaded:editSelected(1,{pitch=1});loaded:frame(0.3)
check(queued==was+1 and loaded.playing,'live note edits use the existing native bar replacement')
local partial=copy(saved);partial.tracks[2].edited=nil;count=imported
local missing=W.new(cb);missing:load(partial)
check(imported==count+1 and not missing:play('song'),'mixed projects still require source MIDI for unedited parts')
local invalid=copy(saved);invalid.tracks[1].edited.bytes='bad';check(not format.project(invalid),'malformed packed notes rejected')
invalid=copy(saved);invalid.tracks[1].edited.division=0;check(not format.project(invalid),'invalid PPQ rejected')
invalid=copy(saved);invalid.timeline.tempos[2].beat=-1;check(not format.project(invalid),'nonmonotonic/negative tempo map rejected')
invalid=copy(saved);invalid.timeline.tempos[2].time=123;check(not format.project(invalid),'forged tempo seconds rejected; clock is derived from beats')
invalid=copy(saved);invalid.timeline=nil;check(not format.project(invalid),'edited notes cannot lose their tempo snapshot')
invalid=copy(saved);invalid.tracks[1].edited=false;check(not format.project(invalid),'false override cannot silently resurrect source')
local many={};for n=1,4096 do many[n]={n=60,s=(n-1)*4,l=1,v=1} end
local packed=assert(format.encodeNotes(many,96));check(#packed.bytes==40960,'thousands of notes have a bounded compact wire representation')
many[4097]=copy(many[1]);check(not format.encodeNotes(many,96),'oversized part refused without truncation')
invalid=copy(saved);invalid.tracks={};invalid.timeline={beats=16384,tempos={{beat=0,tempo=100000}}}
for n=1,3 do invalid.tracks[n]=format.newTrack('midi','Dense part',16);invalid.tracks[n].edited=packed end
check(not format.project(invalid),'combined edited project limit enforced')
local large=copy(song);large.events={}
for n=1,4097 do large.events[n]={kind='on',tick=n,channel=0,track=1,part='1:0',note=60,velocity=1,time=n/960} end
local oversized=W.new(cb);oversized:import(large,'midi/large.mid');local old=copy(oversized.project)
check(not oversized:convertPart(1) and not oversized.project.tracks[1].edited and oversized.project.timeline==nil and oversized.project.tracks[1].part==old.tracks[1].part,'conversion refusal preserves original part and project')
local percussion=copy(song);percussion.names={};percussion.events={
    {kind='on',tick=0,time=0,channel=9,track=1,part='1:9:38',note=38,velocity=1},
    {kind='off',tick=96,time=0.1,channel=9,track=1,part='1:9:38',note=38}}
local drums=W.new(cb);drums:import(percussion,'midi/drums.mid');drums:convertPart(1);local document=assert(format.project(drums.project));local reloaded=W.new(cb);reloaded:load(document)
check(reloaded:applySoundSet('balanced') and reloaded.project.tracks[1].params.drumType==2,'edited GM percussion stays a snare when cycling sound sets without source MIDI')
local zero=copy(song);zero.beats=4;zero.duration=2;zero.tempos={};zero.events={
    {kind='on',tick=0,time=0,channel=0,track=1,part='1:0',note=60,velocity=1},
    {kind='off',tick=0,time=0,channel=0,track=1,part='1:0',note=60},
    {kind='on',tick=1920,time=2,channel=0,track=1,part='1:0',note=61,velocity=1}}
local z=W.new(cb);z:import(zero,'midi/zero.mid');local beforeZero=Sequence.compile(z.project,zero,MIDI,'song',format)
check(z:convertPart(1) and #z:noteData(1)==1 and z:noteData(1)[1].l==0,'same-tick gates stay impulses and end-marker onsets remain excluded')
local afterZero=Sequence.compile(z.project,zero,MIDI,'song',format)
check(beforeZero.bytes==afterZero.bytes and beforeZero.length==afterZero.length,'zero-gate conversion preserves native one-sample duration without extending song')
print('PASS MIDI conversion, selection edits, sustain/PPQ/tempo, native rendering, routing, undo, saved overrides and bounded validation; '..checks..' assertions')
