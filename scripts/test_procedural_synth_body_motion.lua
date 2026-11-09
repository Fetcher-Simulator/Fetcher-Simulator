local root='files/server/server-lua-packages/fetcher.procedural-synth/'
local Body=dofile(root..'client/performance-body.lua')
local Sequence=dofile(root..'shared/sequence.lua')
local schema=dofile(root..'shared/schema.lua');local F=dofile(root..'shared/project.lua')(schema)
local checks=0;local function check(ok,why) checks=checks+1;assert(ok,why) end
local function close(a,b) return math.abs(a-b)<.002 end
local state,seen=nil,{}
for frame=1,480 do
 local beat=frame/30;local motion;motion,state=Body.frame(state,1/60,{playing=true,beat=beat},'lute',false,false)
 check(math.abs(motion.roll)<=math.rad(1.4) and math.abs(motion.nod)<=math.rad(1.5) and motion.foot<=math.rad(7),'gesture limits protect grips and footing')
 seen.roll=seen.roll or math.abs(motion.roll)>.004;seen.nod=seen.nod or math.abs(motion.nod)>.01;seen.foot=seen.foot or motion.foot>.05
end
check(seen.roll and seen.nod and seen.foot,'all three standing gestures occur')
local a=Body.frame({level=1,yaw=0},.02,{playing=true,beat=7.25},'violin',false,false)
local b=Body.frame({level=1,yaw=0},.02,{playing=true,beat=7.25},'violin',false,false)
check(a.nod==b.nod and a.foot==b.foot and a.roll==b.roll,'equal score positions produce equal gestures for listeners')
local slow=Body.frame({level=1,yaw=0},.02,{playing=true,beat=.5,rate=2},'violin',false,false)
local fast=Body.frame({level=1,yaw=0},.02,{playing=true,beat=1,rate=4},'violin',false,false)
check(fast.foot>0 and fast.foot<slow.foot*.3 and math.abs(fast.nod)<math.abs(slow.nod)*.3,'fast songs accent half-time at reduced amplitude')
local right=Body.drumReach({L={note=38,pulse=0},R={note=51,pulse=1}})
local left=Body.drumReach({L={note=42,pulse=1},R={note=38,pulse=0}})
check(right>0 and left<0 and Body.drumReach(nil)==0,'drummer turns toward the reached side, snare centered')
local drum=Body.frame(nil,.1,{playing=true,beat=.5},'drumkit',true,false,right)
check(drum.yaw<0 and drum.roll==0 and drum.foot==0 and drum.nod~=0,'seated drummer turns toward right-side reach, retaining pedals and stable pelvis')
for n=1,180 do a,state=Body.frame(state,1/60,{playing=false,beat=0},'lute',false,false) end
check(a.level<1e-8 and math.abs(a.roll)+math.abs(a.nod)+math.abs(a.foot)<1e-6,'stop settles all body overrides below the bone-clear threshold')
local moving=Body.frame({level=1,yaw=.1},.1,{playing=true,beat=.5},'lute',false,true)
check(moving.level<1,'walking fades presentation without changing playback')
for _,ending in ipairs{0,.5,1} do local hands=Body.conduct(4.5,2,ending)
 for _,side in ipairs{'L','R'} do check(math.abs(hands[side][1])<32 and hands[side][2]<=30 and hands[side][3]>=84 and hands[side][3]<125,'conductor cues stay in reachable upper-body space') end
end
check(Body.conduct(2,1,1).R[3]==84,'stop cue lowers hands')
local function pack(n) return string.char(n%256,math.floor(n/256)%256,math.floor(n/65536)%256,math.floor(n/16777216)%256) end
local function point(f,b,t) return pack(f)..pack(b*960)..pack(t) end
local bytes=point(0,4,500000)..point(48000,6,1000000)
local ok,map=Sequence.validatePulseMap(bytes,96000);check(ok and #map==2,'bounded tempo map decodes')
check(close(Sequence.pulseAt(map,.5),5) and close(Sequence.pulseAt(map,1.5),6.5),'presentation follows tempo changes and cropped musical origin')
for _,bad in ipairs{'',bytes:sub(2),point(1,0,500000),point(0,0,0),point(0,0,16777216),point(0,0,500000)..point(0,1,500000),point(0,4,500000)..point(4,3,500000),string.rep(point(0,0,500000),1026)} do
 check(not Sequence.validatePulseMap(bad,96000),'malformed/oversized/nonmonotonic pulse maps rejected')
end
check(Sequence.validatePulseMap(nil,96000),'old scores remain supported')
local p=F.new();p.tracks[1]=F.newTrack('piano','Pulse',16);p.tracks[1].notes={{n=60,s=0,l=1,v=1}};p.bpm=150
local plan=Sequence.compile(p,nil,nil,'clips',F);ok,map=Sequence.validatePulseMap(plan.pulseMap,plan.length)
check(ok and close(Sequence.pulseAt(map,.4),1),'clip pulse uses authored BPM')
p.timeline={beats=16,tempos={{beat=0,tempo=500000},{beat=5,tempo=1000000}}};p.loopRange={first=2,last=3,enabled=true}
plan=Sequence.compile(p,nil,nil,'song',F);ok,map=Sequence.validatePulseMap(plan.pulseMap,plan.length)
check(ok and map[1].beat==4 and close(Sequence.pulseAt(map,1.5),6),'crop offset retains phase and subsequent MIDI tempos')
check(F.project(p),'existing project remains valid')
p.conductor=true;check(F.project(p).conductor==true,'conductor preference persists')
p.conductor=1;check(not F.project(p),'conductor flag has strict type')
do
 -- Exercise real conductor lifecycle with a small rig and an instrument-solver
 -- spy. Walking/Stop must release overrides without touching musical state.
 local V={};V.__index=V;local function v(x,y,z) return setmetatable({x=x,y=y,z=z},V) end
 function V.__add(a,b) return v(a.x+b.x,a.y+b.y,a.z+b.z) end
 function V.__sub(a,b) return v(a.x-b.x,a.y-b.y,a.z-b.z) end
 function V.__mul(a,b) return v(a.x*b,a.y*b,a.z*b) end
 function V:length() return math.sqrt(self.x*self.x+self.y*self.y+self.z*self.z) end
 local prefix='scripts.multiplayer.fetcher.procedural-synth.client.';local frames,clears,yaw=0,0,nil
 local actor={position=v(0,0,0),rotation={getAnglesXZ=function() return 1.1,.7 end}}
 local pose={bones={}}
 for _,side in ipairs{'L','R'} do for _,part in ipairs{'UpperArm','Forearm','Hand'} do pose.bones[('bip01 '..side..' '..part):lower()]=side..part end end
 function pose:clear() clears=clears+1 end
 function pose:frame(_,source,_,_,_,profile) frames=frames+1;check(source.position.z==100,'conductor fitted to actual shoulder height')
  check(profile.targets.R:length()<46,'rig-fitted conductor hand remains within arm reach');check(yaw==.7,'look pitch never tilts conductor space')
 end
 package.loaded[prefix..'performance']={new=function() return pose end}
 package.loaded[prefix..'performance-body']=Body
 package.loaded['openmw.util']={vector3=v,transform={rotateZ=function(value) yaw=value;return {} end}}
 package.loaded['openmw.animation']={getBoneTransform=function(_,name)
  local side=name:sub(1,1)=='L' and -1 or 1
  local point=name:find('UpperArm') and v(side*12,0,100) or name:find('Forearm') and v(side*20,12,95) or v(side*22,27,90)
  return setmetatable({},{__mul=function() return point end})
 end}
 local conductor=dofile(root..'client/conductor.lua').new(actor);local clock={playing=true,beat=1.5,playId=1}
 for n=1,3 do conductor:frame(.1,clock,n*.1) end
 check(frames==1,'standing still enables conductor after movement debounce')
 local began=conductor.began;clock.playId=2;conductor:frame(.01,clock,.31)
 check(conductor.began==began,'seamless live score replacement does not repeat the entry cue')
 actor.position=v(10,0,0);check(not conductor:frame(.1,clock,.4) and clears>0 and clock.playing,'walking releases gesture without stopping the song')
 for n=5,7 do conductor:frame(.1,clock,n*.1) end
 check(frames>1,'gestures resume after standing still')
 clock.playing=false;conductor:frame(.1,clock,.8);local before=clears
 check(not conductor:frame(.1,clock,1.3) and clears>before,'Stop completes cue and clears all pose overrides')
end
print('PASS beat-synced body motion, reach direction, bounded conductor cues and pulse maps; '..checks..' assertions')
