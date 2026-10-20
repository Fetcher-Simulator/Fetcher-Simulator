package.path='apps/openmw-server/scripts/?.lua;'..package.path
local format=require('procedural_synth_project')(require('procedural_synth_schema'))
package.loaded['openmw.types']={Actor={inventory=function(p) return p.inventory end}}
local checks=0;local function check(ok,msg) checks=checks+1;assert(ok,msg) end
local now=100;local sent,storage,actors,spawns,records,removed={},{},{},{},{},{}
local players={ [1]={guid=1,characterId=11,name='Leader',health=100,cell='A',position={x=0,y=0,z=0},inventory={}},
    [2]={guid=2,characterId=22,name='Member',health=100,cell='A',position={x=10,y=0,z=0},inventory={{refId='misc_de_lute_01',count=1}}},
    [3]={guid=3,characterId=33,name='Observer',health=100,cell='A',position={x=20,y=0,z=0},inventory={}} }
local mp={getUptime=function() return now end,getPlayer=function(id) return players[id] end,
    getPlayers=function() local out={};for _,p in pairs(players) do out[#out+1]=p end;return out end,
    getObjectByMpNum=function(id) if id==7 then return {position={x=0,y=0,z=0}} end end,
    getActorByMpNum=function(id) return actors[id] end,getActorsInCell=function() local out={};for _,a in pairs(actors) do out[#out+1]=a end;return out end,
    getCharacterStorageForCharacter=function(id,_,key) return storage[id..':'..key] end,
    setCharacterStorageForCharacter=function(id,_,key,value) storage[id..':'..key]=value;return true end,
    upsertDynamicRecord=function(kind,id,value) records[id]=value;return true end,
    spawnActor=function(id) spawns[#spawns+1]=id;return true end,
    removeActor=function(id,cell) removed[#removed+1]={id=id,cell=cell};return true end}
local emit=function(guid,data) sent[#sent+1]={guid=guid,data=data} end
local factory=require('procedural_synth_performers');local P=factory(mp,format,emit)
local s={id=7,owner=1,characterId=11,epoch=5,cell='A',band=format.band()};local sessions={[7]=s}
s.band[1].target={kind='player',characterId=22,instrument='lute'}
P:bind(s);local token=P.pending[2].token
check(P:resolve(s,1,s.band[1].target)=='Invitation pending','binding a player never implies consent')
P:request({op='respond',token=token,accept=true},players[3],sessions)
check(not P.accepted[2] and not P.accepted[3],'another player cannot accept a stolen invitation token')
P:request({op='respond',token=token,accept=true},players[2],sessions)
check(P.accepted[2] and P:resolve(s,1,s.band[1].target)~='Ready','acceptance alone does not equip an instrument')
P:request({op='hold',instrument='drum'},players[2],sessions)
check(not P.held[2],'a claimed inventory item cannot ready a missing instrument')
P:request({op='hold',instrument='lute'},players[2],sessions)
local status,runtime=P:resolve(s,1,s.band[1].target)
check(status=='Ready' and runtime.guid==2 and runtime.characterId==22,'native character identity resolves to the consenting live actor')
players[2].inventory={};check(P:resolve(s,1,s.band[1].target)~='Ready','losing the physical item silences its destination')
players[2].inventory={{refId='misc_de_lute_01',count=1}}
P:request({op='leaveBand'},players[2],sessions)
check(not P.accepted[2] and not P.held[2] and P:resolve(s,1,s.band[1].target)=='Invite required','Leave revokes consent and readiness')
local before=#sent;P:bind(s);check(#sent==before,'mix/refresh changes cannot undo a member leaving by auto-inviting again')
P:invite(s,1);token=P.pending[2].token;P:request({op='respond',token=token,accept=false},players[2],sessions)
check(not P.accepted[2] and not P.pending[2],'Decline does not start a performance')
P:invite(s,1);token=P.pending[2].token;now=131
P:request({op='respond',token=token,accept=true},players[2],sessions)
check(not P.accepted[2] and P:resolve(s,1,s.band[1].target)=='Invite required','expired invitation cannot be replayed')
P:invite(s,1);token=P.pending[2].token;s.epoch=6
P:request({op='respond',token=token,accept=true},players[2],sessions);check(not P.accepted[2],'old session epoch cannot authorize a new session')
P:release({id=7,epoch=5});P:invite(s,1);token=P.pending[2].token
P:request({op='respond',token=token,accept=true},players[2],sessions);P:request({op='hold',instrument='lute'},players[2],sessions)
players[2].health=0;check(P:resolve(s,1,s.band[1].target)=='Dead' and not P.accepted[2],'death revokes consent')
players[2].health=100;check(P:resolve(s,1,s.band[1].target)=='Invite required','revival needs fresh consent')
P:disconnect(2);players[4]=players[2];players[4].guid=4;players[2]=nil
check(P:resolve(s,1,s.band[1].target)=='Invite required' and not P.accepted[4],'reconnecting same character does not inherit a previous connection consent')
local fresh=factory(mp,format,emit);check(not next(fresh.accepted) and not next(fresh.held),'server restart cannot restore saved consent')
local npcId='fetcher_synth_bard_11_1';storage['11:performers']={version=1,entries={{refId=npcId,mpNum=80}}}
actors[80]={mpNum=80,refId=npcId,cell='A',position={x=100,y=0,z=0},isDead=false}
local target={kind='npc',mpNum=80,refId=npcId,instrument='drum'}
check(P:resolve(s,2,target)=='Ready','explicitly owned NPC can hold a stage instrument')
local unauthorized={kind='npc',mpNum=81,refId='fetcher_synth_bard_22_1',instrument='drum'}
check(P:resolve(s,2,unauthorized)=='Not your performer','simulation authority does not grant ownership over another NPC')
actors[80].isDead=true;check(P:resolve(s,2,target)=='Dead','dead NPC output is silent')
actors[80].isDead=false;actors[80].cell='B';check(P:resolve(s,2,target)=='Out of range','NPC in another cell cannot emit at the workstation')
actors[80].cell='A';P:hire(s,2);local id=spawns[#spawns]
check(id=='fetcher_synth_bard_11_2' and records[id].mwscript=='' and #storage['11:performers'].entries==2,'hire persists ownership before spawning a distinct script-free NPC record')
P:spawned({pid=1,refId=id,mpNum=82},sessions);check(not storage['11:performers'].entries[2].mpNum,'client cannot forge spawn completion')
P:spawned({refId=id,mpNum=82},sessions);check(storage['11:performers'].entries[2].mpNum==82,'native spawn completion binds stable owned actor identity')
s.band[2].target=unauthorized;P:dismiss(s,2);check(#removed==0,'dismiss cannot remove another character\'s actor')
s.band[2].target=target;P:dismiss(s,2)
check(#removed==1 and removed[1].id==80 and #storage['11:performers'].entries==1,'dismiss removes only the owned actor and its saved roster entry')
P:hire(s,2);check(spawns[#spawns]=='fetcher_synth_bard_11_3','dismiss does not reuse persistent NPC identities')
local legacy=format.new();legacy.band[1].target={kind='player',characterId=22,instrument='lute'};legacy.band[2].target=target
check(format.project(legacy).band[1].target.characterId==22,'actor routing persists stable identity without GUID or consent')
check(not format.target({kind='player',characterId=22,guid=2,instrument='lute'}) and not format.target({kind='npc',mpNum=80,refId='fargoth',instrument='lute'}),'unbounded identities and arbitrary NPC records are rejected')
print('PASS player consent, inventory readiness, expiry/epoch/leave/death/reconnect, owned NPCs and actor-route persistence; '..checks..' assertions')
