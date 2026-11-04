-- Local disposable-fixture acceptance only. Never add to a public manifest.
local core=require('openmw.core');local self=require('openmw.self');local types=require('openmw.types')
local nearby=require('openmw.nearby');local I=require('openmw.interfaces');local synth=require('openmw.synth')
local vfs=require('openmw.vfs');local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local Schema=require(prefix..'shared.schema');local F=require(prefix..'shared.project')(Schema)
local MIDI=require(prefix..'client.midi');local Exercises=require(prefix..'client.band-exercises')
local Sets=require(prefix..'client.soundsets')
local phase,started,at,role,playingAt,lastChange,lastLog,index=0,nil,0,nil,nil,0,0,0
local flags={};local frames,slow,maxDt=0,0,0
local styles={'band-thrash-ref','band-industrial','captured-thrash','band-doom','band-modern','band-alternative','band-funk','band-clean','band-blues','band-acoustic'}
if synth.version>=35 then table.insert(styles,1,'band-capture-metal') end
local function log(s) print('[StudioBandAcceptanceQA] '..tostring(role)..' '..s) end
local function imported(n)
 local file=assert(vfs.open('midi/qa-acceptance-'..n..'.mid'));local bytes=file:read('*a');file:close()
 return MIDI.parse(bytes)
end
local function run(dt)
 local now=core.getRealTime();started=started or now;role=role or types.NPC.record(self).name
 assert(now-started<780,'Band acceptance timed out')
 local api=I.ProceduralSynth;if not api then return end
 synth.setMonitorVolume(0) -- Audio still renders; this keeps the desktop quiet.
 local w=api.workstation();local states=api.status()
 if role=='Synth B' and not flags.observer then
  for _,state in ipairs(states) do if state.nativePlaying and state.voices>0 and #state.channels>=8 then
   assert(state.nativeFault==0,'Late observer native fault');flags.observer=true
   if synth.version>=36 then assert(state.masterMixer and state.masterMixer.compEnabled==1,'Late listener master settings missing');log('PASS late listener receives song master settings') end
   log('PASS late listener receives sounding captured band, routes and patches')
  end end
 end
 if phase==0 then
  if now-started<5 or role=='Synth B' and not flags.observer then return end
  if not w then
   for _,o in ipairs(nearby.activators) do if o.recordId=='fetcher_procedural_synth_v1'
    and (role=='Synth A' and o.position.x<2400 or role=='Synth B' and o.position.x>2400) then o:activateBy(self);break end end
   return
  end
  local library=api.projectLibrary();if not library or not library.ready or library.pending then return end
  local p=assert(Exercises.create(role=='Synth A' and 'acoustic-band' or 'modern-band'))
  p.leaderPlays=false;p.autoBand=role=='Synth A';p.stageLayout='standing'
  if synth.version>=36 then p.masterMixer.compEnabled=1;p.masterMixer.threshold=-42;p.masterMixer.ratio=3 end
  assert(w:load(p));api.hide();phase=1;at=now
 elseif phase==1 then
  assert(now-at<60,'Initial band/sample preparation timed out')
  if not w or w.bandSetup then return end
  if not w.playing then w:play('song');return end
  local own
  for _,s in ipairs(states) do if s.editing then own=s end end
  if not own or not own.nativePlaying or own.voices==0 then return end
  if synth.version>=36 and not flags.busSetup then
   for slot,m in ipairs(w.project.band) do if m.target then
    assert(w:setBusMixer(slot,'compEnabled',1) and w:setBusMixer(slot,'threshold',-42) and w:setBusMixer(slot,'ratio',3))
   end end
   flags.busSetup=true
  end
  assert(own.nativeFault==0);playingAt=now;lastChange=now;lastLog=now;phase=2
  log('READY own band sounding; '..#w.project.tracks..' independent lanes')
 elseif phase==2 then
  frames=frames+1;maxDt=math.max(maxDt,dt);if dt>.1 then slow=slow+1 end
  assert(w and w.playing,'Playback stopped during soak')
  local playing=0;local own
  for _,s in ipairs(states) do
   assert(s.nativeFault==0,'Native timing/score fault '..s.nativeFault)
   if s.nativePlaying then playing=playing+1 end
   if s.editing then own=s end
  end
  assert(own and own.nativePlaying,'Own native transport lost')
  if synth.version>=36 and own then
   for _,output in ipairs(own.outputs or {}) do
    if (output.masterReduction or 0)>.1 and not flags.masterProcessed then flags.masterProcessed=true;log('PASS native song master reduces sounding outputs') end
    if output.slot>0 and (output.memberReduction or 0)>.1 and not flags.memberProcessed then flags.memberProcessed=true;log('PASS native member bus compresses grouped tracks') end
   end
  end
  if playing>=2 and not flags.concurrent then flags.concurrent=true;log('PASS two independent concurrent bands sounding') end
  if now-lastChange>18 then
   index=index+1;local style=styles[(index-1)%#styles+1]
   assert(w:applySoundSet(style));lastChange=now
   if synth.version>=36 then
    local track=w.project.tracks[1];local before=track and track.mixer.mid
    assert(w:setBusMixer(0,'mid',index%2==0 and -1 or 1))
    for slot,m in ipairs(w.project.band) do if m.target then assert(w:setBusMixer(slot,'bass',index%2==0 and -2 or 0)) end end
    assert(not track or track.mixer.mid==before,'Bus edit rewrote a track strip')
   end
   if role=='Synth A' and index%4==0 then
    assert(w:undo());assert(w:redo());phase=3;at=now;log('PASS genre undo/redo; explicitly resuming stopped transport')
   end
  end
  if now-lastLog>30 then
   log('SOAK seconds='..math.floor(now-playingAt)..' frames='..frames..' slow='..slow..' maxDt='..maxDt..' bands='..playing..' voices='..own.voices)
   lastLog=now
  end
  local elapsed=now-playingAt
  if phase~=2 then return end
  -- Exercise real imported dense/long scores without transferring users' files.
  local checkpoint=elapsed>300 and 3 or elapsed>180 and 2 or elapsed>75 and 1 or 0
  if checkpoint>(flags.imported or 0) and not w.bandSetup then
   w:stop(true);assert(w:import(imported(checkpoint),'midi/qa-acceptance-'..checkpoint..'.mid'))
   assert(w:applySoundSet(styles[(index-1)%#styles+1]));w:play('song');api.hide()
   flags.imported=checkpoint;phase=3;at=now
   log('PASS imported representative song '..checkpoint..' parts='..#w.project.tracks)
  elseif elapsed>420 and flags.concurrent and flags.imported==3 then
   assert(api.projectLibrary():save(true,'Private band acceptance '..role));phase=4;at=now
  end
 elseif phase==3 then
  assert(now-at<45,'Imported band restart timed out')
  if w.bandSetup then return end
  if not w.playing then w:play('song');return end
  local own;for _,s in ipairs(states) do if s.editing then own=s end end
  if own and own.nativePlaying then assert(own.nativeFault==0);phase=2;lastChange=now end
 elseif phase==4 and now-at>2 and not api.projectLibrary().pending and not w.dirty then
  flags.saved=assert(api.projectLibrary().active);assert(api.projectLibrary():open(flags.saved,false,true));phase=5;at=now
 elseif phase==5 and now-at>3 and not api.projectLibrary().pending then
  assert(w.project.tracks[1].params and F.project(w.project),'Saved bank/mixer references invalid')
  if synth.version>=36 then assert(w.project.masterMixer.compEnabled==1 and w.project.masterMixer.threshold==-42,'Master strip persistence lost') end
  assert(w:play('song'));phase=6;at=now;log('PASS server-backed project save/reload/replay')
 elseif phase==6 and now-at>8 and not w.bandSetup then
  assert(w.playing);for _,s in ipairs(states) do assert(s.nativeFault==0) end
  if synth.version>=36 then assert(flags.masterProcessed,'Master DSP was never observed');if role=='Synth A' then assert(flags.memberProcessed,'Member DSP was never observed') end end
  w:stop(true)
  if role=='Synth A' then assert(w:setupNpcBand(false,false,true));phase=7;at=now
  else api.close();phase=99;log('PASS complete: concurrent bands, dense imports, genre cycling, late listener and persistence');core.quit() end
 elseif phase==7 and now-at>3 and not w.bandSetup then
  assert(#api.performers()==0,'Owned performer cleanup left NPCs')
  api.close();phase=99;log('PASS complete: concurrent bands, dense imports, genre cycling, late listener, persistence and NPC cleanup');core.quit()
 end
end
return {engineHandlers={onFrame=function(dt)
 if phase==99 then return end
 local ok,err=pcall(run,dt);if not ok then log('FAIL '..tostring(err));phase=99;core.quit() end
end}}
