package.path='apps/openmw-server/scripts/?.lua;'..package.path
local prefix='scripts.multiplayer.fetcher.procedural-synth.';local root='files/server/server-lua-packages/fetcher.procedural-synth/'
local schema=require('procedural_synth_schema');local F=require('procedural_synth_project')(schema);local Layout=require('procedural_synth_band_layout')
local n=0;local function check(b,m) n=n+1;assert(b,m) end
local function copy(v) if type(v)~='table' then return v end;local out={};for k,x in pairs(v) do out[k]=copy(x) end;return out end
if newproxy then
 local home=newproxy(true);getmetatable(home).__index={x=0,y=0,z=0}
 local position=newproxy(true);getmetatable(position).__index={x=120,y=240,z=10}
 check(Layout.inRange(home,position),'native read-only coordinate userdata is accepted alongside test tables')
end
local names={'violin','clarinet','trumpet','keyboard','guitar','bass','cello','flute','drumkit','timpani'}
for _,wide in ipairs{false,true} do for count=1,32 do
 local plan={wideSpacing=wide};for i=1,count do plan[i]={instrument=names[(i-1)%#names+1]} end
 for _,yaw in ipairs{0,math.pi/2,math.pi,-math.pi/2} do
  local anchor={x=300,y=-270,z=8,rz=yaw};local positions=assert(Layout.positions(anchor,plan))
  for i,p in ipairs(positions) do local old=assert(Layout.position(anchor,i,count,wide));check(p.x==old.x and p.y==old.y and p.rz==old.rz,'missing settings retain exact legacy grid positions') end
 end
end end
for _,preset in ipairs{'grid','sections','arc'} do for _,wide in ipairs{false,true} do for _,count in ipairs{1,2,7,8,17,32} do
 local plan={wideSpacing=wide};for i=1,count do plan[i]={instrument=names[(i-1)%#names+1]} end
 for _,yaw in ipairs{0,math.pi/2,math.pi} do for _,scale in ipairs{1,1.3,2} do
  local anchor={x=900,y=8100,z=-24,rz=yaw};local positions=assert(Layout.positions(anchor,plan,{preset=preset,spacing=scale,depth=1,distance=scale}))
  for i,p in ipairs(positions) do
   local dx,dy=p.x-anchor.x,p.y-anchor.y;local distance=math.sqrt(dx*dx+dy*dy)
   check(dx*math.sin(yaw)+dy*math.cos(yaw)>=(wide and 320 or 240)*scale-1e-6,'positions remain ahead of captured leader')
   check(math.abs(dx*math.sin(p.rz)+dy*math.cos(p.rz)+distance)<1e-5,'every preset keeps performers facing the leader')
   check(p.section==Layout.section(plan[i].instrument),'placement does not reorder route/member identities')
   for j=1,i-1 do local q=positions[j];check((p.x-q.x)^2+(p.y-q.y)^2>=((wide and 280 or 140)-.01)^2,'preset preserves bounded body/drum-kit clearance') end
  end
 end end
end end end
check(not F.formation(false) and not Layout.positions({x=0,y=0,z=0},{{instrument='keyboard'}},false),'false settings are invalid rather than a missing default')
local bad={{preset='url',spacing=1,depth=1,distance=1},{preset='grid',spacing=.5,depth=1,distance=1},{preset='grid',spacing=0/0,depth=1,distance=1},
 {preset='arc',spacing=1,depth=3,distance=1},{preset='grid',spacing=1,depth=1,distance=1,x=100},{preset='grid'}}
for _,value in ipairs(bad) do check(not F.formation(value) and not Layout.positions({x=0,y=0,z=0},{{instrument='keyboard'}},value),'bad formation cannot enter a project or placement') end
local p=F.new();p.formation={preset='sections',spacing=1.2,depth=1.3,distance=1.4};p.leaderPlays=false
for i,name in ipairs(names) do p.tracks[i]=F.newTrack('piano','Part '..i,16);p.tracks[i].instrument=name;p.tracks[i].params.fine1=i end
check(F.project(p).formation.depth==1.3,'project round trip preserves formation')
local plan=Layout.plan(p);local positions=Layout.positions({x=0,y=0,z=0},plan,p.formation);local fronts={}
for i,position in ipairs(positions) do local section=position.section;fronts[section]=fronts[section] or position.y end
check(fronts.strings<=fronts.winds and fronts.winds<=fronts.brass and fronts.brass<fronts.percussion,'sections use deterministic front-to-back families')
local band=F.band();for i,part in ipairs(plan) do band[i].target={kind='npc',mpNum=i,refId='npc'..i,instrument=part.instrument} end
band[12].target=copy(band[1].target)
local bound=Layout.bound(band,function(t) return t.mpNum~=2 end)
check(#bound==#plan-1 and bound[2].slot==3 and bound.wideSpacing,'restage retains unique bound slot identity and ownership filter')
-- Source modules remain shared byte-for-byte; workstation edits save/history without moving actors or stopping audio.
package.loaded[prefix..'shared.schema']=schema;package.loaded[prefix..'shared.project']=dofile(root..'shared/project.lua')
package.loaded[prefix..'client.midi']=dofile(root..'client/midi.lua');package.loaded[prefix..'client.soundsets']=dofile(root..'client/soundsets.lua')
local stopped,staged=0,0;local W=dofile(root..'client/workstation.lua');local w=W.new{panic=function() stopped=stopped+1 end,message=function() end,configure=function() end,setupBand=function() staged=staged+1 end}
w:add('piano');w.playing=true;w.native=true;local before=stopped
check(w:setFormation(p.formation,'preset') and w.playing and stopped==before and staged==0,'preview edits leave actual performers/audio untouched')
local saved=F.project(w.project);check(saved.formation.preset=='sections' and w:stageDescription().formation.spacing==1.2,'saved settings participate in managed-stage invalidation')
check(w:undo() and not w.project.formation and w:redo() and w.project.formation.preset=='sections','formation edits have project Undo/Redo')
w.bandSetup={};check(not w:setFormation{preset='arc',spacing=1,depth=1,distance=1},'in-flight stage geometry cannot change');w.bandSetup=nil
w.recording={};check(not w:setFormation{preset='arc',spacing=1,depth=1,distance=1},'recording keeps a stable document');w.recording=nil
for _,name in ipairs{'project','band-layout'} do local server=name=='band-layout' and 'band_layout' or name
 local a=assert(io.open(root..'shared/'..name..'.lua','rb'));local b=assert(io.open('apps/openmw-server/scripts/procedural_synth_'..server..'.lua','rb'));check(a:read('*a')==b:read('*a'),'client/server formation rules stay identical');a:close();b:close()
end
-- Real preflight and server stage job agree on custom geometry and floor heights.
do
 local now=100;local actors={};local registry={entries={}};local replies={};local leader={position={x=0,y=0,z=0,rz=.4}}
 for i=1,#plan do local entry={mpNum=i,refId='npc'..i};registry.entries[i]=entry;actors[i]={refId=entry.refId,actorInstanceId=i,position={x=0,y=0,z=0},cell='A'} end
 local mp={getObjectByMpNum=function() return {position=leader.position} end,getUptime=function() return now end,getPlayer=function() return leader end,getActorByMpNum=function(i) return actors[i] end,
  teleportActorByInstanceId=function(i,cell,position) actors[i].position=copy(position);actors[i].cell=cell;return true end,
  setCharacterStorageForCharacter=function() return true end}
 local P={spawning={},registry=function() return registry end,roster=function() end}
 local Stage=require('procedural_synth_stage')(mp,F,P,function(_,reply) replies[#replies+1]=reply end)
 local session={id=7,epoch=1,owner=1,characterId=11,cell='A',band=F.band()};local sessions={[7]=session}
 local Preflight=dofile(root..'client/stage-preflight.lua')({COLLISION_TYPE={World=1,Door=2,HeightMap=4},castRay=function(a,b)
  if a.x==b.x and a.y==b.y and a.z>b.z then return {hit=true,hitPos={z=a.x*.05+a.y*.01},hitNormal={x=-.05,y=-.01,z=1}} end;return {hit=false}
 end},{vector3=function(x,y,z) return {x=x,y=y,z=z} end,bitOr=function() return 7 end},Layout)
 local ground;local checkFloor=Preflight.new(leader.position,#plan,function(result,error) check(not error,'custom formation footprint passes the shared slope model');ground=result end,plan.wideSpacing,plan,p.formation)
 while not checkFloor.done do checkFloor:frame() end
 check(ground and #ground.heights==#plan,'preflight produces every custom slot floor height')
 check(Stage:begin(session,p,1,sessions,false,false,ground),'server accepts validated section settings and preflight')
 for i=1,300 do now=now+.11;Stage:tick(sessions);if not next(Stage.jobs) then break end end
 local result=replies[#replies];check(result and not result.error and #result.targets==#plan,'section stage job completes')
 local expected=Layout.positions(leader.position,plan,p.formation)
 for i,target in ipairs(result.targets) do
  local a=actors[target.mpNum].position;check(math.abs(a.x-expected[i].x)<1e-7 and math.abs(a.y-expected[i].y)<1e-7 and a.z==ground.heights[i],'server uses shared preset positions and preflight Z instead of the old grid')
  session.band[i].target=target
 end
 local config={preset='arc',spacing=1.4,depth=1.2,distance=1};local bound=Layout.bound(session.band)
 check(Stage:begin(session,{formation=config},2,sessions,true),'custom arc restage begins')
 for i=1,300 do now=now+.11;Stage:tick(sessions);if not next(Stage.jobs) then break end end
 expected=Layout.positions(leader.position,bound,config);result=replies[#replies];check(not result.error,'arc restage completes')
 for i,target in ipairs(result.targets) do check(math.abs(actors[target.mpNum].position.x-expected[i].x)<1e-7,'restage preserves target order while applying a new preset') end
 -- An explicit legacy project resets a prior preset to the default grid.
 local huge=copy(p);huge.formation={preset='arc',spacing=2,depth=2,distance=2}
 local oldRange=Layout.stageRange;Layout.stageRange=100
 local prior=copy(actors[1].position)
 check(not Stage:begin(session,huge,99,sessions,false) and not next(Stage.jobs) and actors[1].position.x==prior.x,'out-of-range formation is rejected before any actor movement')
 Layout.stageRange=oldRange
 local legacy=copy(p);legacy.formation=nil;check(Stage:begin(session,legacy,3,sessions,false),'legacy project can replace a previous custom formation')
 for i=1,300 do now=now+.11;Stage:tick(sessions);if not next(Stage.jobs) then break end end
 expected=Layout.positions(leader.position,plan);result=replies[#replies];check(not result.error,'legacy replacement completes')
 check(math.abs(actors[result.targets[1].mpNum].position.x-expected[1].x)<1e-7,'legacy nil means grid, rather than inheriting a prior song preset')
end
print('PASS formation presets/sections/clearance/facing, legacy positions, strict project settings, stable routing, ownership/dedup and non-moving preview/history; '..n..' assertions')
