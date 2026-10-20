local root='files/server/server-lua-packages/fetcher.procedural-synth/';local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=dofile(root..'shared/schema.lua');local factory=dofile(root..'shared/project.lua');local format=factory(schema)
local MIDI=dofile(root..'client/midi.lua');local Sequence=dofile(root..'shared/sequence.lua')
package.loaded[prefix..'shared.sequence']=Sequence
package.loaded[prefix..'shared.schema']=schema;package.loaded[prefix..'shared.project']=factory
package.loaded[prefix..'client.midi']=MIDI;package.loaded[prefix..'client.soundsets']=dofile(root..'client/soundsets.lua')
local W=dofile(root..'client/workstation.lua');local checks=0
local function check(value,message) checks=checks+1;assert(value,message) end
local function copy(t) if type(t)~='table' then return t end;local out={};for k,v in pairs(t) do out[k]=copy(v) end;return out end
local function u32(s,at) local a,b,c,d=s:byte(at,at+3);return a+b*256+c*65536+d*16777216 end
local clock,position,playing,changes=100,0,true,0;local saved,queued
local function make()
    return W.new{configure=function() end,panic=function() end,message=function() end,on=function() return true end,off=function() end,
        now=function() return clock end,progress=function() return playing,position,0 end,documentChanged=function() changes=changes+1 end,
        schedule=function(p) queued=p;return true end,stopSequence=function() end,save=function(p) saved=p end}
end
local w=make();w:add('piano');w:addNote(1,67,0);local history=#w.undoStack
w:setMember(1,{name='Band mate',level=0.8,mute=false,target={kind='player',characterId=22,instrument='lute'}})
check(not w:newSong(120,0,4,8) and not w:newSong(120,4,3,8) and not w:newSong(40,12,2,256),'invalid settings and overlong songs leave the document intact')
check(w:newSong(120,3,4,8) and w.project.composition.bars==8 and w.project.timeline.beats==24,'blank authored song with explicit meter and length')
check(w.project.midi=='' and #w:noteData(1)==0 and not w:needsMidi() and w:hasSong(),'new song never requires a source MIDI file')
check(w.project.band[1].target.characterId==22 and w.project.tracks[1].presetId=='b:studio-keys','new document keeps the band roster and starts with a piano sound')
check(w:undo() and w.project.tracks[1].kind=='piano' and w:noteData(1)[1].n==67 and w:redo() and w.project.composition,'New Song is an undoable document replacement')
check(changes>=3,'new/undo/redo invalidate pending file selections')
check(w:addPart('snare') and w.project.tracks[2].sourceRole=='snare' and w.project.tracks[2].params.drumType==2,'drum song parts have their own native synth voice')
check(not w:addPart('arbitrary'),'unknown musical roles rejected')
w:setOutput(2,1);check(w:addNote(1,60,0) and w:addNote(1,64,4) and w:addNote(2,38,12),'melodic chords and drum notes can be authored')
w.noteLength=8;w:addNote(1,67,80)
check(w:setComposition{bars=2,loopEnd=2} and #w:noteData(1)==3,'shortening the song preserves later notes')
local plan=Sequence.compile(w.project,nil,MIDI,'song',format)
check(plan.length==144000 and #plan.bytes==33 and not plan.loop,'whole-song playback ends at authored length, excluding preserved later notes')
check(w:setComposition{bars=8,loopEnd=8} and #Sequence.compile(w.project,nil,MIDI,'song',format).bytes==44,'extending the song reveals preserved notes')
check(w:setTempo(60) and w.project.timeline.tempos[1].tempo==1000000,'tempo changes update the authoritative saved clock')
check(w:undo() and w.project.bpm==120 and w.project.timeline.tempos[1].tempo==500000,'tempo and clock undo together')
w:redo();w:setComposition{loop=true,loopStart=2,loopEnd=3}
plan=Sequence.compile(w.project,nil,MIDI,'song',format)
check(plan.loop and plan.length==288000 and plan.quantum==144000,'selected two-bar region loops with a meter-sized replacement quantum')
check(#plan.bytes==11 and u32(plan.bytes,1)==0 and plan.bytes:byte(10)==38,'region is rebased to zero without replaying earlier parts')
w.noteLength=16;w:addNote(1,72,8)
plan=Sequence.compile(w.project,nil,MIDI,'song',format)
check(#plan.bytes==22 and u32(plan.bytes,1)==0 and u32(plan.bytes,5)==144000,'notes sustaining across region start retain their remaining gate')
w.noteLength=12;w:addNote(1,74,32)
plan=Sequence.compile(w.project,nil,MIDI,'song',format)
check(u32(plan.bytes,27)==48000,'a gate crossing the region end is clipped to the boundary')
check(w:save() and saved.composition.loopStart==2 and saved.tracks[2].output.slot==1,'save retains region, source-free notes and performer routing')
local other=make();check(other:load(saved) and other:play('song') and queued.loop,'saved song plays without installed MIDI files')
other.awaiting=false;position=0.5;other:frame(0.01)
check(other.beat==3.5,'native transport display maps region seconds back onto the song timeline')
position=5.8;check(other:startTake(1) and other:recordOn(76,1),'overdub captures the last beat of a loop region')
position=0.05;other:recordFrame()
local recorded=other:noteData(1);local last=recorded[#recorded]
check(not other.recording and last.n==76 and last.s+last.l==36,'loop wrap closes held recorded notes at the region end')
other:stop(true);playing=false;clock=200;other.rollStart=16
check(other:startTake(2),'stopped drum part can record on the full song timeline')
clock=203.2;check(other:recordOn(38,0.8),'meter-sized visual count-in opens capture')
clock=203.7;other:recordOff(38);other:endTake(false)
check(other:noteData(2)[#other:noteData(2)].s==17,'stopped recording starts at the chosen view, independent of the playback region')
local invalid=copy(saved);invalid.composition.loopStart=0;check(not format.project(invalid),'invalid loop markers rejected on load')
invalid=copy(saved);invalid.timeline.beats=1;check(not format.project(invalid),'inconsistent song length rejected')
invalid=copy(saved);invalid.timeline.tempos[1].tempo=500000;check(not format.project(invalid),'inconsistent authoritative tempo rejected')
invalid=copy(saved);invalid.midi='midi/foreign.mid';check(not format.project(invalid),'authored metadata cannot masquerade as file-backed playback')
invalid=copy(saved);invalid.tracks[1].edited=nil;check(not format.project(invalid),'authored parts cannot silently fall back to missing source data')
local tiny=make();tiny:newSong(240,1,16,2);tiny:setComposition{loop=true,loopStart=2,loopEnd=2}
tiny:addNote(1,60,1);tiny:play('song');tiny.awaiting=false;playing=true;position=0.01;tiny.snap=4
check(tiny:startTake(1) and tiny:recordOn(62,1),'coarse snap can record inside a shorter region')
tiny:recordOff(62);tiny:endTake(false);local captured=tiny:noteData(1)[2]
check(captured.s==1 and captured.s+captured.l==2,'quantization remains inside both boundaries of a one-sixteenth-note region')
local edge=make();edge:newSong(120,3,4,8);edge:setComposition{loop=true,loopStart=2,loopEnd=3};edge:addNote(1,60,12)
edge:play('song');edge.awaiting=false;position=2;edge:startTake(1);edge:recordOn(65,1)
position=0.01;edge:recordOff(65);edge:endTake(false);captured=edge:noteData(1)[2]
check(captured.s==28 and captured.s+captured.l==36,'keyup arriving before the wrap frame still closes the gate at the boundary')
edge:stop(true);edge:play('song');edge.awaiting=false;position=2;edge:startTake(1);edge:recordOn(67,1)
position=0.01;check(not edge:recordOn(69,1) and not edge.recording,'new keydown after wrap finalizes the previous pass before accepting another note')
local serverFormat=dofile('apps/openmw-server/scripts/procedural_synth_project.lua')(schema)
check(serverFormat.project(saved).tracks[1].edited.bytes==saved.tracks[1].edited.bytes,'server accepts the exact saved song representation')
local serverSequence=dofile('apps/openmw-server/scripts/procedural_synth_sequence.lua')
check(serverSequence.compile(saved,nil,MIDI,'song',serverFormat).bytes==Sequence.compile(saved,nil,MIDI,'song',format).bytes,'server/client compilation matches byte-for-byte')
local legacy=format.new();legacy.tracks[1]=format.newTrack('piano','Old',16)
check(format.project(legacy) and format.project(legacy).composition==nil,'legacy clip projects do not acquire authored-song semantics')
for n=1,12 do for _,d in ipairs{2,4,8,16} do
    check(w:newSong(120,n,d,2) and w.project.timeline.beats==n*8/d,'supported meter creates a consistent timeline')
end end
print('PASS blank song authoring, meters, region gates, recording wrap, history, persistence and legacy compatibility; '..checks..' assertions')
