-- Disposable local fixture only: real decoder, native transport, GUI and relay.
local core=require('openmw.core');local self=require('openmw.self');local nearby=require('openmw.nearby');local types=require('openmw.types')
local I=require('openmw.interfaces');local synth=require('openmw.synth');local vfs=require('openmw.vfs')
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local F=require(prefix..'shared.project')(require(prefix..'shared.schema'));local Samples=require(prefix..'client.samples');local Studio=require(prefix..'client.studio')
local role,started,at,phase,view,browser=nil,nil,0,0,nil,nil;local flags={}
local function log(text) print('[StudioWaveEditorQA] '..tostring(role)..' '..text) end
local function run()
 local now=core.getRealTime();started=started or now;role=role or types.NPC.record(self).name
 assert(now-started<600,'Wave editor acceptance timed out');synth.setMonitorVolume(0)
 local api=I.ProceduralSynth;if not api then return end
 local state=api.status()[1];local w=api.workstation()
 if role=='Synth B' then
  if state and state.nativePlaying then
   assert(state.nativeFault==0,'Listener native fault')
   for _,c in ipairs(state.channels) do if c.voices>0 and c.rms>.0001 then flags.audio=true end end
  end
  if flags.audio and flags.tempo and flags.slices then log('PASS complete: late listener receives manual slices and rescheduled tempo, healthy sample audio');phase=99;core.quit() end
  return
 end
 if phase==0 then
  if now-started<4 then return end
  if not w then for _,o in ipairs(nearby.activators) do if o.recordId=='fetcher_procedural_synth_v1' and o.position.x<2400 then o:activateBy(self);break end end;return end
  if not api.projectLibrary().ready or api.projectLibrary().pending then return end
  assert(synth.version>=39)
  local path='sound/fetcher/synth/sample-editing-qa.wav';local info=synth.sampleInfo(path,0,1)
  if info.status~='Ready' then assert(info.status=='Loading',info.error);return end
  local wave=synth.sampleWaveformRange(path,0,1,.05,.1,false);local rev=synth.sampleWaveformRange(path,0,1,.05,.1,true)
  assert(#wave.minimum==256 and #rev.maximum==256 and wave.hash==info.hash)
  local point=synth.sampleZeroCrossing(path,0,1,.1,false);assert(point and math.abs(point-.1)<.0051)
  browser=Samples.new(synth,vfs,self);browser:select(path);browser.duration=1
  local p=F.new();p.name='Private waveform acceptance';p.length=32;p.autoBand=false;p.leaderPlays=false
  p.composition={bars=8,numerator=4,denominator=4,loopStart=1,loopEnd=4,loop=true};p.timeline={beats=32,tempos={{beat=0,tempo=500000}}}
  p.tracks[1]=F.newTrack('piano','Original sample',32);p.tracks[1].notes={{n=60,s=0,l=32,v=.8}}
  assert(browser:setLoop(.04,.24,.02));p.tracks[1].params.sample=assert(browser:reference());assert(w:load(p))
  assert(w:addSampleSlices(assert(browser:slices{.08,.16,.23}),'Manual'))
  for n=2,5 do w.project.tracks[n].steps=('9000'):rep(8) end;w:touch();assert(F.project(w.project));phase=.5;at=now
 elseif phase==.5 then
  if not w:prepareSamples() then return end;assert(w:play('song'));api.hide();phase=1;at=now
 elseif phase==1 and state and state.nativePlaying then
  assert(state.nativeFault==0);assert(w:setTempo(140),'tempo 140 rejected');assert(w:setTempo(160),'tempo 160 rejected');phase=2;at=now
  log('READY own wave editor: native sample range, zero snap, manual slices and shared live tempo')
 elseif phase==2 and now-at>6 and not w.pendingTempo and state and state.nativePlaying then
  assert(w.project.bpm==160 and state.nativeFault==0,'tempo='..tostring(w.project.bpm)..' native fault='..tostring(state.nativeFault))
  assert(w:transportAction('pause') and w.paused);assert(w:transportAction('pause'));phase=3;at=now
 elseif phase==3 and now-at>6 and state and state.nativePlaying then
  assert(state.nativeFault==0);assert(api.projectLibrary():save(true,'Private waveform acceptance'));phase=4;at=now
 elseif phase==4 and now-at>2 and not api.projectLibrary().pending and not w.dirty then
  assert(api.projectLibrary():open(assert(api.projectLibrary().active),false,true));phase=5;at=now
 elseif phase==5 and now-at>2 and not api.projectLibrary().pending then
  assert(w.project.bpm==160 and #w.project.tracks==5 and w.project.tracks[3].params.sample.offset==.08)
  assert(w:play('song'));w.selected=1;w.page='samples';api.hide()
  view=Studio.new(w,{midiLabel=function() return 'Private waveform check' end,hide=function() end,
   playSong=function() w:play('song') end,playClips=function() w:play('clips') end,stop=function() w:stop(true) end,
   transport=function(action)
    local before=w.playing;local result=w:transportAction(action)
    log('TRANSPORT action='..action..' before='..tostring(before)..' result='..tostring(result)..' after='..tostring(w.playing)..' paused='..tostring(w.paused~=nil))
    return result
   end})
  view.samples=browser;view:setSampleFocus(true);view:render();phase=6;at=now
  log('READY visual: expanded sample editor; zoom, drag trim, add/move slice markers; then Escape to Arrange')
 elseif phase==6 then
  view:frame(now)
  local file=vfs.open('fetcher-studio83-qa-view.txt');local command=file:read('*a');file:close()
  local automatic=command:find('automatic',1,true)
  if (command:find('arrange',1,true) or automatic) and not flags.arrange then
   flags.arrange=true;view:setSampleFocus(false);w.page='arrange';w.arrangeBars=64;view:render()
   log('READY ruler visual: 64 bars, one row; change BPM during playback, then test Space pause/resume')
  end
  if automatic then
   local key=require('openmw.input').KEY
   local function spacePair() view:key(key.Space,true);view:key(key.Enter,true);view:key(key.Enter,false);view:key(key.Space,false) end
   if not flags.keyStage then assert(w:setTempo(170));flags.keyStage=1;flags.keyAt=now
   elseif flags.keyStage==1 and now-flags.keyAt>4 and state and state.nativePlaying and not w.pendingTempo then
    spacePair();assert(w.paused and not w.playing,'Space+native Activate Enter did not pause once');flags.keyStage=2;flags.keyAt=now
   elseif flags.keyStage==2 and now-flags.keyAt>2 then
    assert(w.paused and not w.playing and not state.nativePlaying,'Paused shared native transport resumed by itself')
    flags.paused=true;spacePair();assert(w.playing and not w.paused);flags.keyStage=3;flags.keyAt=now
   elseif flags.keyStage==3 and now-flags.keyAt>4 and state and state.nativePlaying then command='done' end
  end
  if w.paused then flags.paused=true end
  if flags.paused and w.playing then flags.resumed=true end
  if command:find('done',1,true) then
   assert(flags.resumed,'Native keyboard pause/resume not observed');assert(w.project.bpm~=160,'Native BPM control was not changed')
   view:destroy();api.close();phase=99;log('PASS complete: waveform API, native BPM, Activate-to-Enter pause/resume regression, manual slicing, shared tempo and private persistence');core.quit()
  end
 end
end
return {engineHandlers={onKeyPress=function(e) if view then view:key(e.code,true) end end,onKeyRelease=function(e) if view then view:key(e.code,false) end end,
onFrame=function() if phase==99 then return end;local ok,err=pcall(run);if not ok then phase=99;log('FAIL '..tostring(err));core.quit() end end},eventHandlers={Synth_Message=function(d)
 if d.op=='scoreBegin' and d.length==288000 then flags.tempo=true end
 for _,c in pairs(d.channels or {}) do if c.params and c.params.sample and c.params.sample.offset==.08 then flags.slices=true end end
end}}
