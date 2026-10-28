local V={};V.__index=V
local function v(x,y,z) return setmetatable({x=x,y=y,z=z},V) end
function V.__add(a,b) return v(a.x+b.x,a.y+b.y,a.z+b.z) end
function V.__sub(a,b) return v(a.x-b.x,a.y-b.y,a.z-b.z) end
function V.__mul(a,b) return v(a.x*b,a.y*b,a.z*b) end
local R={};R.__index=R
function R.__mul(a,b)
 if b.x then return v(a[1]*b.x+a[2]*b.y+a[3]*b.z,a[4]*b.x+a[5]*b.y+a[6]*b.z,a[7]*b.x+a[8]*b.y+a[9]*b.z) end
 local out={};for row=0,2 do for col=0,2 do local n=0;for k=0,2 do n=n+a[row*3+k+1]*b[k*3+col+1] end;out[row*3+col+1]=n end end
 return setmetatable(out,R)
end
local T={identity=setmetatable({1,0,0,0,1,0,0,0,1},R)}
-- Match utilpackage.cpp: the named rotation helpers use negative world axes.
function T.rotateX(n) local s,c=math.sin(-n),math.cos(n);return setmetatable({1,0,0,0,c,-s,0,s,c},R) end
function T.rotateY(n) local s,c=math.sin(-n),math.cos(n);return setmetatable({c,0,s,0,1,0,-s,0,c},R) end
function T.rotateZ(n) local s,c=math.sin(-n),math.cos(n);return setmetatable({c,-s,0,s,c,0,0,0,1},R) end
package.loaded['openmw.util']={vector3=v,transform=T}
local P=dofile('files/server/server-lua-packages/fetcher.procedural-synth/client/instrument-profiles.lua')
local checks=0;local function check(value,why) checks=checks+1;assert(value,why) end
local neck=P.lute.position+P.lute.rotation*v(0,20,8)
check(neck.x<0 and neck.z>P.lute.position.z,'vanilla lute neck points toward fretting hand and upward')
local luteContact=P.lute.targets.L+P.lute.rotation*v(-4,0,0)-v(0,3,0)
check(math.abs(neck.x-luteContact.x)<.2 and math.abs(neck.y-luteContact.y)<.2 and math.abs(neck.z-luteContact.z)<.2,'lute wrist leaves room for fingers to reach the measured string surface')
local strings=P.lute.position+P.lute.rotation*v(0,-12,8)
check(math.abs(strings.x-P.lute.targets.R.x)<.2 and math.abs(strings.z-P.lute.targets.R.z)<.2,'strumming hand meets the actual string/body surface')
local low=P.motion(P.keyboard,'L',48,true,1,1,1);local high=P.motion(P.keyboard,'L',60,true,1,1,1)
check(high.x>low.x and high.x<0,'keyboard hand travels with pitch on its own half')
local idle=P.motion(P.keyboard,'R',67,false,1,0,0);local strike,wrist,curl=P.motion(P.keyboard,'R',67,true,1,1,1)
check(strike.z<idle.z and wrist~=0 and curl>.08,'keyboard press changes wrist/fingers and target depth')
local fret=P.motion(P.lute,'L',55,true,1,1,1);local upper=P.motion(P.lute,'L',72,true,1,1,1)
check(upper.x>fret.x and upper.z<fret.z,'lute fretting moves along the neck rather than tapping downward')
local down=P.motion(P.lute,'R',60,true,1,0,1);local up=P.motion(P.lute,'R',60,true,1,1,1)
check(up.z>down.z,'lute alternates up/down strums')
local left=P.motion(P.drum,'L',36,true,1,0,1);local right=P.motion(P.drum,'R',36,true,1,0,1)
check(left.z<right.z and math.abs(left.z-2-(P.drum.position.z+15.4646))<.05,'drum strike leaves room for palm thickness above the measured head')
left=P.motion(P.drum,'L',36,true,1,1,1);right=P.motion(P.drum,'R',36,true,1,1,1)
check(right.z<left.z,'successive drum strikes alternate hands')
local rest=P.motion(P.drum,'R',36,false,1,1,0)
check(rest.z>right.z,'stopped drum holds a raised resting hand')
check(P.keyboard.anchor=='feet' and P.keyboard.model:find('keyboard%-stand.nif') and P.keyboard.position.z==0,'Yamaha stand stays floor anchored')
check(P.keyboard.targets.L.y==P.keyboard.position.y-18 and P.keyboard.targets.L.z==91,'hands meet the standing Yamaha keybed')
local neck=P.violin.position+P.violin.rotation*v(0,22,4.3)
local contact=P.violin.targets.L+P.violin.rotation*v(5,0,4.3)
check(math.abs(neck.x-contact.x)<.001 and math.abs(neck.z-contact.z)<.001,'violin wrist supports the neck from below while the fingers reach the strings')
local frog=P.violin.position+P.violin.rotation*v(19,0,4.75)
check(math.abs(P.violin.targets.R.y+7-frog.y)<.001 and math.abs(P.violin.targets.R.z-1-frog.z)<.001,'bow frog is held by the fingers rather than placed through the wrist')
local lf=P.handDirections(P.clarinet,'L');local rf=P.handDirections(P.clarinet,'R')
check(lf.x>0 and rf.x<0 and P.clarinet.targets.L.z>P.clarinet.targets.R.z,'clarinet hands cross its upper/lower keys from opposite sides rather than hanging down the tube')
check(math.abs(P.trumpet.position.y-3.5-(P.clarinet.position.y-.5))<.001,'wind mouthpieces share the same lip plane instead of burying the trumpet cup in the face')
local fluteDirection=P.handDirections(P.flute,'L')
check(P.flute.targets.L.y>8 and fluteDirection.x>0 and fluteDirection.y<0,'flute cross-body wrist stays in front of the face with fingers returning toward the keys')
local celloEnd=P.cello.position+P.cello.rotation*v(0,-80,0)
check(math.abs(celloEnd.z)<.001 and P.cello.anchor=='feet','standing cello endpin rests on the floor')
local travel=P.bowTravel(P.violin,.2,true)
check(travel>0 and P.bowTravel(P.violin,.2,false)==0,'bow stroke follows held notes and rests on Stop')
local a=P.motion(P.violin,'R',60,true,1,0,1,0);local b=P.motion(P.violin,'R',60,true,1,0,1,travel)
check(b.x~=a.x and b.y~=a.y,'bow hand translates along the bow axis')
local grip=P.motion(P.timpani,'L',48,true,1,0,1)
check(math.abs(grip.z-10-2-64)<.001,'timpani felt head contacts the drum head rather than the wrist')
check(P.fingerCurl(P.trumpet,'R','Bip01 R Finger1',.3,60,true)<P.fingerCurl(P.trumpet,'R','Bip01 R Finger1',.3,61,true),'trumpet finger presses track the valve combination')
for _,name in ipairs{'keyboard','lute','drum','violin','cello','trumpet','flute','clarinet','timpani'} do
 for _,side in ipairs{'L','R'} do local direction,normal=P.handDirections(P[name],side)
  check(math.abs(direction.x*normal.x+direction.y*normal.y+direction.z*normal.z)<.001,'palm frame is well-defined for '..name..' '..side)
 end
end
print('PASS mesh-aligned keyboard, fretting/strumming, alternating drum and finger/wrist profiles; '..checks..' assertions')
