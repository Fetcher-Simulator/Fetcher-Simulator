-- Disposable local-fixture acceptance. Never put this in a public manifest.
local core=require('openmw.core');local self=require('openmw.self');local types=require('openmw.types')
local nearby=require('openmw.nearby');local I=require('openmw.interfaces');local synth=require('openmw.synth')
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local Schema=require(prefix..'shared.schema');local F=require(prefix..'shared.project')(Schema)
local Exercises=require(prefix..'client.band-exercises')
local phase,started,at,role,playingAt=0,nil,0,nil,nil;local flags={};local natural,pinch
local function log(s) print('[StudioHarmonicsQA] '..tostring(role)..' '..s) end
local function run()
 local now=core.getRealTime();started=started or now;role=role or types.NPC.record(self).name
 assert(now-started<200,'Harmonics acceptance timed out')
 local api=I.ProceduralSynth;if not api then return end;synth.setMonitorVolume(0)
 local w=api.workstation();local states=api.status();local own;local playing=0
 for _,s in ipairs(states) do
  assert(s.nativeFault==0,'Native sample/score fault')
  if s.editing then own=s end
  if s.nativePlaying and s.voices>0 then
   playing=playing+1
   if role=='Synth B' and not flags.observer and #s.channels>=9 and not s.editing then
    flags.observer=true;log('PASS late listener receives sounding harmonic project')
   end
  end
 end
 if playing>=2 and not flags.concurrent then flags.concurrent=true;log('PASS two independent concurrent harmonic projects') end
 if phase==0 then
  if now-started<5 or role=='Synth B' and not flags.observer then return end
  if not w then
   for _,o in ipairs(nearby.activators) do if o.recordId=='fetcher_procedural_synth_v1'
    and (role=='Synth A' and o.position.x<2400 or role=='Synth B' and o.position.x>2400) then o:activateBy(self);break end end
   return
  end
  local library=api.projectLibrary();if not library or not library.ready or library.pending then return end
  local p=assert(Exercises.create('harmonic-accents'));p.leaderPlays=false;p.autoBand=role=='Synth A';p.stageLayout='standing'
  for n,t in ipairs(p.tracks) do
   if t.presetId=='b:guitar-natural-harmonics' then natural=n elseif t.presetId=='b:guitar-pinch-harmonics' then pinch=n end
  end
  assert(natural and pinch);assert(w:load(p));api.hide();phase=1;at=now
 elseif phase==1 or phase==3 then
  assert(now-at<45,'Harmonic bank preparation timed out')
  if not w or w.bandSetup then return end
  if not w.playing then w:play('song');return end
  if not own or not own.nativePlaying or own.voices==0 then return end
  for _,n in ipairs{natural,pinch} do assert(w.cb.sampleStatus(w.project.tracks[n].params.sample).status=='Ready','Recorded bank failed readiness') end
  if phase==1 then playingAt=now;phase=2;log('READY own band sounding with both recorded harmonic banks')
  else phase=4;at=now;log('PASS live harmonic switch, Undo/Redo and native resume') end
 elseif phase==2 then
  assert(w.playing and own and own.nativePlaying,'Harmonic playback stopped')
  for _,c in ipairs(own.channels) do
   if c.rms>1e-6 and c.id==natural then flags.natural=true end
   if c.rms>1e-6 and c.id==pinch then flags.pinch=true end
  end
  if now-playingAt>25 and flags.concurrent then
   assert(flags.natural and flags.pinch,'One harmonic lane never produced native audio')
   log('PASS both harmonic lanes produce native RMS during their authored bars')
   assert(w:preset(natural,'b:guitar-pinch-harmonics'))
   assert(w.project.tracks[natural].params.sample.path:find('pinch',1,true))
   assert(w:undo());assert(w.project.tracks[natural].params.sample.path:find('natural',1,true))
   assert(w:redo());assert(w.project.tracks[natural].params.sample.path:find('pinch',1,true))
   assert(w:undo());phase=3;at=now
  end
 elseif phase==4 and now-at>12 and flags.concurrent then
  assert(api.projectLibrary():save(true,'Private harmonic acceptance '..role));phase=5;at=now
 elseif phase==5 and now-at>2 and not api.projectLibrary().pending and not w.dirty then
  flags.saved=assert(api.projectLibrary().active);assert(api.projectLibrary():open(flags.saved,false,true));phase=6;at=now
 elseif phase==6 and now-at>3 and not api.projectLibrary().pending then
  assert(F.project(w.project));assert(w.project.tracks[natural].params.sample.path:find('natural',1,true))
  assert(w.project.tracks[pinch].params.sample.path:find('pinch',1,true))
  assert(w:play('song'));phase=7;at=now;log('PASS server-backed harmonic project save/reload/replay')
 elseif phase==7 and now-at>12 and not w.bandSetup then
  assert(w.playing and own and own.nativePlaying);w:stop(true)
  if role=='Synth A' then assert(w:setupNpcBand(false,false,true));phase=8;at=now
  else api.close();phase=99;log('PASS complete: harmonic banks, concurrent playback, live edits and persistence');core.quit() end
 elseif phase==8 and now-at>3 and not w.bandSetup then
  assert(#api.performers()==0,'Owned harmonic band cleanup left NPCs')
  api.close();phase=99;log('PASS complete: harmonic banks, concurrent playback, live edits, persistence and NPC cleanup');core.quit()
 end
end
return {engineHandlers={onFrame=function()
 if phase==99 then return end
 local ok,err=pcall(run);if not ok then log('FAIL '..tostring(err));phase=99;core.quit() end
end}}
