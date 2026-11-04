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
local stringRotation=T.rotateY(-math.pi/2-.35)*T.rotateX(math.pi/2)
M.guitar={kind='guitar',model=path..'guitar.nif',position=v(8,18,-12),rotation=stringRotation,centerNote=55,
    targets={L=v(-21,23,-1),R=v(12,23,-14)}}
M.bass={kind='bass',model=path..'bass.nif',position=v(10,18,-13),rotation=stringRotation,centerNote=40,
    targets={L=v(-27,24,0),R=v(12,23,-15)}}
-- These untuned string defaults originally placed the wrist on the string
-- contact point. Leave finger-length clearance so curled fingertips meet it.
for _,p in ipairs{M.guitar,M.bass} do
    p.targets.L=p.targets.L-p.rotation*v(-6,0,0)-v(0,6,0)
    p.targets.R=p.targets.R-p.rotation*v(6,0,0)
end
-- Contact points measured on the shipped NIF, favoring reachable inner rims.
local kitContacts={
    [37]=v(-38,-17,66),[38]=v(-38,-17,66),[39]=v(-38,-17,66),[40]=v(-38,-17,66),
    [41]=v(35,-16,63),[43]=v(35,-16,63),[45]=v(10,0,82),[47]=v(10,0,82),
    [48]=v(-14,0,83),[50]=v(-14,0,83),[42]=v(-60,-5,84),[46]=v(-60,-5,84),
    [49]=v(-40,0,99),[55]=v(-40,0,99),[57]=v(-40,0,99),
    [51]=v(38,-10,89),[53]=v(38,-10,89),[59]=v(38,-10,89),
}
local stickTip=v(0,20,-5)
local function kitGrip(profile,side,pitch)
    local contact=profile.position+profile.rotation*(kitContacts[math.floor(pitch)] or kitContacts[38])
    local yaw=math.atan2(contact.x-(side=='L' and -10 or 10),math.max(8,contact.y))
    local rotation=T.rotateZ(yaw)
    return contact-rotation*stickTip,rotation,contact
end
M.kitGrip=kitGrip
M.drumkit={kind='drumkit',anchor='feet',seated=true,model=path..'drumkit.nif',position=v(24,38,0),rotation=T.scale(.75,.75,.75),
    targets={L=v(-57,25,90),R=v(-25,24,70)},musicStand=v(116,95,0),
    extras={{key='synth_mallet_l',model=path..'mallet.nif',hand='L',rotation=T.identity},
        {key='synth_mallet_r',model=path..'mallet.nif',hand='R',rotation=T.identity}}}
M.drumkit.targets.L=kitGrip(M.drumkit,'L',42);M.drumkit.targets.R=kitGrip(M.drumkit,'R',38)
-- Canonical neck/string axes and 85-unit scale match the electric grip.
for _,name in ipairs{'acoustic','classical'} do
    local profile={};for k,value in pairs(M.guitar) do profile[k]=value end
    profile.model=path..name..'.nif';profile.targets={L=M.guitar.targets.L+v(0,1,0),R=M.guitar.targets.R+v(0,4,0)+M.guitar.rotation*v(0,8,0)};M[name]=profile
end
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
function M.motion(profile,side,pitch,pressed,pulse,strikes,count,bowTravel,gesture)
    local point=profile.targets[side];local wrist,curl=0,.05;local stickRotation
    if profile.kind=='keyboard' then
        local shift=clamp((pitch-(side=='L' and 53 or 67))*.9,-16,16)
        point=point+v(shift,0,pressed and -pulse*1.2 or 0)
        wrist=pressed and -.12*pulse or 0;curl=.08+(pressed and .18*pulse or 0)
    elseif profile.kind=='lute' or profile.kind=='guitar' or profile.kind=='bass' then
        if side=='L' then
            local fret=clamp((pitch-(profile.centerNote or 55))*(profile.kind=='bass' and .5 or .27),-5,6)
            point=point+v(fret,0,-fret*.365);curl=.25+(pressed and .08*pulse or 0)
        else
            point=point+v(0,0,pressed and ((strikes or 0)%2==0 and -1 or 1)*pulse*3 or 0)
            wrist=pressed and -.25*pulse or 0;curl=.2
        end
    elseif profile.kind=='drumkit' then
        local grip,rotation=kitGrip(profile,side,pitch)
        local reference=kitGrip(profile,side,side=='L' and 42 or 38)
        local hit=pressed and pulse or 0
        point=profile.targets[side]+grip-reference+v(0,0,4*(1-hit))
        wrist=0;curl=.35;stickRotation=rotation*T.rotateX(-.35*(1-hit))
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
    if gesture and (profile.kind=='guitar' or profile.kind=='bass') then
        if side=='L' then
            -- Calibrated wrist anchor corresponds to fret five. Physical fret
            -- spacing contracts toward the body rather than tracking mean pitch.
            local length=profile.kind=='bass' and 72 or 60
            local along=length*(2^(-gesture.position/12)-2^(-5/12))
            point=profile.targets.L+profile.rotation*v(0,along,0);wrist=0;curl=.22
        else
            local excursion=gesture.strum and 3 or 1.1
            local across=gesture.pickedString>0 and (gesture.pickedString-(gesture.stringCount+1)/2)*.55 or 0
            point=profile.targets.R+profile.rotation*v(across,0,0)+v(0,0,(gesture.direction==1 and 1 or -1)*gesture.pulse*excursion)
            wrist=-.20*gesture.pulse;curl=.2
        end
    end
    if profile.tuning then point=profile.targets[side]+(point-profile.targets[side])*(profile.tuning[side..'MotionScale'] or 1) end
    return point,wrist,curl,stickRotation
end
local function handDirections(profile,side)
    if profile.kind=='lute' or profile.kind=='guitar' or profile.kind=='bass' then
        return profile.rotation*v(side=='L' and -1 or 1,0,0),v(0,side=='L' and profile.kind~='lute' and 1 or -1,0)
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
function M.handDirections(profile,side,stickRotation)
    local forward,normal=handDirections(profile,side)
    if profile.kind=='drumkit' and stickRotation then forward=stickRotation*v(0,1,0) end
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
function M.fingerCurl(profile,side,name,base,pitch,pressed,gesture)
    local amount=fingerCurl(profile,side,name,base,pitch,pressed)
    if gesture and (profile.kind=='guitar' or profile.kind=='bass') then
        local finger=tonumber(name:lower():match('finger(%d)')) or 0
        if side=='L' and finger>0 then amount=gesture.frets[finger] and .34 or .16
        elseif side=='R' and profile.kind=='bass' and finger>0 then
            amount=.12+(finger==gesture.pickFinger and .24*gesture.pulse or 0)
        end
    end
    if profile.tuning then amount=amount*(profile.tuning[side..'CurlScale'] or 1)+(profile.tuning[side..'CurlBias'] or 0) end
    return clamp(amount,0,1.5)
end
function M.fingerSpread(profile,side,name,gesture)
    if not gesture or side~='L' then return 0 end
    local finger=tonumber(name:lower():match('finger(%d)')) or 0
    local strings=gesture.frets[finger];if not strings then return 0 end
    local mean=0;for _,string in ipairs(strings) do mean=mean+string end;mean=mean/#strings
    return clamp((mean-(gesture.stringCount+1)/2)*.045,-.18,.18)
end
local approved=require('scripts.multiplayer.fetcher.procedural-synth.client.pose-defaults')
M.calibrationVersion=approved.version;M.referenceVersion=approved.referenceVersion;M.reference={};M.defaults={}
local function merge(target,source)
    for key,item in pairs(source) do
        if type(item)=='table' and type(target[key])=='table' then merge(target[key],item) else target[key]=copy(item) end
    end
end
for _,name in ipairs{'keyboard','lute','drum','violin','cello','trumpet','flute','clarinet','timpani','guitar','bass','drumkit','acoustic','classical'} do
    M.reference[name]=M[name];local d=M.seed(M[name])
    -- The credited wooden stick drops five units from grip to tip. Leave room
    -- above the new timpani head for its six-unit downstroke.
    if name=='timpani' then d.LTarget[3]=91;d.RTarget[3]=91 end
    if name=='drumkit' then d.LFootTarget={-22,22,0};d.RFootTarget={17,2,0};d.benchScale=.9 end
    merge(d,approved.profiles[name] or ((name=='acoustic' or name=='classical') and approved.profiles.guitar) or {})
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
    merge(d,approved.profiles[name] or ((name=='acoustic' or name=='classical') and approved.profiles.guitar) or {})
    M.defaults[name]=d;M[name]=M.compile(base,d)
end
return M
