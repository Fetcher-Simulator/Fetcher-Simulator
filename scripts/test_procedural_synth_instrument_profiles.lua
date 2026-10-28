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
local approved=dofile('files/server/server-lua-packages/fetcher.procedural-synth/client/pose-defaults.lua')
package.loaded['scripts.multiplayer.fetcher.procedural-synth.client.pose-defaults']=approved
local Published=dofile('files/server/server-lua-packages/fetcher.procedural-synth/client/instrument-profiles.lua')
-- Keep geometry checks on the documented original mesh/reference coordinates.
-- Accepted in-game poses are checked separately below against their exports.
local P=setmetatable({}, {__index=Published});for name,p in pairs(Published.reference) do P[name]=p end
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
local tf,tn=P.handDirections(P.trumpet,'R');local sf,sn=P.handDirections(P.trumpet,'L')
check(tf.x<0 and tn.z<0 and sf.z>0 and sn.x>0,'trumpet valve fingers cross inward with palm down; left palm faces the casings with fingers wrapping upward')
local valveCenter=P.trumpet.position+v(0,17,5.8)
check(P.trumpet.targets.R.x>valveCenter.x and math.abs(P.trumpet.targets.R.y-valveCenter.y)<1.1 and P.trumpet.targets.R.z>valveCenter.z,'right wrist leaves room for fingers above the longitudinal valve row')
check(P.trumpet.targets.L.z<P.trumpet.position.z and P.trumpet.targets.L.x<0,'left wrist supports the upright valve block from its lower left')
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
do
 local data={};local storage={get=function(_,k) return data[k] end,set=function(_,k,x) data[k]=x end}
 local make=dofile('files/server/server-lua-packages/fetcher.procedural-synth/client/pose-calibration.lua')
 local calibration=make(Published,package.loaded['openmw.util'],storage)
 local original=Published.lute.targets.L.x;local base=calibration:profile('lute')
 check(calibration:profile('lute')==base and base.targets.L.x==original,'unchanged profiles reuse a cached transform set')
 check(calibration:set('lute',{'LTarget',1},original+3),'live hand target accepted')
 check(Published.lute.targets.L.x==original and calibration:profile('lute').targets.L.x==original+3,'local calibration cannot mutate source defaults')
 check(calibration:profile('lute')~=base,'a changed hand target invalidates the profile cache')
 check(not calibration:set('lute',{'propPosition',1},0/0) and not calibration:set('lute',{'LTarget',1},1/0),'nonfinite calibration rejected')
 check(not calibration:set('lute',{'bones','Bip01','position',1},1),'root/unknown bone offsets rejected')
 check(not calibration:set('lute',{'extras',1,'position',1},1),'nonexistent prop attachment rejected')
 check(calibration:set('lute',{'bones','Bip01 L Hand','rotation',3},25),'known local bone offset accepted')
 check(calibration:profile('lute').tuning.bones['bip01 l hand']~=nil,'bone offset compiles into the live profile')
 calibration:set('lute',{'LCurlScale'},2);calibration:set('lute',{'LCurlBias'},.1)
 check(math.abs(P.fingerCurl(calibration:profile('lute'),'L','Finger1',.3,60,false)-.7)<.0001,'finger tuning adjusts the animated curl')
 calibration:set('lute',{'LMotionScale'},0)
 local point=P.motion(calibration:profile('lute'),'L',90,true,1,0,1)
 check(math.abs(point.x-calibration:profile('lute').targets.L.x)<.0001,'zero note-motion multiplier holds the edited target while audio continues')
 calibration:set('lute',{'LRotation',1},0);calibration:set('lute',{'LRotation',2},0);calibration:set('lute',{'LRotation',3},90)
 local direction=select(1,P.handDirections(calibration:profile('lute'),'L'))
 local expected=T.rotateZ(math.pi/2)*select(1,P.handDirections({kind='lute',rotation=calibration:profile('lute').rotation},'L'))
 check(math.abs(direction.x-expected.x)<.0001 and math.abs(direction.y-expected.y)<.0001,'palm rotation uses the documented named-axis composition')
 calibration:save();check(not data.profiles.drum and not data.profiles.lute.propPosition,'only changed values are persisted so new defaults can reach untouched controls')
 calibration:reset('lute');check(not calibration:profile('lute').tuning.bones['bip01 l hand'],'reset clears advanced offsets')
 calibration:reload();check(calibration:profile('lute').targets.L.x==original+3,'saved local tuning survives reload')
 local text=calibration:export('lute');local exported=assert((loadstring or load)(text))()
 check(exported.format==1 and math.abs(exported.profiles.lute.LTarget[1]-original-3)<.000001 and not exported.profiles.drum,'selected export contains reusable numeric values only')
 calibration:toggle();check(calibration:profile('lute')==Published.lute,'preview toggle restores the exact default profile')
 calibration:toggle();calibration:save();local restored=make(Published,package.loaded['openmw.util'],storage)
 check(restored:profile('lute').targets.L.x==original+3,'a new calibration instance loads saved values')
 data.profiles.lute.LTarget[1]=99999;restored:reload()
 check(restored:profile('lute').targets.L.x==original,'corrupt out-of-range saved values fall back to defaults')
 local keys=restored:placedKeyboard();check(keys.targets.L.y==-18 and keys.targets.L.z==91,'placed-keyboard hand targets retain the keybed reference frame')
 local copies=0;local engine={get=function(_,key) if key=='profiles' then error('engine storage view must be copied') end;return data[key] end,
  getCopy=function(_,key) copies=copies+1;return data[key] end,set=storage.set}
 make(Published,package.loaded['openmw.util'],engine);check(copies==1,'native storage restores via getCopy rather than treating its userdata view as a Lua table')
 -- A package-37 saved export is already expressed in the original reference
 -- frame. It must replace the accepted values, never rotate them a second time.
 for name,export in pairs(approved.profiles) do
  data={format=1,enabled=true,profiles={[name]=export}}
  local migrated=make(Published,package.loaded['openmw.util'],storage)
  local p=migrated:profile(name);local expected=Published[name]
  check(math.abs(p.position.x-export.propPosition[1])<1e-7 and math.abs(p.targets.L.x-export.LTarget[1])<1e-7,'new default positions match '..name..' export')
  for _,axis in ipairs{v(1,0,0),v(0,1,0),v(0,0,1)} do
   local a,b=p.rotation*axis,expected.rotation*axis
   check(math.abs(a.x-b.x)+math.abs(a.y-b.y)+math.abs(a.z-b.z)<1e-7,'saved '..name..' prop rotation is not applied twice')
   for _,side in ipairs{'L','R'} do
    a,b=p.tuning[side..'Rotation']*axis,expected.tuning[side..'Rotation']*axis
    check(math.abs(a.x-b.x)+math.abs(a.y-b.y)+math.abs(a.z-b.z)<1e-7,'saved '..name..' palm rotation is not applied twice')
   end
  end
  local function validate(node,path)
   if type(node)=='table' then for key,item in pairs(node) do local nextPath={};for n,x in ipairs(path) do nextPath[n]=x end;nextPath[#nextPath+1]=key;validate(item,nextPath) end
   else local spec=migrated.spec(path);check(spec and node>=spec.min and node<=spec.max,'export field is valid for '..name) end
  end
  validate(export,{})
  migrated:save();check(not next(data.profiles),'accepted '..name..' export becomes the default rather than a redundant user override')
  migrated:reset(name);check(migrated:get(name,{'LTarget',1})==export.LTarget[1],'Reset restores the accepted '..name..' defaults')
 end
end
print('PASS mesh-aligned profiles, live calibration, validated persistence/export; '..checks..' assertions')
