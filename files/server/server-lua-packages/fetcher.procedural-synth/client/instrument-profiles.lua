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
-- Vanilla drum top is 15.46 units above its model origin.
M.drum={kind='drum',model='meshes/m/Misc_de_drum_01.nif',position=v(0,24,-25),rotation=T.identity,
    targets={L=v(-6,24,-3.5),R=v(6,24,-3.5)}}
local path='meshes/fetcher/orchestra/'
M.violin={kind='violin',model=path..'violin.nif',position=v(-6,14,12),rotation=T.rotateZ(-1.2)*T.rotateX(-.1),
    targets={L=v(-26.2,22.3,18.5),R=v(1.7,32.5,17)},centerNote=60,
    extras={{key='synth_bow',model=path..'bow.nif',position=v(0,0,5),rotation=T.identity,bow=true}}}
-- Wrist anchors sit behind the finger contact points, not on the strings/frog.
M.violin.targets.L=M.violin.position+M.violin.rotation*v(-5,22,0)
M.violin.targets.R=M.violin.position+M.violin.rotation*v(19,0,4.75)-v(0,5,-2)
M.cello={kind='cello',anchor='feet',model=path..'cello.nif',position=v(0,17,55),rotation=T.rotateZ(math.pi)*T.rotateX(-math.pi/2),
    targets={L=v(0,24.3,98),R=v(20,25.5,69)},centerNote=48,
    extras={{key='synth_bow',model=path..'bow.nif',position=v(0,14,8.5),rotation=T.rotateZ(math.pi),bow=true}}}
M.trumpet={kind='trumpet',model=path..'trumpet.nif',position=v(0,6,13),rotation=T.identity,
    targets={L=v(-4,18,10),R=v(4,21,17)}}
M.flute={kind='flute',model=path..'flute.nif',position=v(0,8,13),rotation=T.identity,
    targets={L=v(11,0,14),R=v(28,0,14)}}
M.clarinet={kind='clarinet',model=path..'clarinet.nif',position=v(0,8,13),rotation=T.identity,
    targets={L=v(-6,14.1,0),R=v(6,16.6,-13)}}
M.timpani={kind='timpani',anchor='feet',model=path..'timpani.nif',position=v(0,35,0),rotation=T.identity,
    targets={L=v(-10,27,82),R=v(10,27,82)},
    extras={{key='synth_mallet_l',model=path..'mallet.nif',hand='L'},{key='synth_mallet_r',model=path..'mallet.nif',hand='R'}}}
local function clamp(n,a,b) return math.max(a,math.min(b,n)) end
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
            point=point+v(fret,0,-fret*.365);curl=.35+(pressed and .12*pulse or 0)
        else
            point=point+v(0,0,pressed and ((strikes or 0)%2==0 and -1 or 1)*pulse*3 or 0)
            wrist=pressed and -.25*pulse or 0;curl=.2
        end
    elseif profile.kind=='drum' or profile.kind=='timpani' then
        local hit=pressed and ((count or 0)>1 or ((strikes or 0)%2==0)==(side=='L'))
        point=point+v(0,0,hit and -pulse*6 or 0);wrist=hit and -.18*pulse or .08;curl=profile.kind=='timpani' and .35 or .03
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
    end
    return point,wrist,curl
end
function M.handDirections(profile,side)
    if profile.kind=='lute' then
        return profile.rotation*v(side=='L' and -1 or 1,0,0),v(0,side=='L' and 1 or -1,0)
    end
    if profile.kind=='violin' then
        if side=='L' then return profile.rotation*v(1,0,.15),profile.rotation*v(-.15,0,1) end
        return v(0,1,0),v(0,0,side=='L' and 1 or -1)
    elseif profile.kind=='clarinet' then
        return v(side=='L' and 1 or -1,0,-.15),v(0,-1,0)
    elseif profile.kind=='cello' then
        return v(0,0,-1),v(0,side=='L' and 1 or -1,0)
    elseif profile.kind=='trumpet' and side=='L' then return v(0,1,0),v(0,0,1) end
    return v(0,1,0),v(0,0,-1)
end
function M.fingerCurl(profile,side,name,base,pitch,pressed)
    if not pressed then return base end
    local finger=tonumber(name:lower():match('finger(%d)')) or 0
    if profile.kind=='trumpet' and side=='R' and finger>0 then
        local valves={0,7,5,6,3,1,2,0,6,3,1,2};local bit=2^math.min(2,finger-1)
        return math.floor(valves[math.floor(pitch)%12+1]/bit)%2==1 and .65 or .2
    elseif profile.kind=='clarinet' and finger>0 then
        return (math.floor(pitch)+finger*2+(side=='L' and 0 or 3))%12<7 and .35 or .1
    elseif profile.kind=='flute' and finger>0 then
        return (math.floor(pitch)+finger*2+(side=='L' and 0 or 3))%12<7 and .55 or .18
    end
    return base
end
return M
