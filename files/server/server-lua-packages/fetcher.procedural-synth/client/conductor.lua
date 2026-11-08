-- Reuses the instrument IK solver: no actor movement, camera lock or gameplay AI.
local util=require('openmw.util')
local anim=require('openmw.animation')
local Performance=require('scripts.multiplayer.fetcher.procedural-synth.client.performance')
local Body=require('scripts.multiplayer.fetcher.procedural-synth.client.performance-body')
local M={};local v=util.vector3
function M.new(actor)
    local pose=Performance.new(actor);if not pose or pose.missing then return end
    local c={actor=actor,pose=pose,movement={}}
    function c:clear() pcall(self.pose.clear,self.pose);self.began=nil;self.ending=nil end
    function c:frame(dt,clock,now)
        local moving=Body.moving(self.movement,self.actor.position,dt)
        if moving then self:clear();return false end
        if clock and clock.playing then
            if not self.began or self.ending then self.began=now end
            self.playId=clock.playId;self.ending=nil
        elseif self.began then self.ending=self.ending or now
        else return false end
        local ending=self.ending and (now-self.ending)/.4 or 0
        if ending>=1 then self:clear();return false end
        -- Fit to the actual rig, including beast/custom bodies. Player rotation
        -- includes look pitch; presentation uses upright actor yaw only.
        local shoulders={};local reach=0
        for _,side in ipairs{'L','R'} do local positions={}
            for _,part in ipairs{'UpperArm','Forearm','Hand'} do
                local name=self.pose.bones[('bip01 '..side..' '..part):lower()]
                local bone=name and anim.getBoneTransform(self.actor,name,true,true)
                if not bone then self:clear();return false end
                positions[#positions+1]=bone*v(0,0,0)
            end
            shoulders[#shoulders+1]=positions[1];reach=reach+(positions[2]-positions[1]):length()+(positions[3]-positions[2]):length()
        end
        local _,yaw=self.actor.rotation:getAnglesXZ();local facing=util.transform.rotateZ(yaw)
        local scale=reach/92;local points=Body.conduct(clock and clock.beat or 0,now-self.began,ending)
        local function target(side) local p=points[side];return v(p[1],p[2],p[3]-100)*scale end
        local profile={kind='conductor',anchor='feet',facing=facing,targets={L=target('L'),R=target('R')}}
        self.pose:frame(dt,{position=(shoulders[1]+shoulders[2])*.5,rotation=facing},{},now,0,profile,nil,clock)
        return true
    end
    return c
end
return M
