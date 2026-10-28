local checks=0;local function check(x,why) checks=checks+1;assert(x,why) end
local V={};V.__index=V;local function v(x,y,z) return setmetatable({x=x,y=y,z=z},V) end
function V.__add(a,b) return v(a.x+b.x,a.y+b.y,a.z+b.z) end
function V.__sub(a,b) return v(a.x-b.x,a.y-b.y,a.z-b.z) end
function V.__mul(a,b) return v(a.x*b,a.y*b,a.z*b) end
function V.__div(a,b) return a*(1/b) end
function V:length() return math.sqrt(self.x*self.x+self.y*self.y+self.z*self.z) end
local store={};local storage={get=function(_,k) return store[k] end,set=function(_,k,x) store[k]=x end}
local settings,calls,probes={},0,0
local synth={unitsPerMeter=70,setListening=function(p) settings=p;calls=calls+1 end,getListening=function() return {listenerPosition=v(0,0,100),decay=1,reflectionsAvailable=true} end}
local nearby={COLLISION_TYPE={World=1,Door=2,HeightMap=8},castRay=function(from,to,options)
 probes=probes+1;check(options.collisionType==11,'probes exclude actor hitboxes')
 local d=to-from;local distance=d:length();return {hit=true,hitPos=from+d*(math.min(300,distance)/distance)}
end}
local make=dofile('files/server/server-lua-packages/fetcher.procedural-synth/client/listening.lua')
local service=make(synth,nearby,{vector3=v,bitOr=function() return 11 end},storage)
check(settings.reference==250 and settings.rolloff==.45,'gentler world defaults')
check(not service:set('rolloff',0/0) and not service:set('wet',2) and not service:set('balanced',1),'invalid personal settings rejected')
check(service:set('balanced',true) and store.balanced and settings.balanced,'balanced preference saves locally')
local function audio() return {setMonitorEligible=function(self,x) self.eligible=x end,setObstruction=function(self,x) self.blocked=x end} end
local a,b=audio(),audio();local src={position=v(0,500,0),isValid=function() return true end}
local instances={[10]={audio=a,source=src},[20]={audio=b,source=src}}
local transport={each=function() end};local cell={}
for n=1,120 do local before=probes;service:frame(instances,{id=10},{},transport,n/240,cell);check(probes-before<=3,'per-frame ray budget') end
check(probes<=30,'high FPS cannot exceed the time-bounded probe budget')
check(a.eligible and not b.eligible and service.scope==10,'balanced selects only the owned Studio band')
check(a.blocked==1 and b.blocked==1,'geometry obstruction is recorded separately from musical gain')
service.selected=20;service:frame(instances,{id=10},{},transport,1,cell)
check(not a.eligible and b.eligible,'observer can select a different band without claiming it')
service.selected=nil;service:frame(instances,nil,{membership={id=20}},transport,2,cell)
check(b.eligible and service.scope==20,'accepted performers can monitor their leader without opening Studio')
service:set('room',false)
for n=1,5 do service:frame(instances,nil,{membership={id=20}},transport,3+n*.1,cell) end
check(a.blocked==0 and b.blocked==0 and not settings.room,'dry bypass clears direct obstruction and reflections')
service:reset();check(not service.scope and #service.outputs==0 and settings.enclosure==0,'disconnect clears transient band/room state')
local rays={};for n=1,14 do rays[n]={hit=true,distance=350} end
local room=service.fit(rays,70);check(room.enclosure==1 and room.width==10 and room.height==10,'closed geometry produces a measured room')
rays[5].hit=false;check(service.fit(rays,70).enclosure==0,'open sky does not create indoor reverb')
local loaded=make(synth,nearby,{vector3=v,bitOr=function() return 11 end},storage)
check(loaded.options.balanced==true and loaded.options.room==false,'listener preferences survive reconnect')
print('PASS local monitoring, selected bands, geometry/occlusion budgets, persistence; '..checks..' assertions')
