local root='files/server/server-lua-packages/fetcher.procedural-synth/'
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local function module(name) local x=dofile(root..name:gsub('%.','/')..'.lua');package.loaded[prefix..name]=x;return x end
local schema=module('shared.schema');local factory=module('shared.project');local F=factory(schema)
local MIDI=module('client.midi');local Sequence=module('shared.sequence');module('client.soundsets');module('client.arrangement');module('client.pattern-tools')
local W=module('client.workstation');local checks=0
local function check(ok,why) checks=checks+1;assert(ok,why) end
local function make() return W.new{configure=function() end,panic=function() end,message=function() end,off=function() end,
 now=function() return 100 end,on=function() return true end,schedule=function() return true end,stopSequence=function() end} end
local function pitches(plan)
 local out={};for at=1,#plan.bytes,11 do local n=plan.bytes:byte(at+9);if n<128 then out[n]=(out[n] or 0)+1 end end;return out
end
local w=make();w:add('piano');w:toggleNote(1,60,0);w:add('drum','b:kick','Kick',36);w:toggleStep(2,0)
w.project.tracks[2].params.sample={path='sound/fx/kick.wav',root=60,offset=0,duration=1,oneShot=true,hash=string.rep('a',32)}
w:touch()
local history=#w.undoStack
check(w:capturePattern(),'whole pattern capture creates an arrangement from a legacy sequencer')
check(w.project.composition.bars==8 and #w:selectedRegions()==2,'song length is independent and captured lanes are selected together')
check(#w.undoStack==history+1,'whole pattern capture is one undo action')
for _,t in ipairs(w.project.tracks) do check(t.edited and t.arrangement and #t.arrangement.clips==1,'each lane retains its own source') end
check(w.project.tracks[2].params.sample.path=='sound/fx/kick.wav','capture retains sample assignment')
local heard=pitches(Sequence.compile(w.project,nil,MIDI,'song',F))
check(heard[60]==1 and heard[36]==1,'captured notes play once, without duplicate legacy hits')
check(w:batchRegions('duplicate') and #w:selectedRegions()==2,'multi-lane phrase duplicates together')
check(w:batchRegions('move',16*10),'arrangement can extend past the original song end')
check(w.project.composition.bars>=12 and w.project.length==16,'placement extends song without extending one-bar pattern')
check(w:undo() and w:undo() and w:undo() and w.project.tracks[1].kind=='piano','complete capture/edit undo restores original sequencer')
w:length(32);w.bar=1;w:toggleStep(2,20);w:length(16)
local before=#w.undoStack;check(not w:capturePattern() and #w.undoStack==before and w.project.tracks[2].steps:sub(21,21)~='0','hidden later bars cannot be discarded by capture')
w:length(48);w.bar=1;check(w:clearDrumBar(),'clear selected drum bar')
check(w.project.length==48 and w.project.tracks[1].notes[1].n==60,'clear preserves piano and pattern duration')
w:toggleStep(2,32);w.bar=1;before=#w.undoStack
check(w:deletePatternBar() and w.project.length==32,'delete removes one pattern bar')
check(w.project.tracks[2].steps:sub(17,17)~='0' and #w.undoStack==before+1,'later drum bar shifts left in one undo')
check(w:undo() and w.project.length==48 and w.project.tracks[2].steps:sub(33,33)~='0','deleted bars are recoverable')
local params=w.project.tracks[2].params;check(w:duplicateTrack(2),'duplicate drum lane')
check(w.project.tracks[3].params.sample.path==params.sample.path and w.project.tracks[3].steps==w.project.tracks[2].steps,'duplicate carries sample and pattern')
w.project.tracks[3].params.sample.root=72;check(w.project.tracks[2].params.sample.root==60,'duplicate is an independent deep copy')
check(w:undo() and #w.project.tracks==2,'track duplication undo')
local p=make();p:newSong(120,4,4,8);p:addNote(1,67,0);p:add('drum','b:kick','Kick',36);p:toggleStep(2,0)
heard=pitches(Sequence.compile(p.project,nil,MIDI,'clips',F))
check(heard[67]==1 and heard[36]==1,'Play clips includes authored piano-roll notes with drum patterns')
p:selectAllNotes(1);check(p:captureClip(1),'piano phrase becomes reusable source')
heard=pitches(Sequence.compile(p.project,nil,MIDI,'clips',F));check(heard[67]==1,'placed piano clip is audible in Play clips')
check(p:editClip(1,1),'edit reusable source')
-- compilePlan uses the current playback mode; select the dedicated source audition.
p.mode='pattern';heard=pitches(p:compilePlan());check(heard[67]==1 and not heard[36],'source audition isolates the selected clip')
local reference={path='sound/fx/kick.wav',root=60,offset=0,duration=1,oneShot=true,hash=string.rep('a',32)}
local drums=make();drums:add('drum','b:kick','Kick',36)
check(drums:setSample(1,reference) and drums.project.tracks[1].note==60,'sample assignment aligns drum trigger with original-speed root')
check(drums:undo() and drums.project.tracks[1].note==36 and not drums.project.tracks[1].params.sample,'sample pitch correction is undoable')
drums.native=true;drums.playing=true;drums.sequenceDirty=false
check(drums:setSample(1,reference) and drums.sequenceDirty,'root-note changes replace a running pattern at the normal boundary')
check(drums:preset(1,'b:kick') and drums.project.tracks[1].note==36,'switching a sample lane back to kick restores its drum reference pitch')
local combined=make();combined:newSong(120,4,4,8);combined:addNote(1,72,0);combined:addNote(1,74,32)
combined:add('drum','b:kick','Kick',36);combined:toggleStep(2,0)
check(combined:capturePattern() and #combined:selectedRegions()==2,'whole pattern captures authored piano-roll notes with drum lanes')
check(#combined:noteData(1)==1 and combined:noteData(1)[1].s==32,'later linear song notes survive first-pattern capture')
heard=pitches(Sequence.compile(combined.project,nil,MIDI,'clips',F));check(heard[72]==1 and heard[36]==1 and not heard[74],'clip playback uses the displayed pattern length')
heard=pitches(Sequence.compile(combined.project,nil,MIDI,'song',F));check(heard[74]==1,'song playback retains later piano-roll notes')
local exported=make();exported:newSong(120,4,4,8);exported:addNote(1,64,0)
local Export=module('client.midi-export');local song=assert(MIDI.parse(Export.encode(exported.project,nil,MIDI,F)))
local imported=make();check(imported:import(song,'midi/duplicate-test.mid'),'import duplicate-test song')
check(imported:duplicateTrack(1) and imported.project.tracks[2].edited,'unedited MIDI duplicates to an independent editable part')
heard=pitches(Sequence.compile(imported.project,song,MIDI,'song',F))
check(heard[64]==2,'both original MIDI part and independent duplicate are audible')
for n=#imported.project.tracks+1,F.maxTracks do imported:add('piano') end
history=#imported.undoStack;check(not imported:duplicateTrack(1) and #imported.project.tracks==F.maxTracks and #imported.undoStack==history,'track budget rejects duplication atomically')
check(F.maxTracks==32 and Sequence.maxTracks==32,'document and score agree on the expanded track limit')
local expanded=make();expanded:newSong(120,4,4,8)
for n=2,F.maxTracks do expanded:addPart('keys') end
check(expanded:addNote(32,79,0) and expanded:setOutput(32,8),'track 32 edits and routes to a band member')
heard=pitches(Sequence.compile(expanded.project,nil,MIDI,'song',F));check(heard[79]==1,'track 32 reaches native score compilation')
local roundtrip=assert(MIDI.parse(Export.encode(expanded.project,nil,MIDI,F)))
local destination=make();check(destination:import(roundtrip,'midi/32-parts.mid'),'32-part MIDI round trip imports')
print('PASS Studio usability: '..checks..' assertions')
