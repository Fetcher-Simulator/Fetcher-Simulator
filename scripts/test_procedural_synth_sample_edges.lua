local root='files/server/server-lua-packages/fetcher.procedural-synth/'
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=dofile(root..'shared/schema.lua');package.loaded[prefix..'shared.schema']=schema
for _,name in ipairs{'speech-catalog','voice-text'} do package.loaded[prefix..'client.'..name]=dofile(root..'client/'..name..'.lua') end
package.loaded[prefix..'shared.voice-transcripts']=dofile(root..'shared/voice-transcripts.lua')
package.loaded[prefix..'shared.sample-library']=dofile(root..'shared/sample-library.lua')
local Samples=dofile(root..'client/samples.lua');local format=package.loaded[prefix..'shared.sample-library'](schema)
local n=0;local function check(b,m) n=n+1;assert(b,m) end
local hash=string.rep('a',32);local ref={path='sound/edge.wav',offset=.2,duration=2,root=60,oneShot=true,hash=hash,fadeIn=.2,fadeOut=.3}
local valid=assert(schema.sample(ref));check(valid.fadeIn==.2 and valid.fadeOut==.3,'fades accepted as source-reference fields')
for _,changes in ipairs{{fadeIn=-1},{fadeOut=0/0},{fadeIn=2,fadeOut=.1},{fadeIn=math.huge},{fadeIn='1'}} do
 local bad={};for k,v in pairs(ref) do bad[k]=v end;for k,v in pairs(changes) do bad[k]=v end;check(not schema.sample(bad),'malformed fades rejected')
end
local unfaded={};for k,v in pairs(ref) do unfaded[k]=v end;unfaded.fadeIn=nil;unfaded.fadeOut=nil
check(schema.sampleKey(ref)~=schema.sampleKey(unfaded),'fade edits invalidate native channel identity')
for _,preset in ipairs(schema.builtins) do if schema.isBank(preset.state.sample) then local bad={};for k,v in pairs(preset.state.sample) do bad[k]=v end;bad.fadeIn=.1;check(not schema.sample(bad),'banks retain original zone envelopes');break end end
local status,peak,duration='Ready',.2,2
local synth={version=41,sampleInfo=function() return {status=status,peak=peak,duration=duration,hash=hash} end}
local b=Samples.new(synth,{},{},{});assert(b:loadReference(ref))
check(Samples.status({version=40},ref).status=='Unavailable','old clients cannot silently ignore fades')
check(b:setEdits(2,.15,.25,'drag') and b:setEdits(3,.1,.2,'drag') and #b.editUndo==1,'many smooth drag updates are one local Undo')
check(b:undoEdit() and b.gain==nil and b.fadeIn==.2 and b:undoEdit(true) and b.gain==3 and b.fadeOut==.2,'source edit Undo/Redo restores previous settings')
local before=#b.editUndo;check(not b:setEdits(9,0,0) and not b:setEdits(1,1.5,1) and #b.editUndo==before,'invalid edits leave source and history intact')
b:finishEdit();check(b:normalize() and (b.gain or 1)*peak<=10^(-1/20) and (b.gain or 1)*peak>.89,'normalization floors gain to retain at least 1 dB source headroom')
check(b:undoEdit() and b.gain==3 and b:undoEdit(true),'normalization is reversible')
peak=0;before=#b.editUndo;check(not b:normalize() and #b.editUndo==before,'silent source does not create gain or history noise')
peak=.001;local ok,message=b:normalize();check(ok and b.gain==8 and message:find('capped',1,true),'extremely quiet source reports bounded boost')
peak=4;check(not b:normalize(),'out-of-range floating-point source cannot normalize to a clipping gain')
peak=.2;status='Loading';check(not b:normalize() and not b:setEdits(1,0,0),'loading sample cannot be edited or normalized');status='Ready'
b:select(ref.path);assert(b:loadReference(ref));local excerpt=assert(b:excerpt(.5,.7));check(excerpt.offset==.7 and math.abs((excerpt.fadeIn or 0)+(excerpt.fadeOut or 0)-excerpt.duration)<1e-7,'extracted interval retains fitted non-overlapping fades')
b.reverse=true;excerpt=assert(b:excerpt(.5,.7));check(excerpt.offset==1.5 and excerpt.reverse,'reverse slice starts at correct physical offset')
b.editView={key=b:viewKey(),first=0,span=2,caret=.5,trimFirst=0,trimLast=2,points={.5,1},snap=false,sensitivity=.6,spacing=.1}
local kit=assert(b:recipe('kit'));check(kit.source.fadeIn==.2 and kit.source.fadeOut==.3,'saved kit retains source fades')
local slices=assert(b:slices(b.editView.points));check(#slices==3 and slices[1].fadeIn==.2 and slices[1].fadeOut==.3,'kit lanes retain fade settings where they fit')
local restored=Samples.new(synth,{},{},{});assert(restored:loadRecipe(kit));check(restored.fadeIn==.2 and restored.fadeOut==.3 and restored.editView.points[2]==1,'library reopening restores fades and kit cuts')
local bad=assert(format.recipe(kit));bad.span=.4;bad.markers={.1};check(not format.recipe(bad),'stored fade length must also fit the real decoded recipe span')
local readySample=assert(b:reference());duration=.2;check(Samples.status(synth,readySample).status=='Unavailable','late short decode cannot play out-of-bounds fades');duration=2
assert(b:trim(.2,.4));check((b.fadeIn or 0)+(b.fadeOut or 0)<=.2+1e-7 and #b.editUndo==0,'new trim fits fades and clears obsolete local history')
package.loaded[prefix..'shared.project']=dofile(root..'shared/project.lua');package.loaded[prefix..'client.midi']=dofile(root..'client/midi.lua');package.loaded[prefix..'client.soundsets']=dofile(root..'client/soundsets.lua')
local W=dofile(root..'client/workstation.lua');local w=W.new{configure=function() end,panic=function() end,message=function() end,sampleStatus=function(s) return Samples.status(synth,s) end}
w:add('piano');check(w:setSample(1,ref),'apply fade reference to existing part')
local F=package.loaded[prefix..'shared.project'](schema);check(F.project(w.project).tracks[1].params.sample.fadeOut==.3,'project save validation retains fades')
check(w:undo() and not w.project.tracks[1].params.sample and w:redo() and w.project.tracks[1].params.sample.fadeIn==.2,'project Undo/Redo retains complete source edits')
print('PASS sample edges, peak normalization/headroom/cap/silence, local grouped history, slice/reverse fitting, library/project persistence and old-client/late-decode bounds; '..n..' assertions')
