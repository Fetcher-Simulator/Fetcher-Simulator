-- Targets share the mesh's chest/feet anchor, in actor-facing axes.
local util=require('openmw.util');local v,T=util.vector3,util.transform
local M={}
M.keyboard={kind='keyboard',anchor='feet',model='meshes/fetcher/synth/keyboard-stand.nif',position=v(0,50,0),rotation=T.identity,
    targets={L=v(-18,32,91),R=v(18,32,91)}}
-- Vanilla lute neck runs toward model +Y; its string face is model +Z.
M.lute={kind='lute',model='meshes/m/Misc_de_lute_01.nif',position=v(6,17,-10),
    -- rotateX/Y/Z use OpenMW's clockwise angles, unlike transform.rotate(axis).
    rotation=T.rotateY(-math.pi/2-.35)*T.rotateX(math.pi/2),
    targets={L=v(-12.8,25,-3.1),R=v(17.3,25,-14.1)}}
M.lute.targets.L=M.lute.targets.L-M.lute.rotation*v(-4,0,0)+v(0,3,0)
-- Vanilla drum top is 15.46 units above its model origin.
M.drum={kind='drum',model='meshes/m/Misc_de_drum_01.nif',position=v(0,24,-25),rotation=T.identity,
    targets={L=v(-6,24,-3.5),R=v(6,24,-3.5)}}
local path='meshes/fetcher/orchestra/'
M.violin={kind='violin',model=path..'violin.nif',position=v(-6,14,12),rotation=T.rotateZ(-1.2)*T.rotateX(-.1),
    targets={L=v(-26.2,22.3,18.5),R=v(1.7,32.5,17)},centerNote=60,
    extras={{key='synth_bow',model=path..'bow.nif',position=v(0,0,5),rotation=T.identity,bow=true}}}
-- Wrist anchors sit behind the finger contact points, not on the strings/frog.
M.violin.targets.L=M.violin.position+M.violin.rotation*v(-5,22,0)
M.violin.targets.R=M.violin.position+M.violin.rotation*v(19,0,4.75)-v(0,7,-1)
M.cello={kind='cello',anchor='feet',model=path..'cello.nif',position=v(0,17,80),rotation=T.rotateZ(math.pi)*T.rotateX(-math.pi/2),
    targets={L=v(-6,27,111),R=v(19,18.75,88)},centerNote=48,
    extras={{key='synth_bow',model=path..'bow.nif',position=v(0,6,8),rotation=T.rotateZ(math.pi),bow=true}}}
M.trumpet={kind='trumpet',model=path..'trumpet.nif',position=v(0,11,13),rotation=T.identity,
    targets={L=v(-5,28,10),R=v(10,27,20.5)}}
M.flute={kind='flute',model=path..'flute.nif',position=v(0,8,13),rotation=T.identity,
    targets={L=v(6,11,15),R=v(28,1.5,14)}}
M.clarinet={kind='clarinet',model=path..'clarinet.nif',position=v(0,8,13),rotation=T.identity,
    targets={L=v(-6,14.1,0),R=v(6,16.6,-13)}}
M.timpani={kind='timpani',anchor='feet',model=path..'timpani.nif',position=v(0,35,0),rotation=T.identity,
    targets={L=v(-10,27,82),R=v(10,27,82)},
    extras={{key='synth_mallet_l',model=path..'mallet.nif',hand='L'},{key='synth_mallet_r',model=path..'mallet.nif',hand='R'}}}
local function clamp(n,a,b) return math.max(a,math.min(b,n)) end
local function copy(value)
    if type(value)~='table' then return value end
    local result={};for key,item in pairs(value) do result[key]=copy(item) end;return result
end
local function vector(p) return v(p[1] or 0,p[2] or 0,p[3] or 0) end
local function rotation(p) return T.rotateZ(math.rad(p[3] or 0))*T.rotateY(math.rad(p[2] or 0))*T.rotateX(math.rad(p[1] or 0)) end
function M.seed(p)
    local d={propPosition={p.position.x,p.position.y,p.position.z},propRotation={0,0,0},bones={},extras={}}
    for _,side in ipairs{'L','R'} do
        d[side..'Target']={p.targets[side].x,p.targets[side].y,p.targets[side].z};d[side..'Rotation']={0,0,0};d[side..'Pole']={side=='L' and -1 or 1,0,-.6}
        d[side..'CurlScale']=1;d[side..'CurlBias']=0;d[side..'MotionScale']=1
    end
    for n,e in ipairs(p.extras or {}) do d.extras[n]={position=e.position and {e.position.x,e.position.y,e.position.z} or {0,0,0},rotation={0,0,0}} end
    if p.seated then
        d.pelvisOffset={0,0,-32};d.benchPosition={0,-5,0};d.benchRotation={0,0,0};d.benchScale=1
        for _,side in ipairs{'L','R'} do
            d[side..'FootTarget']={0,27,0};d[side..'FootRotation']={0,0,0};d[side..'KneePole']={side=='L' and -.15 or .15,1,0}
        end
    end
    return d
end
function M.compile(base,d)
    local p={};for k,x in pairs(base) do p[k]=x end
    p.position=vector(d.propPosition);p.rotation=rotation(d.propRotation)*base.rotation;p.targets={L=vector(d.LTarget),R=vector(d.RTarget)}
    p.tuning={bones={}}
    if base.seated then
        p.seat={pelvis=vector(d.pelvisOffset),targets={},rotations={},poles={}}
        for _,side in ipairs{'L','R'} do
            p.seat.targets[side]=vector(d[side..'FootTarget']);p.seat.rotations[side]=rotation(d[side..'FootRotation']);p.seat.poles[side]=vector(d[side..'KneePole'])
        end
        p.bench={position=vector(d.benchPosition),rotation=rotation(d.benchRotation),scale=d.benchScale}
    end
    for _,side in ipairs{'L','R'} do
        p.tuning[side..'Rotation']=rotation(d[side..'Rotation']);p.tuning[side..'Pole']=vector(d[side..'Pole'])
        for _,key in ipairs{'CurlScale','CurlBias','MotionScale'} do p.tuning[side..key]=d[side..key] end
    end
    for name,b in pairs(d.bones) do
        local pos=b.position or {};local rot=b.rotation or {};local nonzero=false
        for n=1,3 do nonzero=nonzero or (pos[n] or 0)~=0 or (rot[n] or 0)~=0 end
        if nonzero then p.tuning.bones[name:lower()]={position=vector(pos),rotation=rotation(rot)} end
    end
    p.extras={}
    for n,extra in ipairs(base.extras or {}) do
        local e={};for k,x in pairs(extra) do e[k]=x end
        e.position=(extra.position or extra.hand) and vector(d.extras[n].position) or extra.position
        e.rotation=rotation(d.extras[n].rotation)*(extra.rotation or T.identity);p.extras[n]=e
    end
    return p
end
function M.bowTravel(profile,age,pressed)
    return (profile.kind=='violin' or profile.kind=='cello') and pressed and math.sin(age*math.pi*1.6)*5 or 0
end
function M.motion(profile,side,pitch,pressed,pulse,strikes,count,bowTravel)
    local point=profile.targets[side];local wrist,curl=0,.05
    if profile.kind=='keyboard' then
        local shift=clamp((pitch-(side=='L' and 53 or 67))*.9,-16,16)
        point=point+v(shift,0,pressed and -pulse*1.2 or 0)
        wrist=pressed and -.12*pulse or 0;curl=.08+(pressed and .18*pulse or 0)
    elseif profile.kind=='lute' then
        if side=='L' then
            local fret=clamp((pitch-55)*.27,-5,6)
            point=point+v(fret,0,-fret*.365);curl=.25+(pressed and .08*pulse or 0)
        else
            point=point+v(0,0,pressed and ((strikes or 0)%2==0 and -1 or 1)*pulse*3 or 0)
            wrist=pressed and -.25*pulse or 0;curl=.2
        end
    elseif profile.kind=='drum' or profile.kind=='timpani' then
        local hit=pressed and ((count or 0)>1 or ((strikes or 0)%2==0)==(side=='L'))
        point=point+v(0,0,hit and -pulse*(profile.kind=='drum' and 4 or 6) or 0);wrist=hit and -.18*pulse or .08;curl=profile.kind=='timpani' and .35 or .03
    elseif profile.kind=='violin' or profile.kind=='cello' then
        if side=='L' then
            local fret=clamp((pitch-profile.centerNote)*(profile.kind=='cello' and .45 or .25),-5,7)
            point=point-profile.rotation*v(0,fret,0);curl=(profile.kind=='violin' and .22 or .42)+(pressed and pulse*.05 or 0)
        else
            local axis=profile.rotation*profile.extras[1].rotation*v(1,0,0)
            point=point+axis*(bowTravel or 0);curl=.35
        end
    elseif profile.kind=='trumpet' or profile.kind=='flute' or profile.kind=='clarinet' then
        curl=pressed and .32 or .2
        if profile.kind=='trumpet' and side=='R' then curl=.12 end
    end
    if profile.tuning then point=profile.targets[side]+(point-profile.targets[side])*(profile.tuning[side..'MotionScale'] or 1) end
    return point,wrist,curl
end
local function handDirections(profile,side)
    if profile.kind=='lute' then
        return profile.rotation*v(side=='L' and -1 or 1,0,0),v(0,-1,0)
    end
    if profile.kind=='violin' then
        if side=='L' then return profile.rotation*v(1,0,.15),profile.rotation*v(-.15,0,1) end
        return v(0,1,0),v(0,0,side=='L' and 1 or -1)
    elseif profile.kind=='clarinet' then
        return v(side=='L' and 1 or -1,0,-.15),v(0,-1,0)
    elseif profile.kind=='flute' and side=='L' then
        return v(1,-.3,0),v(0,0,-1)
    elseif profile.kind=='cello' then
        if side=='L' then return v(1,0,-.15),v(0,-1,0) end
        return v(0,1,0),v(0,0,-1)
    elseif profile.kind=='trumpet' then
        if side=='L' then return v(0,0,1),v(1,0,0) end
        return v(-1,0,0),v(0,0,-1)
    end
    return v(0,1,0),v(0,0,-1)
end
function M.handDirections(profile,side)
    local forward,normal=handDirections(profile,side)
    local rotation=profile.tuning and profile.tuning[side..'Rotation']
    return rotation and rotation*forward or forward,rotation and rotation*normal or normal
end
local function fingerCurl(profile,side,name,base,pitch,pressed)
    if not pressed then return base end
    local finger=tonumber(name:lower():match('finger(%d)')) or 0
    if profile.kind=='trumpet' and side=='R' and finger>0 then
        local valves={0,7,5,6,3,1,2,0,6,3,1,2};local bit=2^math.min(2,finger-1)
        return math.floor(valves[math.floor(pitch)%12+1]/bit)%2==1 and .32 or .12
    elseif profile.kind=='clarinet' and finger>0 then
        return (math.floor(pitch)+finger*2+(side=='L' and 0 or 3))%12<7 and .35 or .1
    elseif profile.kind=='flute' and finger>0 then
        return (math.floor(pitch)+finger*2+(side=='L' and 0 or 3))%12<7 and .35 or .1
    end
    return base
end
function M.fingerCurl(profile,side,name,base,pitch,pressed)
    local amount=fingerCurl(profile,side,name,base,pitch,pressed)
    if profile.tuning then amount=amount*(profile.tuning[side..'CurlScale'] or 1)+(profile.tuning[side..'CurlBias'] or 0) end
    return clamp(amount,0,1.5)
end
local approved=require('scripts.multiplayer.fetcher.procedural-synth.client.pose-defaults')
M.calibrationVersion=approved.version;M.referenceVersion=approved.referenceVersion;M.reference={};M.defaults={}
local function merge(target,source)
    for key,item in pairs(source) do
        if type(item)=='table' and type(target[key])=='table' then merge(target[key],item) else target[key]=copy(item) end
    end
end
for _,name in ipairs{'keyboard','lute','drum','violin','cello','trumpet','flute','clarinet','timpani'} do
    M.reference[name]=M[name];local d=M.seed(M[name])
    -- The credited wooden stick drops five units from grip to tip. Leave room
    -- above the new timpani head for its six-unit downstroke.
    if name=='timpani' then d.LTarget[3]=91;d.RTarget[3]=91 end
    merge(d,approved.profiles[name] or {})
    M.defaults[name]=d;M[name]=M.compile(M.reference[name],d)
end
-- Seated profiles have independent tuner values; standing exports stay intact.
for _,kind in ipairs{'keyboard','cello'} do
    local name=kind..'_seated';local base={};for k,x in pairs(M[kind]) do base[k]=x end
    base.seated=true;base.model=kind=='keyboard' and path..'keyboard-seated.nif' or base.model
    base.position=kind=='keyboard' and v(0,50,0) or v(0,20,62.4)
    if kind=='cello' then base.rotation=base.rotation*T.scale(.78,.78,.78) end
    base.targets=kind=='keyboard' and {L=v(-18,32,67),R=v(18,32,67)} or {L=v(-7,30,88),R=v(18,29,70)}
    M.reference[name]=base;local d=M.seed(base)
    for _,side in ipairs{'L','R'} do
        for _,key in ipairs{'Rotation','Pole','CurlScale','CurlBias','MotionScale'} do d[side..key]=copy(M.defaults[kind][side..key]) end
    end
    merge(d,approved.profiles[name] or {})
    M.defaults[name]=d;M[name]=M.compile(base,d)
end
return M
