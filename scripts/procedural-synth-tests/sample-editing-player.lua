-- Disposable local-fixture acceptance; never add to a public manifest.
local core=require('openmw.core');local self=require('openmw.self');local nearby=require('openmw.nearby')
local types=require('openmw.types');local I=require('openmw.interfaces');local synth=require('openmw.synth');local vfs=require('openmw.vfs')
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local Schema=require(prefix..'shared.schema');local F=require(prefix..'shared.project')(Schema)
local Samples=require(prefix..'client.samples');local Studio=require(prefix..'client.studio')
local path='sound/fetcher/synth/sample-editing-qa.wav'
local role,started,at,phase,view=nil,nil,0,0,nil
local flags={};local reference
local function log(s) print('[StudioSampleEditQA] '..tostring(role)..' '..s) end
local function channel(state,n) for _,c in ipairs(state and state.channels or {}) do if c.id==n then return c end end end
local function run()
 local now=core.getRealTime();started=started or now;role=role or types.NPC.record(self).name
 assert(now-started<180,'Sample edit acceptance timed out');assert(synth.version>=38)
 synth.setMonitorVolume(0)
 local api=I.ProceduralSynth;if not api then return end
 local state=api.status()[1];local w=api.workstation()
 if role=='Synth B' then
  if state then
   assert(state.nativeFault==0,'Observer native transport fault')
   local c=channel(state,1);local beat=state.nativePosition%4
   if state.nativePlaying and beat>.8 and beat<2.8 and c and c.voices>0 and c.rms>.0001 then flags.audible=true end
  end
  if flags.audible and flags.loop and flags.reverse then
   phase=99;log('PASS complete: late snapshot carries loop/reverse and sustained sample audio reaches listener');core.quit()
  end
  return
 end
 if phase==0 then
  if now-started<4 then return end
  if not w then for _,o in ipairs(nearby.activators) do if o.recordId=='fetcher_procedural_synth_v1' and o.position.x<2400 then o:activateBy(self);break end end;return end
  if not api.projectLibrary().ready or api.projectLibrary().pending then return end
  local info=synth.sampleInfo(path,0,1);if info.status~='Ready' then assert(info.status=='Loading',info.error);return end
  local wave=synth.sampleWaveform(path,0,1);assert(#wave.minimum==256 and #wave.maximum==256 and wave.hash==info.hash)
  for n=1,256 do assert(wave.minimum[n]<=wave.maximum[n]) end
  reference=assert(Schema.sample{path=path,offset=0,duration=1,root=60,oneShot=false,hash=info.hash,loop={start=.04,finish=.24,crossfade=.02}})
  local browser=Samples.new(synth,vfs,self);assert(browser:loadReference(reference) and browser:audition());flags.preview=browser
  phase=1;at=now;log('PASS loader waveform, sample hash and loop preview configured')
 elseif phase==1 and now-at>1 then
  assert(flags.preview.preview.voices>0,'Loop preview ended at the short source boundary')
  assert(flags.preview:release());phase=2;at=now
 elseif phase==2 and now-at>.4 then
  assert(flags.preview.preview.voices==0,'Release did not finish preview envelope');flags.preview:stop()
  local p=F.new();p.name='Private sample editing acceptance';p.leaderPlays=false;p.autoBand=false;p.length=32
  p.composition={bars=2,numerator=4,denominator=4,loopStart=1,loopEnd=2,loop=true}
  p.timeline={beats=8,tempos={{beat=0,tempo=500000}}}
  for n=1,3 do
   local t=F.newTrack('piano','Sample '..n,32);p.tracks[n]=t
   t.notes=n==1 and {{n=60,s=0,l=28,v=.8}} or {{n=60,s=0,l=1,v=.6},{n=67,s=16,l=1,v=.6}}
   t.params=assert(Schema.state{attack=.002,sustain=1,release=.08,cutoff=18000})
   local ref={};for k,v in pairs(reference) do ref[k]=v end
   if n>1 then ref.loop=nil;ref.reverse=n==2 or nil;ref.oneShot=true end
   t.params.sample=assert(Schema.sample(ref));t.followPrograms=false
  end
  assert(w:load(p),'Private sample project validation failed');assert(w:play('song'),'Private sample score could not start');api.hide();phase=3;at=now
 elseif phase==3 and state and state.nativePlaying then
  assert(state.nativeFault==0);phase=4;at=now;log('READY own sampler sounding; independent forward, reverse and loop cursors')
 elseif phase==4 then
  assert(state and state.nativeFault==0)
  local c=channel(state,1);local position=state.nativePosition%4
  if position>1 and position<2.8 and c and c.voices>0 and c.rms>.0001 then flags.sustained=true end
  if now-at>12 and flags.sustained then
   local ref={};for k,v in pairs(reference) do ref[k]=v end;ref.reverse=true;ref.loop={start=.02,finish=.2,crossfade=.015}
   assert(w:setSample(1,ref));assert(w:undo());assert(w:redo());if not w.playing then w:play('song') end
   phase=5;at=now;log('PASS short source sustains through held score gates; live edit and Undo/Redo retain playback settings')
  end
 elseif phase==5 and now-at>6 and state and state.nativePlaying then
  assert(state.nativeFault==0);assert(api.projectLibrary():save(true,'Private sample editing acceptance'));phase=6;at=now
 elseif phase==6 and now-at>2 and not api.projectLibrary().pending and not w.dirty then
  flags.saved=assert(api.projectLibrary().active);assert(api.projectLibrary():open(flags.saved,false,true));phase=7;at=now
 elseif phase==7 and now-at>2 and not api.projectLibrary().pending then
  local ref=w.project.tracks[1].params.sample
  assert(ref.reverse and ref.loop.finish==.2 and ref.loop.crossfade==.015 and ref.hash==reference.hash)
  assert(#w.project.tracks[1].notes==1 and not w.project.tracks[3].params.sample.loop and not w.project.tracks[3].params.sample.reverse)
  assert(w:play('song'));phase=8;at=now;log('PASS server-backed save/reload retains exact edits, notes and unrelated shared-source part')
 elseif phase==8 and now-at>5 and state and state.nativePlaying then
  assert(state.nativeFault==0);w:stop(true);api.hide();w.selected=1;w.page='samples'
  view=Studio.new(w,{midiLabel=function() return 'Local sample editing check' end,hide=function() end,sound=function() end})
  phase=9;at=now;log('READY visual: Samples waveform and controls')
 elseif phase==9 then
  view:frame(now)
  local file=vfs.open('fetcher-studio81-qa-view.txt');local text=file:read('*a');file:close()
  if text:find('done',1,true) then
   view:destroy();api.close();phase=99;log('PASS complete: preview release, native sustain/reverse, shared PCM isolation, Undo/Redo, persistence and native UI');core.quit()
  end
 end
end
return {engineHandlers={onFrame=function()
 if phase==99 then return end
 local ok,err=pcall(run);if not ok then phase=99;log('FAIL '..tostring(err));core.quit() end
end},eventHandlers={Synth_Message=function(d)
 if d.op=='error' or d.op=='scoreError' then log('SERVER '..tostring(d.message)) end
 local function inspect(params)
  local ref=params and params.sample;if not ref or ref.path~=path then return end
  if ref.loop then flags.loop=true end;if ref.reverse then flags.reverse=true end
 end
 inspect(d.params);for _,c in pairs(d.channels or {}) do inspect(c.params) end
end}}
