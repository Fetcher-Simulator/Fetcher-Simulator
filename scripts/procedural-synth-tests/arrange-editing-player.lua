-- Local copied-fixture navigation/ruler/chopping acceptance only.
local core=require('openmw.core');local self=require('openmw.self');local nearby=require('openmw.nearby')
local types=require('openmw.types');local I=require('openmw.interfaces');local synth=require('openmw.synth');local vfs=require('openmw.vfs')
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local Schema=require(prefix..'shared.schema');local F=require(prefix..'shared.project')(Schema);local Samples=require(prefix..'client.samples')
local role,started,at,phase=nil,nil,0,0;local flags={}
local function log(s) print('[StudioArrangeEditQA] '..tostring(role)..' '..s) end
local function run()
 local now=core.getRealTime();started=started or now;role=role or types.NPC.record(self).name
 assert(now-started<240,'Arrange acceptance timed out');synth.setMonitorVolume(0)
 local api=I.ProceduralSynth;if not api then return end
 local state=api.status()[1];local w=api.workstation()
 if role=='Synth B' then
  if state and state.nativePlaying then
   assert(state.nativeFault==0,'Listener transport fault')
   for _,c in ipairs(state.channels) do if c.voices>0 and c.rms>.0001 then flags.audio=true end end
  end
  if flags.audio and flags.seek and flags.slices then phase=99;log('PASS complete: late listener receives sample slices and shared seek, sounding native score remains healthy');core.quit() end
  return
 end
 if phase==0 then
  if now-started<4 then return end
  if not w then for _,o in ipairs(nearby.activators) do if o.recordId=='fetcher_procedural_synth_v1' and o.position.x<2400 then o:activateBy(self);break end end;return end
  if not api.projectLibrary().ready or api.projectLibrary().pending then return end
  local path='sound/fetcher/synth/sample-editing-qa.wav';local info=synth.sampleInfo(path,0,1)
  if info.status~='Ready' then assert(info.status=='Loading',info.error);return end
  local p=F.new();p.name='Private Arrange acceptance';p.length=32;p.autoBand=false;p.leaderPlays=false
  p.composition={bars=8,numerator=4,denominator=4,loopStart=1,loopEnd=8,loop=false}
  p.timeline={beats=32,tempos={{beat=0,tempo=500000}}};p.tracks[1]=F.newTrack('piano','Original piano',32)
  for n=0,7 do p.tracks[1].notes[#p.tracks[1].notes+1]={n=60+n%3*4,s=n*4,l=3,v=.7} end
  assert(w:load(p));local b=Samples.new(synth,vfs,self);b.path=path;b.duration=1
  assert(w:addSampleSlices(assert(b:chops(4)),'QA cut'))
  for n=2,5 do assert(w:toggleStep(n,(n-2)*4)) end
  assert(w:capturePattern());for n,t in ipairs(w.project.tracks) do for _,step in ipairs{32,64,96} do assert(w:placeClip(n,1,step)) end end
  phase=1;at=now;log('PASS four non-destructive cuts form independent sequencer lanes and arrangement sources')
 elseif phase==1 and now-at>2 then
  if not w.playing then w:play('song');return end
  if state and state.nativePlaying then api.sound(1);phase=2;at=now;log('READY own arranger: Synth page open; use Arrange tab, drag both loop handles and seek on ruler') end
 elseif phase==2 then
  assert(state and state.nativeFault==0)
  if not flags.auto then
   local file=vfs.open('fetcher-studio82-qa-view.txt');local text=file:read('*a');file:close()
   if text:find('auto',1,true) then
    flags.auto=true;assert(w:setSongLoop(2,6,true));assert(w:seek(12.25));if not w.playing then assert(w:play('song')) end
    w.page='arrange';api.studio()
   end
  end
  local s=w.project.composition
  if w.page=='arrange' and w.cursorBeat and w.cursorBeat>0 and s.loop and s.loopStart>1 and s.loopEnd<8 then
   phase=3;at=now;log('PASS native top tabs, loop-start/end drags and playhead seek observed; cursor='..w.cursorBeat..' loop='..s.loopStart..'-'..s.loopEnd)
  end
 elseif phase==3 and now-at>6 and state and state.nativePlaying then
  assert(state.nativeFault==0);local ending=w.project.composition.loopEnd
  assert(w:undo() and w:redo() and w.project.composition.loopEnd==ending);assert(w:play('song'))
  assert(api.projectLibrary():save(true,'Private Arrange acceptance'));phase=4;at=now
 elseif phase==4 and now-at>2 and not api.projectLibrary().pending and not w.dirty then
  flags.saved=assert(api.projectLibrary().active);assert(api.projectLibrary():open(flags.saved,false,true));phase=5;at=now
 elseif phase==5 and now-at>2 and not api.projectLibrary().pending then
  assert(#w.project.tracks==5 and w.project.composition.loop and w.project.composition.loopStart>1 and w.project.composition.loopEnd<8)
  for n=2,5 do assert(w.project.tracks[n].params.sample and w.project.tracks[n].params.sample.duration==.075) end
  assert(w:seek(12.25));w:stop(true);w.page='arrange';api.studio();phase=6;at=now
  log('PASS loop markers and sample cuts survive server save/reload and Undo/Redo')
  log('READY final ruler visual: drag the stopped teal playhead handle, then Play song')
 elseif phase==6 and w.playing and w.cursorBeat and w.cursorBeat~=12.25 then phase=7;at=now
 elseif phase==7 and now-at>8 and state and state.nativePlaying then
  assert(state.nativeFault==0);api.close();phase=99;log('PASS complete: navigation, ruler, shared seeking, loop history, sample cuts and private persistence');core.quit()
 end
end
return {engineHandlers={onFrame=function()
 if phase==99 then return end
 local ok,err=pcall(run);if not ok then phase=99;log('FAIL '..tostring(err));core.quit() end
end},eventHandlers={Synth_Message=function(d)
 if d.op=='error' or d.op=='scoreError' then log('SERVER '..tostring(d.message)) end
 if d.op=='scoreBegin' and d.activate and d.started and d.activate-d.started>.5 then flags.seek=true end
 local count=0;for _,c in pairs(d.channels or {}) do if c.params and c.params.sample and c.params.sample.path=='sound/fetcher/synth/sample-editing-qa.wav' then count=count+1 end end
 if count==4 then flags.slices=true end
end}}
