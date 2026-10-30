local root='files/server/server-lua-packages/fetcher.procedural-synth/'
local V={};V.__index=V;local function v(x,y,z) return setmetatable({x=x,y=y,z=z},V) end
local checks=0;local function check(x,why) checks=checks+1;assert(x,why) end
local rays,mode=0,'clear'
local nearby={COLLISION_TYPE={World=1,Door=2,HeightMap=4}}
function nearby.castRay(a,b,options)
    rays=rays+1;check(options.collisionType==7,'stage checks exclude actors')
    if options.radius and options.radius>0 then return {hit=mode=='ceiling'} end
    if a.z>b.z then return {hit=mode~='void',hitPos=v(a.x,a.y,mode=='uneven' and a.x>0 and 30 or 0),hitNormal={z=mode=='slope' and .3 or 1}} end
    return {hit=mode=='wall'}
end
local Layout=dofile(root..'shared/band-layout.lua')
local Preflight=dofile(root..'client/stage-preflight.lua')(nearby,{vector3=v,bitOr=function() return 7 end},Layout)
local result,error
local function run(count)
    result,error=nil,nil;rays=0
    local p=Preflight.new({x=0,y=0,z=0,rz=0},count,function(r,e) result,error=r,e end)
    for n=1,40 do local before=rays;p:frame();check(rays-before<=8,'preflight never exceeds eight rays per frame');if p.done then break end end
    check(p.done,'preflight completes in bounded frames');return p
end
mode='clear';run(32);check(result and #result.heights==32 and not error and rays==224,'32-place grid has measured floors and bounded work')
local wideDone,wideGround
local wide=Preflight.new({x=0,y=0,z=0,rz=0},3,function(r) wideDone=true;wideGround=r end,true)
for n=1,8 do wide:frame() end
check(wideDone and #wideGround.heights==3 and wide.wide,'full kit uses the larger footprint and the same bounded floor checks')
run(0);check(result and #result.heights==0 and rays==0,'solo songs need no NPC collision work')
for _,case in ipairs{'void','slope','ceiling','wall','uneven'} do mode=case;run(4);check(not result and type(error)=='string','unsafe '..case..' stage is rejected') end
local Seating=dofile(root..'client/seated-pose.lua')({}, {vector3=v}, function() end,function() end)
check(not Seating.supported({}),'unknown rigs remain standing')
local bones={['bip01 pelvis']='Bip01 Pelvis'}
for _,side in ipairs{'l','r'} do for _,part in ipairs{'thigh','calf','foot'} do bones['bip01 '..side..' '..part]=true end end
check(Seating.supported(bones),'complete biped legs can be seated')
bones['bip01 r calf']=nil;check(not Seating.supported(bones),'partial rigs cannot receive half a seated pose')
print('PASS seated rig eligibility and stage floor/slope/headroom/wall checks; '..checks..' assertions')
