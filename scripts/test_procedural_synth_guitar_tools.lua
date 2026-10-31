local root='files/server/server-lua-packages/fetcher.procedural-synth/'
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local Tools=dofile(root..'client/guitar-tools.lua');local schema=dofile(root..'shared/schema.lua')
local factory=dofile(root..'shared/project.lua');local f=factory(schema);local MIDI=dofile(root..'client/midi.lua')
local Sequence=dofile(root..'shared/sequence.lua');local Export=dofile(root..'client/midi-export.lua')
package.loaded[prefix..'shared.schema']=schema;package.loaded[prefix..'shared.project']=factory
package.loaded[prefix..'shared.sequence']=Sequence;package.loaded[prefix..'client.midi']=MIDI
package.loaded[prefix..'client.soundsets']=dofile(root..'client/soundsets.lua')
package.loaded[prefix..'client.arrangement']=dofile(root..'client/arrangement.lua')
package.loaded[prefix..'client.guitar-tools']=Tools
local W=dofile(root..'client/workstation.lua')
local checks=0;local function check(b,m) checks=checks+1;assert(b,m) end
local function copy(t) if type(t)~='table' then return t end;local o={};for k,v in pairs(t) do o[k]=copy(v) end;return o end
local function same(a,b)
    if type(a)~='table' or type(b)~='table' then return a==b end
    for k,v in pairs(a) do if not same(v,b[k]) then return false end end
    for k in pairs(b) do if a[k]==nil then return false end end;return true
end
local clock={quantum=1/960,seconds=function(s) return s/8 end,shift=function(s,delta) return s+delta*8 end}
local function chord(step,pitches,length)
    local notes={};for _,n in ipairs(pitches or {40,47,52,56,59,64}) do notes[#notes+1]={n=n,s=step or 0,l=length or 4,v=.8} end;return notes
end
local function append(a,b) for _,n in ipairs(b) do a[#a+1]=n end;return a end
local original=chord();local plan=assert(Tools.plan(original,{},clock))
check(same(original,chord()) and plan.shifted==5,'preview is pure and strums five delayed picks')
local frets={0,2,2,1,0,0}
for n,row in ipairs(plan.rows) do
    check(row.string==7-n and row.fret==frets[n],'open E-major gets a playable low-position grip')
    check(plan.notes[n].n==original[n].n and plan.notes[n].v==original[n].v,'pitches and velocities stay intact')
    check(math.abs(plan.notes[n].s+plan.notes[n].l-4)<1e-10,'strums retain original note ends')
end
check(math.abs(plan.rows[6].delay-40)<.1,'eight milliseconds per string spans forty milliseconds')
local up=assert(Tools.plan(original,{direction=1},clock));check(up.notes[6].s==0 and up.notes[1].s>0,'upstroke reverses physical string order')
local alternate=assert(Tools.plan(append(chord(),chord(8)),{direction=2},clock))
check(not alternate.rows[1].up and alternate.rows[7].up,'alternate flips once per edited gesture')
local zero=assert(Tools.plan(original,{spacing=0},clock));check(same(zero.notes,original),'zero spacing preserves an already playable chord')
check(not Tools.plan(chord(0,{20}),{},clock),'unplayable pitches are rejected without octave changes')
check(not Tools.plan(chord(0,{40,40}),{},clock),'duplicate unisons are rejected instead of silently collapsing native voices')
check(not Tools.plan(chord(0,{40,45,50,55,59,64,69}),{},clock),'oversized six-string chords are not dropped or thinned')
check(not Tools.plan(chord(0,{41,87}),{span=5},clock),'wide impossible stretches reject atomically')
check(not Tools.plan(chord(0,nil,.01),{spacing=25},clock),'short notes cannot be erased by pick delay')
check(not Tools.plan(append(chord(),chord(.1)),{spacing=25},clock),'a strum cannot cross the next attack')
for _,profile in ipairs(Tools.profiles) do
    local p=assert(Tools.plan(chord(0,{profile.opens[1]}),{profile=profile.id},clock))
    check(p.rows[1].fret==0 and p.rows[1].string==#profile.opens,'every tuning exposes its open low string')
end
local bass=assert(Tools.plan(chord(0,{40}),{profile='bass'},clock,nil,-12))
check(bass.rows[1].string==4 and bass.rows[1].fret==0 and bass.notes[1].n==40,'bass Tune offset affects fingering, not written pitch')
check(not Tools.plan(chord(),{},clock,nil,.5),'fractional Tune offsets reject clearly')
local overlap={{n=40,s=0,l=4,v=.7},{n=42,s=2,l=4,v=.6}}
local choked=assert(Tools.plan(overlap,{},clock));check(choked.choked==1 and choked.notes[1].l==2 and choked.notes[2].l==4,'reusing a string shortens only its previous selected gate')
check(not Tools.plan(overlap,{scope='selected'},clock,{[2]=true}),'selected-note mode cannot shorten an unselected held note')
local protected=assert(Tools.plan(overlap,{scope='selected'},clock,{[1]=true}))
check(same(protected.notes[2],overlap[2]),'unselected note values stay exact')
check(not Tools.plan(original,{scope='selected'},clock,{}),'empty selected scope is explicit, not all-note fallback')
check(not Tools.options{spacing=0/0} and not Tools.options{span=13} and not Tools.options{direction=.5},'invalid settings reject')
local long={};for n=0,4095 do long[#long+1]={n=40,s=n*2,l=1,v=.8} end
local start=os.clock();local large=assert(Tools.plan(long,{},clock))
check(#large.rows==4096 and large.search<500000,'full edited-part budget stays within bounded search')
print(string.format('4096-note fingering plan: %.4fs (informational)',os.clock()-start))
long[#long+1]={n=40,s=8192,l=1,v=.8};check(not Tools.plan(long,{},clock),'over-budget input rejects')
local function make()
    local messages={};local w=W.new{configure=function() end,panic=function() end,message=function(m) messages[#messages+1]=m end,schedule=function() return true end,stopSequence=function() end}
    assert(w:newSong(120,4,4,8));assert(w:storeNotes(1,chord()));w.project.tracks[1].instrument='guitar';w:touch()
    assert(w:openGuitarTools());return w,messages
end
local w=make();local before=copy(w.project);local undo=#w.undoStack;local generation=w.generation
check(w:previewGuitar() and same(w.project,before) and #w.undoStack==undo and w.generation==generation,'workstation preview never changes project/history or playback')
w.playing=true;w.native=true;w.sequenceDirty=false
check(w:applyGuitar() and #w.undoStack==undo+1 and w.sequenceDirty,'apply commits once and schedules a native live replacement')
check(same(w.project.tracks[1].params,before.tracks[1].params) and same(w.project.band,before.band) and same(w.project.effects,before.effects),'sound, effects and physical band routing are unchanged')
local applied=copy(w.project);local validated=assert(f.project(applied));local saved=assert(f.project(copy(validated)))
check(same(saved.tracks[1].edited,validated.tracks[1].edited),'source-free validation/reload retains transformed notes')
local score=Sequence.compile(saved,nil,MIDI,'song',f)
local frames={};local function u32(s,a) local x,y,z,k=s:byte(a,a+3);return x+y*256+z*65536+k*16777216 end
for at=1,#score.bytes,11 do if score.bytes:byte(at+9)<128 then frames[#frames+1]=u32(score.bytes,at) end end
check(#frames==6 and frames[1]==0 and frames[6]>frames[1],'native score contains six real staggered note attacks')
local exported=assert(MIDI.parse(Export.encode(saved,nil,MIDI,f)))
local parts=MIDI.parts(exported);local spans=MIDI.editableSpans(exported,parts[1].id)
check(#spans==6 and spans[6].s>spans[1].s,'MIDI export/reimport keeps the strum timing')
check(w:undo() and same(w.project,before) and w:redo() and same(w.project,applied),'single Undo/Redo restores exact documents')
local noOp=make();noOp.guitarOptions.spacing=0;assert(noOp:previewGuitar());local noOpHistory=#noOp.undoStack
check(noOp:applyGuitar() and #noOp.undoStack==noOpHistory,'a no-change fingering does not manufacture an Undo entry')
assert(w:previewGuitar());local current=copy(w.project);w.guitarOptions.spacing=12
check(not w:applyGuitar() and same(w.project,current),'settings changes invalidate preview atomically')
assert(w:previewGuitar());assert(w:setTrack(1,'level',.5));current=copy(w.project)
check(not w:applyGuitar() and same(w.project,current),'project edits invalidate stale previews')
w.guitarOptions.scope='selected';w:selectAllNotes(1);assert(w:previewGuitar());w:selectNote(1,1)
check(not w:applyGuitar(),'changed selections invalidate previews')
w.failedTake={};check(not w:previewGuitar(),'failed takes are protected')
local tempo=make();tempo.project.timeline={beats=32,tempos={{beat=0,tempo=500000},{beat=.25,tempo=1000000}}};assert(tempo:storeNotes(1,chord(.9)))
assert(tempo:previewGuitar());local row=tempo.guitarPreview.rows[6]
check(math.abs(row.delay-40)<.6 and row.step>1.10 and row.step<1.12,'millisecond spacing follows the real tempo map across a boundary')
local linked=make();linked:selectAllNotes(1);assert(linked:captureClip(1));assert(linked:duplicateRegion(1,1));assert(linked:editClip(1,1))
local placements=copy(linked.project.tracks[1].arrangement.regions);assert(linked:openGuitarTools());assert(linked:previewGuitar());assert(linked:applyGuitar())
check(same(linked.project.tracks[1].arrangement.regions,placements) and #linked.project.tracks[1].arrangement.clips==1,'linked-source edit preserves every placement/reference')
check(f.project(linked.project)~=nil,'linked strummed source remains a valid saved arrangement')
local readOnly=make();readOnly.project.tracks[1].edited=nil;local untouched=copy(readOnly.project)
check(not readOnly:previewGuitar() and same(readOnly.project,untouched),'unconverted MIDI stays read-only until explicit conversion')
print(string.format('PASS string-aware voicing, strum directions, gates, tuning, bounded search, preview/Undo, tempo, source-free MIDI and linked clips; %d assertions',checks))
