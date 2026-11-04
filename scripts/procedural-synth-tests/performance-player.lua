-- Local-only visual/regression driver. Never ship in the package manifest.
local core=require('openmw.core');local self=require('openmw.self');local types=require('openmw.types')
local nearby=require('openmw.nearby');local I=require('openmw.interfaces');local synth=require('openmw.synth')
local camera=require('openmw.camera');local util=require('openmw.util');local vfs=require('openmw.vfs');local mp=require('openmw.mp')
local anim=require('openmw.animation');local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local Schema=require(prefix..'shared.schema');local F=require(prefix..'shared.project')(Schema)
local V=util.vector3;local names={'guitar','bass','acoustic','classical','drumkit'}
local phase,started,at,last,role=0,nil,0,nil,nil;local flags={};local checked={}
local function log(s) print('[StudioPerformanceQA] '..tostring(role)..' '..s) end
local function actorFor(name)
 local id;for _,p in ipairs(I.ProceduralSynth.performers()) do if p.instrument==name then id=tonumber(p.key:sub(3));break end end
 for _,a in ipairs(nearby.actors) do if mp.getObjectMpNum(a)==id then return a end end
end
local function view(index,angle,zoom)
 local name=names[index];local a=name and actorFor(name);if not a then return end
 local target=a.position+V(0,0,angle==7 and 30 or name=='drumkit' and 70 or 100)
 local offsets={V(0,130,10),V(-110,25,12),V(110,25,12),V(0,75,90),V(-60,65,-30),V(60,65,-30),V(-70,65,10)}
 local point=target+a.rotation*offsets[angle]*(zoom or 1);local delta=target-point
 camera.setMode(camera.MODE.Static);camera.setStaticPosition(point)
 camera.setYaw(math.atan2(delta.x,delta.y));camera.setPitch(-math.atan2(delta.z,math.sqrt(delta.x^2+delta.y^2)))
end
local function frame()
 local now=core.getRealTime();started=started or now;role=role or types.NPC.record(self).name
 if phase==99 then return end;assert(now-started<900,'Presentation QA timed out')
 synth.setMonitorVolume(0);local api=I.ProceduralSynth;if not api then return end
 local w=api.workstation()
 if role=='Synth B' then
  if #api.performers()==5 and not flags.observer then
   local frets,feet=false,false
   for _,p in ipairs(api.performers()) do
    assert(p.bones==6 and p.fingers>0 and not p.missingModel,'Observer missing mesh/pose '..p.instrument)
    frets=frets or p.fret~=nil;feet=feet or p.footRight~=nil
   end
   if frets and feet then flags.observer=true;log('PASS late observer receives native string/pedal presentation') end
  end
 else
  if phase==0 then
   if now-started<5 then return end
   if not w then for _,o in ipairs(nearby.activators) do if o.recordId=='fetcher_procedural_synth_v1' then o:activateBy(self);break end end;return end
   local library=api.projectLibrary();if not library or not library.ready or library.pending then return end
   local calibration=api.poseCalibration();for _,name in ipairs(calibration.names) do calibration:reset(name) end;calibration.enabled=true
   api.studio();assert(w:newSong(84,4,4,4));w.project.leaderPlays=false;w.project.autoBand=true;w.project.stageLayout='seated'
   for n=2,5 do assert(w:addPart(n==2 and 'bass' or n==5 and 'snare' or 'guitar')) end
   for index,t in ipairs(w.project.tracks) do
    t.instrument=names[index];t.followPrograms=false
    assert(w:preset(index,index==2 and 'b:sampled-bass-expressive' or index==5 and 'b:sampled-drum-kit' or index==3 and 'b:sampled-guitar-steel' or index==4 and 'b:sampled-guitar-nylon' or 'b:captured-guitar-thrash'))
    t.params.stringLayout=index==2 and 5 or index==5 and 0 or 1
    local notes={};local function add(pitch,start,length,string,direction)
     notes[#notes+1]={n=pitch,s=start,l=length,v=.8,e=string and {stringSelect=string,pickDirection=direction or 0} or nil}
    end
    if index==5 then
     for bar=0,3 do local b=bar*16
      for step=0,14,2 do add(42,b+step,.2) end
      for _,step in ipairs{0,2,6,8,10,14} do add(36,b+step,.2) end
      for _,step in ipairs{4,12} do add(38,b+step,.2) end
      add(46,b+7,.2);add(44,b+7.8,.2);add(49,b,.2);add(45,b+15,.2)
     end
    elseif index==2 then
     for step=0,60,4 do local string=step%8==0 and 1 or 2;add((string==1 and 28 or 33)+(step%16==0 and 5 or 2),step,3,string) end
    else
     for start=0,56,8 do
      local f=start%24==0 and 2 or start%24==8 and 5 or 9
      add(40+f,start,6,1,start%16==0 and 0 or 1);add(45+f,start+.08,5.9,2,0);add(50+f+1,start+.16,5.8,3,0)
     end
    end
    t.edited=assert(F.encodeNotes(notes,960))
   end
   w:touch();w:configureAll();assert(w:setupNpcBand(false));phase=1;at=now
  elseif phase==1 and not w.bandSetup and #api.performers()==5 then
   assert(w:play('song'));api.hide();phase=2;at=now;log('READY five instruments playing with explicit strings and a full kit')
  elseif phase==2 and now-at>.7 then
   for _,p in ipairs(api.performers()) do
    assert(p.bones==6 and p.fingers>0 and not p.missingModel,'Owner missing pose '..p.instrument)
    assert(p.reachError<4,'Hand target exceeds reachable pose '..p.instrument..' '..p.reachError)
    if p.reachError>3 and not flags['reach:'..p.instrument] then
     flags['reach:'..p.instrument]=true;log('REACH '..p.instrument..' '..p.reachError)
     local a=actorFor(p.instrument);if a then for _,side in ipairs{'L','R'} do
      local h=anim.getBoneTransform(a,'Bip01 '..side..' UpperArm',true,true)*V(0,0,0)
      local f=anim.getBoneTransform(a,'Bip01 '..side..' Forearm',true,true)*V(0,0,0)
      local w=anim.getBoneTransform(a,'Bip01 '..side..' Hand',true,true)*V(0,0,0)
      log('GEOMETRY '..p.instrument..' '..side..' shoulder='..tostring(a.rotation:inverse()*(h-a.position))..' length='..tostring((f-h):length()+(w-f):length()))
     end end
    end
    if p.instrument=='drumkit' and (p.legReachError or 0)>4 then error('Unreachable pedal target '..p.legReachError) end
    if p.fret then checked[p.instrument]=true end
    if p.footRight and p.footRight>0 then flags.kick=true end
    if p.footLeft and p.footLeft<0 then flags.openHat=true end
    if p.stickLeft and p.stickLeft>0 then flags.left=true end
    if p.stickRight and p.stickRight>0 then flags.right=true end
   end
   if checked.guitar and checked.bass and checked.acoustic and checked.classical and flags.kick and flags.openHat and flags.left and flags.right and not flags.accepted then
    flags.accepted=true;log('PASS runtime fretting, both sticks, kick and open/closed pedal movement')
   end
  end
 end
 if phase==2 or role=='Synth B' then
  local f=vfs.open('fetcher-studio74-qa-view.txt');local text=f and f:read('*a');if f then f:close() end
  local command={};for n in (text or ''):gmatch('%S+') do command[#command+1]=tonumber(n) end
  if command[1]==99 then
   if w then w:stop(true);api.close() end;phase=99;log('PASS visual inspection complete');core.quit();return
  end
  if command[1] and command[1]>=1 and command[1]<=5 then
   view(command[1],command[2] or 1,command[4])
   if text~=last then last=text;log('VIEW '..text:gsub('\n',' ')) end
  end
 end
end
return {engineHandlers={onFrame=function() local ok,err=pcall(frame);if not ok then log('FAIL '..tostring(err));core.quit();phase=99 end end}}
