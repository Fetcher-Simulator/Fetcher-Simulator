-- Presentation only: two-link arm IK, evaluated locally from the shared notes.
local anim=require('openmw.animation')
local util=require('openmw.util')
local types=require('openmw.types')
local Profiles=require('scripts.multiplayer.fetcher.procedural-synth.client.instrument-profiles')
local M={}
local v,T=util.vector3,util.transform
local zero=v(0,0,0)
local function clamp(n,a,b) return math.max(a,math.min(b,n)) end
local function between(a,b)
    local na,nb=a:length(),b:length()
    if na<0.0001 or nb<0.0001 then return T.identity end
    a,b=a/na,b/nb
    local axis=a:cross(b)
    if axis:length()<0.0001 then
        if a:dot(b)>0 then return T.identity end
        axis=a:cross(v(0,0,1))
        if axis:length()<0.0001 then axis=a:cross(v(0,1,0)) end
    end
    return T.rotate(math.acos(clamp(a:dot(b),-1,1)),axis/axis:length())
end
-- Exported for deterministic geometric tests without an engine scene graph.
function M.elbow(shoulder,target,a,b,pole)
    local delta=target-shoulder
    local distance=delta:length()
    if distance<0.0001 then return nil end
    local direction=delta/distance
    local reach=clamp(distance,math.abs(a-b)+0.01,(a+b)*0.98)
    local perpendicular=pole-direction*pole:dot(direction)
    if perpendicular:length()<0.0001 then perpendicular=direction:cross(v(1,0,0)) end
    if perpendicular:length()<0.0001 then perpendicular=direction:cross(v(0,1,0)) end
    perpendicular=perpendicular/perpendicular:length()
    local along=(a*a-b*b+reach*reach)/(2*reach)
    local height=math.sqrt(math.max(0,a*a-along*along))
    return shoulder+direction*along+perpendicular*height,shoulder+direction*reach
end
function M.new(actor)
    local p={actor=actor,bones={},amount=0,lastNotes='',strike=0,missing=false,applied=0,targets={},error=0,appliedBones={},fingers=0}
    if anim.boneTransformsVersion~=1 then p.missing=true;return p end
    if not anim.hasAnimation(actor) then return nil end
    for _,name in ipairs(anim.getBoneNames(actor)) do p.bones[name:lower()]=name end
    function p:clear()
        for name in pairs(self.appliedBones) do anim.clearBoneTransform(self.actor,name) end
        self.appliedBones={};self.applied=0;self.fingers=0
    end
    function p:frame(dt,source,notes,now,strikes,profile)
        self.error=0;self.fingers=0
        local record=not profile and source.recordId=='fetcher_procedural_keyboard_v1' and types.Miscellaneous.record(source) or (not profile and types.Activator.record(source))
        local staged=record and record.model:lower():gsub('\\','/'):find('keyboard%-stand%.nif$')~=nil
        local halfWidth,spacing,spread,front,height=8,0.65,5,-2,5
        if staged then halfWidth,spacing,spread,front,height=14,1.2,18,-18,91 end
        if staged then profile={kind='keyboard',targets={L=v(-18,-18,91),R=v(18,-18,91)}} end
        local rootName=self.bones['bip01']
        local root=rootName and anim.getBoneTransform(self.actor,rootName,true,true)
        if not root then return end
        local toModel=root:inverse()
        local signature,low,high={}, {}, {}
        for identity in pairs(notes) do
            local note=identity%128
            signature[#signature+1]=note
            local group=note<60 and low or high;group[#group+1]=note
        end
        table.sort(signature);signature=table.concat(signature,',')
        if signature~=self.lastNotes or strikes~=self.strikes then
            self.strike=now;self.lastNotes=signature;self.strikes=strikes
        end
        self.amount=math.min(1,self.amount+math.min(dt,0.1)*4)
        local pulse=math.max(0,1-(now-self.strike)/0.13)
        self.bowTravel=profile and Profiles.bowTravel(profile,now-self.strike,#signature>0) or 0
        for _,side in ipairs{'L','R'} do
            local sign=side=='L' and -1 or 1
            local upper=self.bones[('Bip01 '..side..' UpperArm'):lower()]
            local fore=self.bones[('Bip01 '..side..' Forearm'):lower()]
            local hand=self.bones[('Bip01 '..side..' Hand'):lower()]
            if upper and fore and hand then
                local u=anim.getBoneTransform(self.actor,upper,true,true)
                local f=anim.getBoneTransform(self.actor,fore,true,true)
                local h=anim.getBoneTransform(self.actor,hand,true,true)
                if u and f and h then
                    -- Solve before race/model scaling. Rotating world-space arm
                    -- lengths is inaccurate for actors with nonuniform scale.
                    u,f,h=toModel*u,toModel*f,toModel*h
                    local s,e,w=u*zero,f*zero,h*zero
                    local current=anim.getBoneTransform(self.actor,hand,true)
                    if current and self.targets[side] then self.error=math.max(self.error,(current*zero-self.targets[side]):length()) end
                    local group=side=='L' and low or high
                    if profile and profile.kind~='keyboard' then group={} for _,n in ipairs(low) do group[#group+1]=n end;for _,n in ipairs(high) do group[#group+1]=n end end
                    local pitch=side=='L' and 53 or 67
                    if #group>0 then pitch=0;for _,n in ipairs(group) do pitch=pitch+n end;pitch=pitch/#group end
                    -- Configurable keyboard-space targets: this small model has
                    -- a narrow key bed. No actor teleport or root motion is used.
                    local x=sign*halfWidth+clamp((pitch-(side=='L' and 53 or 67))*spacing,-spread,spread)
                    local point,wrist,curl
                    if profile then
                        point,wrist,curl=Profiles.motion(profile,side,pitch,#group>0,pulse,strikes,#low+#high,self.bowTravel)
                    else point,wrist,curl=v(x,front,height+(#group>0 and -pulse or 0)),pulse*.08,0 end
                    local target=toModel*(source.position+source.rotation*point)
                    local a,b=(e-s):length(),(w-e):length()
                    if a>0.1 and b>0.1 then
                        local pole=toModel*(root*s+self.actor.rotation*v(sign,0,-0.6))-s
                        local elbow,tip=M.elbow(s,target,a,b,pole)
                        if elbow then
                            self.targets[side]=root*tip
                            local inverseUpper=u:inverse()
                            local upperRotation=between(inverseUpper*e,inverseUpper*elbow)
                            local adjusted=u*upperRotation*inverseUpper
                            local inverseFore=(adjusted*f):inverse()
                            local foreRotation=between(inverseFore*(adjusted*w),inverseFore*tip)
                            anim.setBoneTransform(self.actor,upper,{rotation=upperRotation,weight=self.amount})
                            anim.setBoneTransform(self.actor,fore,{rotation=foreRotation,weight=self.amount})
                            local handRotation=T.identity
                            if profile then
                                local finger=self.bones[('Bip01 '..side..' Finger1'):lower()]
                                local thumb=self.bones[('Bip01 '..side..' Finger0'):lower()]
                                local ft=finger and anim.getBoneTransform(self.actor,finger,true,true)
                                local tt=thumb and anim.getBoneTransform(self.actor,thumb,true,true)
                                if ft and tt then
                                    local inverseHand=h:inverse()
                                    local forward=inverseHand*(toModel*ft*zero)
                                    local thumbDirection=inverseHand*(toModel*tt*zero)
                                    local normal=forward:cross(thumbDirection)*(side=='L' and 1 or -1)
                                    local posedFore=adjusted*f
                                    local posedHand=posedFore*foreRotation*posedFore:inverse()*adjusted*h
                                    local inversePosed=posedHand:inverse()
                                    local desiredForward,desiredNormal=Profiles.handDirections(profile,side)
                                    local origin=toModel*source.position
                                    local direction=toModel*(source.position+source.rotation*desiredForward)-origin
                                    local palm=toModel*(source.position+source.rotation*desiredNormal)-origin
                                    local localForward=inversePosed*(tip+direction)
                                    local localNormal=inversePosed*(tip+palm)
                                    if localForward:length()>.001 and normal:length()>.001 then
                                        localForward=localForward/localForward:length()
                                        local align=between(forward,localForward)
                                        normal=align*normal;normal=normal-localForward*normal:dot(localForward)
                                        localNormal=localNormal-localForward*localNormal:dot(localForward)
                                        handRotation=between(normal,localNormal)*align
                                    end
                                end
                            end
                            anim.setBoneTransform(self.actor,hand,{rotation=handRotation*T.rotateY(wrist),weight=self.amount})
                            self.appliedBones[upper]=true;self.appliedBones[fore]=true;self.appliedBones[hand]=true
                            if profile then
                                local stem=('bip01 '..side..' finger'):lower()
                                for lower,name in pairs(self.bones) do if lower:sub(1,#stem)==stem then
                                    local amount=Profiles.fingerCurl(profile,side,name,curl,pitch,#group>0)
                                    anim.setBoneTransform(self.actor,name,{rotation=T.rotateZ((side=='L' and -1 or 1)*amount),weight=self.amount})
                                    self.appliedBones[name]=true;self.fingers=self.fingers+1
                                end end
                            end
                            self.applied=6
                        end
                    end
                end
            end
        end
    end
    return p
end
return M
