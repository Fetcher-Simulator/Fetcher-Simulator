-- Disposable copied-fixture acceptance; no original profiles/database are used.
local core=require('openmw.core');local self=require('openmw.self');local nearby=require('openmw.nearby');local types=require('openmw.types')
local I=require('openmw.interfaces');local synth=require('openmw.synth');local vfs=require('openmw.vfs')
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local F=require(prefix..'shared.project')(require(prefix..'shared.schema'));local Samples=require(prefix..'client.samples');local Studio=require(prefix..'client.studio')
local role,started,at,phase,view,browser=nil,nil,0,0,nil,nil;local flags={};local path='sound/fetcher/synth/sample-slicing-qa.wav'
local function log(text) print('[StudioAutoSliceQA] '..tostring(role)..' '..text) end
local function run()
 local now=core.getRealTime();started=started or now;role=role or types.NPC.record(self).name
 assert(now-started<360,'Automatic slicing acceptance timed out');synth.setMonitorVolume(0)
 local api=I.ProceduralSynth;if not api then return end
 local state=api.status()[1];local w=api.workstation()
 if role=='Synth B' then
  if state and state.nativePlaying then
   assert(state.nativeFault==0,'Listener native fault')
   for _,c in ipairs(state.channels) do if c.voices>0 and c.rms>.0001 then flags.audio=true end end
  end
  if flags.audio and flags.slices then phase=99;log('PASS complete: late listener receives detected slice references and original-timing native phrase audio');core.quit() end
  return
 end
 if phase==0 then
  if now-started<4 then return end
  if not w then for _,o in ipairs(nearby.activators) do if o.recordId=='fetcher_procedural_synth_v1' and o.position.x<2400 then o:activateBy(self);break end end;return end
  if not api.projectLibrary().ready or api.projectLibrary().pending then return end
  assert(synth.version>=40)
  local info=synth.sampleInfo(path,0,2);if info.status~='Ready' then assert(info.status=='Loading',info.error);return end
  browser=Samples.new(synth,vfs,self);assert(browser:loadReference{path=path,offset=0,duration=2,root=60,oneShot=true,hash=info.hash})
  local result=assert(browser:detect(.5,.12,true));assert(#result.points==3 and result.found==3)
  for n,point in ipairs(result.points) do assert(math.abs(point-({.4,.9,1.45})[n])<.013) end
  flags.refs=assert(browser:slices(result.points));assert(#flags.refs==4)
  local p=F.new();p.name='Private automatic slice acceptance';p.autoBand=false;p.leaderPlays=false
  p.composition={bars=4,numerator=4,denominator=4,loopStart=1,loopEnd=4,loop=true};p.timeline={beats=16,tempos={{beat=0,tempo=500000}}}
  p.tracks[1]=F.newTrack('piano','Original sample',16);p.tracks[1].params.sample=assert(browser:reference());p.tracks[1].mute=true
  p.tracks[1].notes={{n=60,s=0,l=16,v=.8}};assert(w:load(p));w.cursorBeat=4;api.hide();w.page='samples';w.selected=1
  view=Studio.new(w,{midiLabel=function() return 'Private automatic slicing' end,hide=function() end,
   playSong=function() w:play('song') end,playClips=function() w:play('clips') end,stop=function() w:stop(true) end,transport=function(action) w:transportAction(action) end})
  view.samples=browser;view:setSampleFocus(true);view:render();assert(view.waveEditor:detect());assert(#view.waveEditor.s.points==3)
  phase=1;at=now;log('READY visual: automatic markers, sensitivity/gap and Create phrase button')
 elseif phase==1 then
  view:frame(now)
  if w.page=='arrange' then
   assert(#w.project.tracks==5 and w.project.tracks[1].mute and w.project.tracks[1].kind=='piano')
   for n=2,5 do local t=w.project.tracks[n];assert(t.arrangement and t.params.sample.offset==flags.refs[n-1].offset) end
   assert(math.abs(w.project.tracks[2].arrangement.regions[1].s-16)<1e-8)
   view:destroy();view=nil;phase=2;at=now
  end
 elseif phase==2 then
  if not w:prepareSamples() then return end;assert(w:play('song'));phase=3;at=now;log('READY own slices: Create phrase retained original track and placed four linked clips at the edit playhead')
 elseif phase==3 then
  assert(not state or state.nativeFault==0)
  if now-at>25 and state and state.nativePlaying and not api.projectLibrary().pending then
   assert(api.projectLibrary():save(true,'Private automatic slice acceptance'));phase=4;at=now
  end
 elseif phase==4 and now-at>2 and not api.projectLibrary().pending and not w.dirty then
  assert(api.projectLibrary():open(assert(api.projectLibrary().active),false,true));phase=5;at=now
 elseif phase==5 and now-at>2 and not api.projectLibrary().pending then
  assert(#w.project.tracks==5 and w.project.tracks[1].mute)
  for n=2,5 do local t=w.project.tracks[n];assert(t.params.sample.hash==flags.refs[n-1].hash and #t.arrangement.regions==1) end
  assert(w:undo()==false,'Reloaded project should have an empty edit history')
  assert(w:play('song'));phase=6;at=now
 elseif phase==6 and now-at>5 and state and state.nativePlaying then
  assert(state.nativeFault==0);api.close();phase=99;log('PASS complete: decoded transient suggestions, native UI phrase creation, exact source offsets, relay, sounding score and private save/reload');core.quit()
 end
end
return {engineHandlers={onFrame=function() if phase==99 then return end;local ok,err=pcall(run);if not ok then phase=99;log('FAIL '..tostring(err));core.quit() end end},eventHandlers={Synth_Message=function(d)
 local count=0;for _,c in pairs(d.channels or {}) do if c.params and c.params.sample and c.params.sample.path==path and c.params.sample.offset>0 then count=count+1 end end
 if count>=3 then flags.slices=true end
end}}
