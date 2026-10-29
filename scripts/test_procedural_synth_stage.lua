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
 removeActor=function(id) if not actors[id] then return false end;actors[id]=nil;return true end,
 spawnActor=function(id,_,__,cell,position,options) if failSpawn then return false end;queue[#queue+1]={refId=id,cell=cell,position=copy(position),options=options};return true end,
 teleportActorByInstanceId=function(id,cell,position)
  teleports[#teleports+1]={id=id,cell=cell,position=copy(position)};actors[id].cell=cell;actors[id].position=copy(position);return true
 end}
local emit=function(guid,data) sent[#sent+1]={guid=guid,data=data} end
local P=require('procedural_synth_performers')(mp,Format,emit)
local S=require('procedural_synth_stage')(mp,Format,P,emit)
local session={id=7,epoch=1,owner=1,characterId=11,cell='A',band=Format.band()};local sessions={[7]=session}
local project=Format.new();project.leaderPlays=false
for n=1,32 do project.tracks[n]=Format.newTrack('midi','Part '..n,16);project.tracks[n].sourceRole=n%2==0 and 'guitar' or 'keys';project.tracks[n].params.fine1=n end
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
check(result.targets[1].instrument=='keyboard' and result.targets[2].instrument=='lute','distinct melodic timbres remain separate physical performers')
local serial=P:registry(11).nextId
check(S:begin(session,project,3,sessions,false),'existing complete roster can be reused at capacity');pump()
check(P:registry(11).nextId==serial and #teleports==32,'repeating Auto teleports owned actors without creating duplicates')
leader.position.rz=math.pi/2
check(S:begin(session,nil,4,sessions,true),'explicit restage begins from current leader facing');pump()
check(#teleports==64 and actors[first].position.x>239,'restage moves the existing formation ahead of the new facing')
local other={id=9,characterId=11,band=Format.band()};other.band[1].target=result.targets[1];sessions[9]=other
check(not S:begin(session,project,5,sessions,false),'busy owned performer is never stolen from another active band at roster capacity')
check(not S:begin(session,nil,5,sessions,true),'restage cannot move a performer claimed by another active band')
check(sent[#sent].data.op=='bandSetup' and sent[#sent].data.requestId==5 and sent[#sent].data.error,'rejected setup reports a matching completion so the client need not wait for timeout')
sessions[9]=nil
check(S:begin(session,project,6,sessions,false),'new job can begin after prior completion')
session.epoch=2;S:tick(sessions);check(not next(S.jobs),'session loss/epoch changes cancel queued world actions')
-- A rejected spawn rolls back its roster slot rather than consuming the hire limit.
local fresh={id=8,epoch=1,owner=1,characterId=12,cell='A',band=Format.band()};sessions[8]=fresh
failSpawn=true
local one=Format.new();one.leaderPlays=false;one.tracks[1]=Format.newTrack('piano','One',16)
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
check(w:receiveBandSetup{requestId=requests[1].id,restage=false,targets=result.targets,routes=result.routes},'matching complete NPC setup commits')
check(#w.undoStack==history+1 and w.project.tracks[32].output.slot==32 and w.project.band[32].target,'32-part routing is one validated Undo action')
check(w:undo() and w.project.tracks[32].output.slot==0 and w:redo() and w.project.tracks[32].output.slot==32,'routing supports Undo/Redo without changing the owned roster')
w:setupNpcBand(false);w:renameTrack(1,'Changed while waiting')
check(not w:receiveBandSetup{requestId=requests[2].id,restage=false,targets=result.targets} and w.project.tracks[1].name=='Changed while waiting','late automatic setup cannot overwrite newer edits')
w:setupNpcBand(false)
check(not w:receiveBandSetup{requestId=requests[3].id,restage=false,error='No available performers'} and not w.bandSetup,'rejected setup immediately releases the pending UI')
-- Grouping changes destinations, never the musical tracks or patches.
local grouped=Format.new();local kinds={'keys','keys'}
for n=1,14 do
    local t=Format.newTrack(n>=3 and n<=12 and 'drum' or 'piano','Part '..n,16)
    t.sourceRole=n<=2 and 'keys' or n<=12 and 'snare' or 'strings'
    t.instrument=n<=2 and 'keyboard' or n<=12 and 'drum' or n==13 and 'violin' or 'cello'
    if n>=3 then t.params.fine1=n end
    grouped.tracks[n]=t
end
local plan=Layout.plan(grouped)
check(#plan==3 and plan.routes[1]==0 and plan.routes[2]==0,'leader plays compatible keyboard parts at the workstation')
for n=3,12 do check(plan.routes[n]==1,'independent drum lanes share one physical drummer') end
check(plan.routes[13]==2 and plan.routes[14]==3 and #grouped.tracks==14,'different melodic instruments retain their own performers and source tracks')
grouped.leaderPlays=false;check(#Layout.plan(grouped)==4,'leader can choose to conduct without playing');grouped.leaderPlays=nil
failSpawn=false;session.band=Format.band()
check(S:begin(session,grouped,10,sessions,false),'grouped setup begins with a pre-existing oversized roster');pump()
local groupedResult=sent[#sent-1].data
for _,message in ipairs(sent) do if message.data.op=='bandSetup' and message.data.requestId==10 then groupedResult=message.data end end
check(#P:registry(11).entries==3 and groupedResult.removed==29,'surplus and unbound old hires are removed rather than left in corners')
for n,target in ipairs(groupedResult.targets) do session.band[n].target=target end
local busyTarget=groupedResult.targets[1];sessions[9]={id=9,characterId=11,band=Format.band()};sessions[9].band[1].target=busyTarget
sessions[10]={id=10,characterId=88,band=Format.band()};sessions[10].band[1].target=groupedResult.targets[2]
local leaderOnly=Format.new();leaderOnly.tracks[1]=Format.newTrack('piano','Solo',16)
check(S:begin(session,leaderOnly,11,sessions,false),'a keyboard-only song needs zero NPCs');pump()
check(#P:registry(11).entries==1 and actors[busyTarget.mpNum],'automatic cleanup preserves an actor claimed by another active band')
check(not actors[groupedResult.targets[2].mpNum],'a foreign character cannot block owned-NPC cleanup by forging a band target')
sessions[10]=nil
check(S:begin(session,nil,12,sessions,false,true),'explicit clear works without any song parts');pump()
check(#P:registry(11).entries==1,'clear cannot remove another active band performer')
sessions[9]=nil;check(S:begin(session,nil,13,sessions,false,true),'clear retries after the other band releases');pump()
check(#P:registry(11).entries==0,'clear includes unbound owned NPCs')
-- A delayed native spawn after Finish must not leave an orphan.
check(S:begin(session,one,14,sessions,false),'pending spawn cleanup starts');now=now+.2;S:tick(sessions)
check(#queue==1,'native spawn is pending at session close')
sessions[session.id]=nil;S:release(session,sessions);drain()
check(#P:registry(11).entries==0 and not next(S.jobs),'session release and late spawn completion leave no managed NPCs')
sessions[session.id]=session
check(P:hire(session,1),'manual hire starts for clear-race coverage')
check(not S:begin(session,one,15,sessions,false),'automatic setup cannot race an outstanding manual hire')
check(S:begin(session,nil,16,sessions,false,true),'clear can cancel a pending manual hire');now=now+1;S:tick(sessions);drain()
check(#P:registry(11).entries==0 and not next(actors),'a manual spawn arriving after clear does not leave an unregistered actor')
local aligned
mp.teleportPlayer=function(guid,cell,position) aligned=copy(position);return guid==1 and cell=='A' end
local alignStage=require('procedural_synth_stage')(mp,Format,P,emit,'meshes/fetcher/synth/keyboard-stand.nif')
check(alignStage:begin(session,leaderOnly,17,sessions,false) and aligned.x==0 and aligned.y==-50 and aligned.z==0,'participating leader aligns at the fixed stand playing side')
alignStage:tick(sessions)
local seated=Format.new();seated.leaderPlays=false;seated.stageLayout='seated';seated.tracks[1]=Format.newTrack('piano','Cello',16);seated.tracks[1].instrument='cello'
local floor={anchor=copy(leader.position),heights={3}}
check(S:begin(session,seated,18,sessions,false,false,floor),'seated setup accepts bounded matching floor hints');pump()
local entry=P:registry(11).entries[1];local target={kind='npc',mpNum=entry.mpNum,refId=entry.refId,instrument='cello'}
session.band=Format.band();session.band[1].target=target
local status,runtime=P:resolve(session,1,target)
check(status=='Ready' and runtime.posture=='seated' and actors[entry.mpNum].position.z==3,'seated intent and measured height accompany the owned performer')
check(Format.project(seated).stageLayout=='seated','layout persists in the validated project')
seated.stageLayout='invalid';check(not Format.project(seated),'unknown layout rejected');seated.stageLayout='seated'
local before=#teleports;floor.heights[1]=500
check(not S:begin(session,seated,19,sessions,false,false,floor) and #teleports==before,'unbounded floor cannot teleport an owned NPC')
floor.heights[1]=3;floor.anchor.x=100
check(not S:begin(session,seated,20,sessions,false,false,floor),'stale leader anchor rejected')
floor.anchor.x=leader.position.x
floor.anchor.rz=floor.anchor.rz+2*math.pi
floor.heights.extra=3;check(not S:begin(session,seated,21,sessions,false,false,floor),'non-array floor fields rejected');floor.heights.extra=nil
floor.anchor.extra={};check(not S:begin(session,seated,21,sessions,false,false,floor),'extra anchor payload rejected');floor.anchor.extra=nil
check(S:begin(session,{stageLayout='standing'},21,sessions,true,false,floor),'standing restage reuses the seated performer');pump()
status,runtime=P:resolve(session,1,target);check(status=='Ready' and not runtime.posture,'standing restage clears the replicated seated intent')
-- Play waits for automatic staging and then resumes exactly once.
w:newSong(120,4,4,8);w:setAutoBand(true)
check(w:play('clips') and w.bandSetup and not w.playing,'managed Play queues setup before starting the score')
local request=w.bandSetup.requestId
check(w:receiveBandSetup{requestId=request,restage=false,targets={},routes={0}} and w.playing,'leader-only setup resumes playback without creating NPCs')
w:stop(true);check(w:play('clips') and not w.bandSetup,'unchanged song restarts without moving the formation')
w:stop(true);w.project.tracks[1].params.fine1=23
check(w:play('clips') and w.bandSetup,'changed instrument grouping refreshes the lineup before playing')
request=w.bandSetup.requestId;w:stop(true)
check(w:receiveBandSetup{requestId=request,restage=false,targets={},routes={0}} and not w.playing,'Stop during setup cancels deferred playback')
check(Format.project(w.project).autoBand==true,'managed mode survives project validation/save')
w.project.leaderPlays='yes';check(not Format.project(w.project),'malformed leader settings are rejected');w.project.leaderPlays=nil
w:setOutput(1,0);check(not w.project.autoBand,'explicit manual routing turns off automatic replacement')
-- Stand controller applies only to this package's generated NPC identities.
local verifySession={id=70,epoch=1,owner=1,characterId=70,cell='A',band=Format.band()};sessions[70]=verifySession
check(S:begin(verifySession,one,70,sessions,false),'readback regression starts with a real spawned actor');pump()
local verifyResult=sent[#sent].data
for n=#sent,1,-1 do if sent[n].data.op=='bandSetup' and sent[n].data.requestId==70 then verifyResult=sent[n].data;break end end
verifySession.band[1].target=verifyResult.targets[1]
local originalTeleport=mp.teleportActorByInstanceId
local attempts=0;mp.teleportActorByInstanceId=function(_,_,_,owner) attempts=attempts+1;check(owner==1,'owned NPC teleport carries trusted owner');return true end
leader.position.x=leader.position.x+140
check(S:begin(verifySession,one,71,sessions,false),'queued native move can be accepted without being applied');pump()
check(attempts==2 and sent[#sent].data.error,'unchanged native position is retried and rejected instead of claiming setup succeeded')
mp.teleportActorByInstanceId=originalTeleport
check(S:begin(verifySession,one,72,sessions,false),'same owned identity can recover after rejected native movement');pump()
local verifiedActor=actors[verifySession.band[1].target.mpNum];local expected=Layout.position(leader.position,1,1)
check(math.abs(verifiedActor.position.x-expected.x)<.01 and not sent[#sent].data.error,'readback confirms the new slot before completing setup')
local npc={recordId='fetcher_synth_bard_11_1',controls={}}
function npc:enableAI(enabled) self.ai=enabled end
package.loaded['openmw.self']=npc;dofile(root..'client/stage-actor.lua').engineHandlers.onUpdate()
check(npc.ai==false and npc.controls.movement==0 and npc.controls.yawChange==0 and npc.controls.use==0,'owned NPC stays still rather than wandering, facing away or fighting')
npc.recordId='fargoth';check(not dofile(root..'client/stage-actor.lua').engineHandlers,'ordinary NPCs are unaffected')
print('PASS orchestra formation, 32-part automatic/reused routing, authority, restaging, failure recovery, Undo and stand controller; '..checks..' assertions')
