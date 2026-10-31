local root='files/server/server-lua-packages/fetcher.procedural-synth/'
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=dofile(root..'shared/schema.lua');local factory=dofile(root..'shared/project.lua');local f=factory(schema)
local MIDI=dofile(root..'client/midi.lua');local S=dofile(root..'shared/sequence.lua')
package.loaded[prefix..'shared.schema']=schema;package.loaded[prefix..'shared.project']=factory
package.loaded[prefix..'shared.sequence']=S;package.loaded[prefix..'client.midi']=MIDI
package.loaded[prefix..'client.soundsets']=dofile(root..'client/soundsets.lua')
package.loaded[prefix..'client.arrangement']=dofile(root..'client/arrangement.lua')
local W=dofile(root..'client/workstation.lua');local checks=0
local function check(b,m) checks=checks+1;assert(b,m) end
local function copy(t) if type(t)~='table' then return t end;local o={};for k,v in pairs(t) do o[k]=copy(v) end;return o end
local messages,last={},nil
local w=W.new{configure=function() end,panic=function() end,stopSequence=function() end,
    message=function(m) messages[#messages+1]=m end,schedule=function(p) last=p;return true end}
assert(w:newSong(120,4,4,8));w.project.autoBand=false
local recorded={{n=60,s=0,l=64,v=.8},{n=64,s=20,l=2,v=.8},{n=67,s=36,l=2,v=.8},{n=72,s=52,l=2,v=.8}}
assert(w:storeNotes(1,recorded));w:selectAllNotes(1);assert(w:captureClip(1))
local function notes(plan) local count=0;for at=1,#plan.bytes,11 do if plan.bytes:byte(at+9)<128 then count=count+1 end end;return count end
local before=copy(w.project);local clip=before.tracks[1].arrangement.clips[1]
check(w.project.length==16 and clip.length==64 and #clip.notes.bytes/10==4,'reproduce: captured four-bar recording retains a one-bar shared loop')
assert(w:play('clips'));check(notes(last)==1 and last.length==96000,'old short playback range includes only the first held note')
check(messages[#messages]:find('shared 1%-bar loop') and messages[#messages]:find('Fit clips'),'short range emits an actionable explanation instead of silently cropping')
local history=#w.undoStack;assert(w:fitClipLoop())
check(w.project.length==64 and #w.undoStack==history+1 and not w.playing,'Fit expands the shared loop in one stopped Undo action')
check(w.project.tracks[1].arrangement.clips[1].notes.bytes==clip.notes.bytes and w.project.tracks[1].arrangement.regions[1].l==64,'fitting never edits recording notes, gates or placements')
assert(w:play('clips'));check(notes(last)==4 and last.length==384000,'fitted playback contains every recorded note and the complete four-bar loop')
check(w:undo() and w.project.length==16 and w:redo() and w.project.length==64,'range-only Undo/Redo restores both lengths')
history=#w.undoStack;local generation=w.generation;assert(w:fitClipLoop())
check(#w.undoStack==history and w.generation==generation,'already fitting does not stop or create fake history')
assert(w:editClip(1,1));w:length(16);assert(w:playClip())
check(notes(last)==4 and last.length==384000,'source audition stays independent of the shared loop length')
assert(w:leaveClip());assert(w:add('drum'));local drum=w.project.tracks[2]
drum.steps=drum.steps:sub(1,79)..'9'..drum.steps:sub(81);w:touch();assert(w:fitClipLoop())
check(w.project.length==80 and w:clipLoopExtent()==80,'Fit also includes later saved drum steps, without using one-shot tail length')
w.project.tracks[1].arrangement.regions[1].s=256;w:touch();generation=w.generation;local length=w.project.length
check(not w:fitClipLoop() and w.project.length==length and w.generation==generation,'over-16-bar arrangements reject rather than silently clamp or mutate')
check(messages[#messages]:find('Play song'),'over-limit message points to full arrangement playback')
w.recording={};check(not w:fitClipLoop(),'Fit protects active takes');w.recording=nil
w.failedTake={};check(not w:fitClipLoop(),'Fit protects uncommitted failed takes');w.failedTake=nil
local empty=W.new{message=function(m) messages[#messages+1]=m end}
check(not empty:fitClipLoop() and empty.project.length==16,'empty material does not change the loop')
print(string.format('PASS recorded-clip short-loop reproduction, shared-range warning, fit/history, source audition, later drum data and bounds; %d assertions',checks))
