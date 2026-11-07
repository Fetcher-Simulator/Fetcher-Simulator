-- Private copied-fixture acceptance; UI settings must match authoritative actor positions.
local core=require('openmw.core');local self=require('openmw.self');local nearby=require('openmw.nearby');local types=require('openmw.types')
local I=require('openmw.interfaces');local synth=require('openmw.synth')
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local F=require(prefix..'shared.project')(require(prefix..'shared.schema'));local Layout=require(prefix..'shared.band-layout')
local started,role,phase,at,anchor,identities,previous=nil,nil,0,0,nil,nil,nil;local flags={}
local function log(s) print('[StudioFormationQA] '..tostring(role)..' '..s) end
local function capture() local p=self.position;local _,rz=self.rotation:getAnglesXZ();return {x=p.x,y=p.y,z=p.z,rz=rz} end
local function actual(target)
 for _,o in ipairs(nearby.actors) do if o.recordId==target.refId then return o.position end end
end
local function verify(w,expected,stable)
 local bound=Layout.bound(w.project.band);assert(#bound==4,'Expected four bound NPCs');local out={}
 for n,part in ipairs(bound) do
  local p=actual(part.target);if not p then return end
  assert((p.x-expected[n].x)^2+(p.y-expected[n].y)^2<32^2,'Native NPC XY differs from formation preview')
  if stable then assert(part.target.mpNum==stable[n],'Restage changed an owned actor identity') end
  out[n]=part.target.mpNum
 end;return out
end
local function run()
 local now=core.getRealTime();started=started or now;role=role or types.NPC.record(self).name;assert(now-started<420,'Formation acceptance timed out');synth.setMonitorVolume(0)
 local api=I.ProceduralSynth;if not api then return end;local w=api.workstation();local states=api.status()
 if role=='Synth B' then
  for _,state in ipairs(states) do if state.nativePlaying and state.voices>0 then assert(state.nativeFault==0,'Listener native fault');flags.audio=true end end
  if flags.audio and #api.performers()>=4 then phase=99;log('PASS complete: late listener sees four physical performers and hears the sounding staged band');core.quit() end;return
 end
 if phase==0 then
  if now-started<4 then return end
  if not w then for _,o in ipairs(nearby.activators) do if o.recordId=='fetcher_procedural_synth_v1' and o.position.x<2400 then o:activateBy(self);return end end;return end
  if not api.projectLibrary().ready or api.projectLibrary().pending then return end
  local p=F.new();p.name='Private formation acceptance';p.autoBand=false;p.leaderPlays=false;p.stageLayout='standing'
  for n,instrument in ipairs{'violin','violin','flute','flute'} do
   p.tracks[n]=F.newTrack('piano','Stage '..instrument,16);p.tracks[n].instrument=instrument;p.tracks[n].params.fine1=n
   p.tracks[n].notes={{n=59+n,s=0,l=2,v=.7},{n=64+n,s=8,l=2,v=.7}}
  end
  assert(w:load(p));w.page='band';api.studio();phase=1;at=now;log('READY Band: click Formation, choose Instrument sections and increase Across spacing')
 elseif phase==1 then
  if w.page=='formation' and w.project.formation and w.project.formation.preset=='sections' and w.project.formation.spacing>1.05 then
   anchor=capture();phase=2;at=now;log('READY Apply sections: preview updated; click Create / update auto band')
  end
 elseif phase==2 then
  if w.bandSetup then flags.staging=true;return end
  if flags.staging and Layout.bound(w.project.band)[4] then phase=3;at=now end
 elseif phase==3 then
  if now-at<3 then return end
  local plan=Layout.plan(w.project);local expected=Layout.positions(anchor,plan,w.project.formation)
  identities=verify(w,expected);if not identities then return end
  previous=expected;assert(w:play('clips'));api.hide();phase=4;at=now;log('READY own formation: authoritative section positions match preview; four NPCs sounding')
 elseif phase==4 then
  if now-at<18 then return end
  assert(w.playing,'Band unexpectedly stopped');w.page='formation';api.studio();phase=5;at=now;log('READY Arc preview: choose Arc; existing band must stay put until Restage bound NPCs')
 elseif phase==5 then
  if w.project.formation and w.project.formation.preset=='arc' then
   assert(w.playing,'Preview edits stopped the live band');assert(verify(w,previous,identities),'Preview edits moved actors')
   anchor=capture();flags.restaging=nil;phase=6;at=now;log('READY Restage arc: live band unchanged; click Restage bound NPCs')
  end
 elseif phase==6 then
  if w.bandSetup then flags.restaging=true;return end
  if flags.restaging then phase=7;at=now end
 elseif phase==7 then
  if now-at<3 then return end
  local plan=Layout.bound(w.project.band);local expected=Layout.positions(anchor,plan,w.project.formation)
  if not verify(w,expected,identities) then return end
  assert(api.projectLibrary():save(true,'Private saved formation'));phase=8;at=now
 elseif phase==8 then
  if now-at<1 or api.projectLibrary().pending then return end
  flags.spacing=w.project.formation.spacing;assert(api.projectLibrary():open(api.projectLibrary().active,false,true));phase=9;at=now
 elseif phase==9 then
  if now-at<1 or api.projectLibrary().pending then return end
  assert(w.project.formation.preset=='arc' and w.project.formation.spacing==flags.spacing,'Formation project persistence')
  assert(w:setupNpcBand(false,false,true));phase=10;at=now
 elseif phase==10 then
  if now-at<2 or w.bandSetup then return end
  assert(#api.performers()==0,'Owned NPCs were not removed');api.close();phase=99
  log('PASS complete: native Formation UI/spacing, authoritative section positions, no movement or audio stop while previewing, identity-preserving arc restage, private save/reload and cleanup');core.quit()
 end
end
return {engineHandlers={onFrame=function() if phase==99 then return end;local ok,err=pcall(run);if not ok then phase=99;log('FAIL '..tostring(err));core.quit() end end}}
