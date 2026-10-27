package.path='apps/openmw-server/scripts/?.lua;'..package.path
local root='files/server/server-lua-packages/fetcher.procedural-synth/'
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=require('procedural_synth_schema');local Format=require('procedural_synth_project')(schema)
local Layout=require('procedural_synth_band_layout');local checks=0
local function check(value,why) checks=checks+1;assert(value,why) end
local function copy(v) if type(v)~='table' then return v end;local o={};for k,x in pairs(v) do o[k]=copy(x) end;return o end
check(io.open(root..'shared/band-layout.lua','rb'):read('*a')==io.open('apps/openmw-server/scripts/procedural_synth_band_layout.lua','rb'):read('*a'),'shared formation rules mirror the server')
for _,yaw in ipairs{0,math.pi/2,math.pi,-math.pi/2} do
 local leader={x=100,y=-60,z=20,rz=yaw};local positions={}
 for n=1,32 do local p=assert(Layout.position(leader,n,32));positions[n]=p
  local dx,dy=p.x-leader.x,p.y-leader.y
  check(dx*math.sin(yaw)+dy*math.cos(yaw)>=239.99,'every row is ahead of leader yaw')
  local facing=(leader.x-p.x)*math.sin(p.rz)+(leader.y-p.y)*math.cos(p.rz)
  check(math.abs(facing-math.sqrt(dx*dx+dy*dy))<1e-5,'every performer faces the conductor, including edge columns')
  for k=1,n-1 do local q=positions[k];check((p.x-q.x)^2+(p.y-q.y)^2>=139.99^2,'grid avoids overlapping performers') end
 end
end
check(not Layout.position({x=0/0,y=0,z=0},1,1),'invalid leader coordinates rejected')
local now=100;local actors,storage,sent,queue,teleports={},{},{},{},{};local nextActor=50;local failSpawn=false
local leader={guid=1,characterId=11,name='Leader',health=100,cell='A',position={x=0,y=0,z=0,rz=0},inventory={}}
package.loaded['openmw.types']={Actor={inventory=function(p) return p.inventory end}}
local mp={getUptime=function() return now end,getPlayer=function(id) if id==1 then return leader end end,
 getPlayers=function() return {leader} end,getObjectByMpNum=function() return {position={x=0,y=0,z=0}} end,
 getActorByMpNum=function(id) return actors[id] end,getActorsInCell=function() local o={};for _,a in pairs(actors) do o[#o+1]=a end;return o end,
 getCharacterStorageForCharacter=function(id,_,key) return storage[id..':'..key] end,
 setCharacterStorageForCharacter=function(id,_,key,value) storage[id..':'..key]=value;return true end,
 upsertDynamicRecord=function() return true end,
 spawnActor=function(id,_,__,cell,position,options) if failSpawn then return false end;queue[#queue+1]={refId=id,cell=cell,position=copy(position),options=options};return true end,
 teleportActorByInstanceId=function(id,cell,position)
  teleports[#teleports+1]={id=id,cell=cell,position=copy(position)};actors[id].cell=cell;actors[id].position=copy(position);return true
 end}
local emit=function(guid,data) sent[#sent+1]={guid=guid,data=data} end
local P=require('procedural_synth_performers')(mp,Format,emit)
local S=require('procedural_synth_stage')(mp,Format,P,emit)
local session={id=7,epoch=1,owner=1,characterId=11,cell='A',band=Format.band()};local sessions={[7]=session}
local project=Format.new()
for n=1,32 do project.tracks[n]=Format.newTrack('midi','Part '..n,16);project.tracks[n].sourceRole=n%3==0 and 'guitar' or n%3==1 and 'keys' or 'snare' end
local function drain()
 for _,p in ipairs(queue) do nextActor=nextActor+1;p.mpNum=nextActor
  actors[nextActor]={mpNum=nextActor,actorInstanceId=nextActor,refId=p.refId,cell=p.cell,position=p.position,isDead=false}
  P:spawned(p,sessions)
 end;queue={}
end
local function pump() for _=1,380 do now=now+.11;S:tick(sessions);drain();if not next(S.jobs) then return end end;error('job did not finish') end
check(S:begin(session,project,1,sessions,false),'32-part automatic setup starts')
check(not S:begin(session,project,2,sessions,false),'second job cannot race the same character roster')
pump();local result
for _,s in ipairs(sent) do if s.data.op=='bandSetup' then result=s.data end end
check(result and not result.error and #result.targets==32 and #P:registry(11).entries==32,'native spawn completions produce all 32 owned targets')
local first=result.targets[1].mpNum
for n,t in ipairs(result.targets) do
 check(t.instrument==Layout.instrument(project.tracks[n]),'physical role mapping matches project')
 session.band[n].target=t
end
check(result.targets[1].instrument=='keyboard' and result.targets[2].instrument=='drum' and result.targets[3].instrument=='lute','keyboard/percussion/pluck defaults are distinct')
local serial=P:registry(11).nextId
check(S:begin(session,project,3,sessions,false),'existing complete roster can be reused at capacity');pump()
check(P:registry(11).nextId==serial and #teleports==32,'repeating Auto teleports owned actors without creating duplicates')
leader.position.rz=math.pi/2
check(S:begin(session,nil,4,sessions,true),'explicit restage begins from current leader facing');pump()
check(#teleports==64 and actors[first].position.x>239,'restage moves the existing formation ahead of the new facing')
local other={id=9,band=Format.band()};other.band[1].target=result.targets[1];sessions[9]=other
check(not S:begin(session,project,5,sessions,false),'busy owned performer is never stolen from another active band at roster capacity')
check(not S:begin(session,nil,5,sessions,true),'restage cannot move a performer claimed by another active band')
check(sent[#sent].data.op=='bandSetup' and sent[#sent].data.requestId==5 and sent[#sent].data.error,'rejected setup reports a matching completion so the client need not wait for timeout')
sessions[9]=nil
check(S:begin(session,project,6,sessions,false),'new job can begin after prior completion')
session.epoch=2;S:tick(sessions);check(not next(S.jobs),'session loss/epoch changes cancel queued world actions')
-- A rejected spawn rolls back its roster slot rather than consuming the hire limit.
local fresh={id=8,epoch=1,owner=1,characterId=12,cell='A',band=Format.band()};sessions[8]=fresh
failSpawn=true
local one=Format.new();one.tracks[1]=Format.newTrack('piano','One',16)
check(S:begin(fresh,one,7,sessions,false),'failure case begins');pump()
check(#P:registry(12).entries==0,'native spawn queue rejection does not leave an unspawned roster entry')
-- Client routing commits only the matching, unchanged document as one Undo step.
package.loaded[prefix..'shared.schema']=schema;package.loaded[prefix..'shared.project']=dofile(root..'shared/project.lua')
package.loaded[prefix..'client.midi']=dofile(root..'client/midi.lua');package.loaded[prefix..'client.soundsets']=dofile(root..'client/soundsets.lua')
package.loaded[prefix..'client.arrangement']=dofile(root..'client/arrangement.lua')
local Workstation=dofile(root..'client/workstation.lua');local requests={}
local w=Workstation.new{configure=function() end,panic=function() end,message=function() end,now=function() return now end,
 off=function() end,band=function() end,setupBand=function(id,restage,p) requests[#requests+1]={id=id,restage=restage,project=p} end}
w:newSong(120,4,4,8);for n=2,32 do w:addPart('keys') end
check(w:setupNpcBand(false) and not w:play('song'),'pending stage setup prevents playback against half-built routing')
check(not w:receiveBandSetup{requestId=999,restage=false,targets=result.targets} and w.bandSetup,'unmatched replies cannot change the routing or cancel a pending job')
local history=#w.undoStack
check(w:receiveBandSetup{requestId=requests[1].id,restage=false,targets=result.targets},'matching complete NPC setup commits')
check(#w.undoStack==history+1 and w.project.tracks[32].output.slot==32 and w.project.band[32].target,'32-part routing is one validated Undo action')
check(w:undo() and w.project.tracks[32].output.slot==0 and w:redo() and w.project.tracks[32].output.slot==32,'routing supports Undo/Redo without changing the owned roster')
w:setupNpcBand(false);w:renameTrack(1,'Changed while waiting')
check(not w:receiveBandSetup{requestId=requests[2].id,restage=false,targets=result.targets} and w.project.tracks[1].name=='Changed while waiting','late automatic setup cannot overwrite newer edits')
w:setupNpcBand(false)
check(not w:receiveBandSetup{requestId=requests[3].id,restage=false,error='No available performers'} and not w.bandSetup,'rejected setup immediately releases the pending UI')
-- Stand controller applies only to this package's generated NPC identities.
local npc={recordId='fetcher_synth_bard_11_1',controls={}}
function npc:enableAI(enabled) self.ai=enabled end
package.loaded['openmw.self']=npc;dofile(root..'client/stage-actor.lua').engineHandlers.onUpdate()
check(npc.ai==false and npc.controls.movement==0 and npc.controls.yawChange==0 and npc.controls.use==0,'owned NPC stays still rather than wandering, facing away or fighting')
npc.recordId='fargoth';check(not dofile(root..'client/stage-actor.lua').engineHandlers,'ordinary NPCs are unaffected')
print('PASS orchestra formation, 32-part automatic/reused routing, authority, restaging, failure recovery, Undo and stand controller; '..checks..' assertions')
