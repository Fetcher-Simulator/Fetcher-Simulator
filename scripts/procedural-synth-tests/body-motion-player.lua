-- Local copied-fixture acceptance only. Not listed in the production manifest.
local core=require('openmw.core');local self=require('openmw.self');local nearby=require('openmw.nearby')
local types=require('openmw.types');local I=require('openmw.interfaces');local synth=require('openmw.synth')
local camera=require('openmw.camera');local util=require('openmw.util');local vfs=require('openmw.vfs');local mp=require('openmw.mp')
local anim=require('openmw.animation');local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local F=require(prefix..'shared.project')(require(prefix..'shared.schema'));local v=util.vector3
local phase,started,at,role,origin,previous=nil,nil,0,nil,nil,nil
local flags={};local extremes={};local lastView;local previousMotion
local function log(s) print('[StudioBodyMotionQA] '..tostring(role)..' '..s) end
local function subject(index)
 if index==4 then return role=='Synth A' and self or mp.getPlayerObject and previousMotion end
 local name=({'drumkit','guitar','flute'})[index]
 for _,p in ipairs(I.ProceduralSynth.performers()) do if p.instrument==name then
  for _,actor in ipairs(nearby.actors) do if mp.getObjectMpNum(actor)==tonumber(p.key:sub(3)) then return actor end end
 end end
end
local function view(index,angle)
 local actor=subject(index);if not actor then return end
 local center=actor.position+v(0,0,index==1 and 75 or 110)
 local offsets={v(0,150,10),v(-140,30,10),v(140,30,15),v(0,80,100),v(65,90,-65)}
 local _,yaw=actor.rotation:getAnglesXZ()
 local point=center+util.transform.rotateZ(yaw)*(offsets[angle] or offsets[1]);local delta=center-point
 camera.setMode(camera.MODE.Static);camera.setStaticPosition(point)
 camera.setYaw(math.atan2(delta.x,delta.y));camera.setPitch(-math.atan2(delta.z,math.sqrt(delta.x^2+delta.y^2)))
end
local function run(dt)
 local now=core.getRealTime();started=started or now;phase=phase or 0;role=role or types.NPC.record(self).name
 assert(now-started<540,'Body-motion acceptance timed out');synth.setMonitorVolume(0)
 local api=I.ProceduralSynth;if not api then return end;local w=api.workstation()
 if role=='Synth B' then
  local conducting=false
  for _,p in ipairs(api.performers()) do
   if p.instrument=='conductor' then conducting=true;flags.conductor=true end
   if p.instrument=='drumkit' and p.body and math.abs(p.body.yaw)>.015 then flags.drummer=true end
   if p.instrument=='guitar' and p.body and math.abs(p.body.roll)>.005 then flags.sway=true end
  end
  for _,s in ipairs(api.status()) do if s.nativePlaying and s.voices>0 then flags.audio=true;assert(s.nativeFault==0,'Listener native audio fault') end end
  if flags.audio and flags.drummer and flags.sway and flags.conductor and not flags.ready then flags.ready=true;log('PASS late listener receives conductor, torso reach, sway and sounding band') end
  if flags.ready and not conducting then flags.stopped=flags.stopped or now
   if now-flags.stopped>1 then log('PASS complete: late observer animation, audio and stop cleanup');core.quit();phase=99 end
  else flags.stopped=nil end
  return
 end
 if phase==0 then
  if now-started<4 then return end
  if not w then for _,o in ipairs(nearby.activators) do if o.recordId=='fetcher_procedural_synth_v1' and o.position.x<2400 then o:activateBy(self);return end end;return end
  if not api.projectLibrary().ready or api.projectLibrary().pending then return end
  local calibration=api.poseCalibration();for _,name in ipairs(calibration.names) do calibration:reset(name) end;calibration.enabled=true
  assert(w:newSong(120,4,4,4));assert(w:addPart('keys'));assert(w:addPart('keys'))
  w.project.name='Private performer motion acceptance';w.project.leaderPlays=false;w.project.autoBand=false;w.project.conductor=false;w.project.composition.loop=true
  w.project.timeline.tempos={{beat=0,tempo=500000},{beat=8,tempo=750000}}
  for n,instrument in ipairs{'guitar','flute','drumkit'} do
   local t=w.project.tracks[n];t.instrument=instrument;t.followPrograms=false
   assert(w:preset(n,n==1 and 'b:captured-guitar-thrash' or n==3 and 'b:sampled-drum-kit' or 'b:flute'))
   local notes={};local function note(pitch,step,length) notes[#notes+1]={n=pitch,s=step,l=length,v=.8} end
   if n==3 then for step=0,62,2 do note(step%16<8 and 42 or 51,step,.3)
    if step%8==4 then note(38,step,.3) end;if step%8==0 then note(36,step,.3) end
   end else for step=0,60,4 do note(n==1 and 45 or 72,step,3) end end
   t.edited=assert(F.encodeNotes(notes,960))
  end
  w:touch();w:configureAll();assert(w:setupNpcBand(false));w.page='band';api.studio();phase=1
  log('READY UI: Leader plays Off; click Conduct Off to enable gestures')
 elseif phase==1 then
  if w.bandSetup or not w.project.conductor then return end
  if #api.performers()<3 then return end
  assert(w:play('song'));api.hide();camera.setMode(camera.MODE.ThirdPerson,true);origin=self.position;phase=2;at=now
  log('READY own motion: three performers plus conductor; change QA view file for multiple angles')
 elseif phase==2 then
  assert(w.playing,'Band stopped while observing poses')
  for _,p in ipairs(api.performers()) do
   local b=p.body
   assert(p.bones==6 and (p.reachError or 0)<12,'Unreachable playing pose '..p.instrument..' '..tostring(p.reachError))
   if b and b.level>.9 then
    local e=extremes[p.instrument] or {minYaw=0,maxYaw=0,nod=0,roll=0,foot=0,error=0,reach=0,jitter=0};extremes[p.instrument]=e
    e.minYaw=math.min(e.minYaw,b.yaw);e.maxYaw=math.max(e.maxYaw,b.yaw);e.nod=math.max(e.nod,math.abs(b.nod));e.roll=math.max(e.roll,math.abs(b.roll));e.foot=math.max(e.foot,math.abs(b.foot));e.error=math.max(e.error,p.error or 0);e.reach=math.max(e.reach,p.reachError or 0)
    if p.instrument=='guitar' and p.propPosition then
     if e.prop then e.jitter=math.max(e.jitter,(p.propPosition-e.prop):length());assert(e.jitter<.02,'Strapped guitar attachment jitter') end
     e.prop=p.propPosition
    end
   end
   if p.instrument=='conductor' then flags.conductor=true end
  end
  if (self.position-origin):length()>1 then
   assert(w.playing,'Walking stopped the band');flags.walk=true
   if not flags.walkLog then flags.walkLog=true;log('PASS leader movement remains available and band keeps playing') end
  end
  if now-at>15 and not flags.reported then
   flags.reported=true
   for name,e in pairs(extremes) do log(string.format('METRICS %s yaw=%.4f..%.4f nod=%.4f sway=%.4f foot=%.4f error=%.3f reach=%.3f propJitter=%.5f',name,e.minYaw,e.maxYaw,e.nod,e.roll,e.foot,e.error,e.reach,e.jitter)) end
  end
 elseif phase==3 then
  if now-at<2 then return end
  for _,p in ipairs(api.performers()) do assert(p.instrument~='conductor','Stopped conductor overrides not cleared');assert(not p.body or p.body.level<.001,'Stopped body motion not settled') end
  assert(flags.conductor and extremes.drumkit and extremes.drumkit.maxYaw>.01 and extremes.drumkit.minYaw<-.01,'Drummer did not swivel both ways')
  assert(extremes.guitar.roll>.003 and extremes.flute.foot>.01 and extremes.flute.nod>.005,'Standing sway, foot tap or nod absent')
  assert(api.projectLibrary():save(true,'Private motion preference'));phase=4;at=now
 elseif phase==4 then
  if now-at<1 or api.projectLibrary().pending then return end
  assert(api.projectLibrary():open(api.projectLibrary().active,false,true));phase=5;at=now
 elseif phase==5 then
  if now-at<1 or api.projectLibrary().pending then return end
  assert(w.project.conductor==true and w.project.leaderPlays==false,'Conductor project preference lost')
  assert(w:setupNpcBand(false,false,true));phase=6;at=now
 elseif phase==6 then
  if now-at<2 or w.bandSetup then return end
  assert(#api.performers()==0,'Final pose cleanup failed');api.close();phase=99
  log('PASS complete: multi-view playing poses, both torso directions, beat head/foot/sway, conductor stop cue, saved preference and complete cleanup');core.quit();return
 end
 if phase==2 then
  local f=vfs.open('fetcher-studio88-qa-view.txt');local text=f and f:read('*a') or '';if f then f:close() end
  local index,angle=text:match('(%d+)%s*(%d*)');index,angle=tonumber(index),tonumber(angle) or 1
  if index==99 then w:stop(true);phase=3;at=now;log('READY stopped; checking all overrides settle')
  elseif index and index>=1 and index<=4 then view(index,angle)
   if text~=lastView then lastView=text;log('VIEW '..text) end
  end
 end
end
return {engineHandlers={onFrame=function(dt) if phase==99 then return end;local ok,err=pcall(run,dt);if not ok then phase=99;log('FAIL '..tostring(err));core.quit() end end}}
