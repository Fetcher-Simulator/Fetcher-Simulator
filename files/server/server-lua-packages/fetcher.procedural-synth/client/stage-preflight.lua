-- Short, bounded local collision preflight. Server still selects all grid X/Y/yaw.
return function(nearby,util,Layout)
    local M={};local V=util.vector3
    function M.new(anchor,count,complete,wide)
        local invalid=type(count)~='number' or count%1~=0 or count<0 or count>32
        if not invalid then invalid=not Layout.position(anchor,1,math.max(1,count)) end
        return setmetatable({anchor=anchor,count=count,complete=complete,wide=wide,index=1,step=1,heights={},rays=0,invalid=invalid}, {__index=M})
    end
    function M:finish(error)
        self.done=true;self.complete(not error and {anchor=self.anchor,heights=self.heights} or nil,error)
    end
    function M:frame()
        if self.done then return end
        if self.invalid then self:finish('Invalid stage dimensions.');return end
        if not nearby.castRay then self:finish('Stage collision checks are unavailable.');return end
        local mask=util.bitOr(nearby.COLLISION_TYPE.World,nearby.COLLISION_TYPE.Door,nearby.COLLISION_TYPE.HeightMap)
        local offsets=self.wide and {{0,0},{-110,-40},{135,-40},{-110,110},{135,110}} or {{0,0},{-58,-32},{58,-32},{-58,108},{58,108}}
        for _=1,8 do
            if self.index>self.count then self:finish();return end
            local p=Layout.position(self.anchor,self.index,self.count,self.wide)
            local function ray(a,b,radius)
                self.rays=self.rays+1;return nearby.castRay(a,b,{collisionType=mask,radius=radius or 0})
            end
            if self.step<=5 then
                local offset=offsets[self.step];local s,c=math.sin(p.rz),math.cos(p.rz)
                local x,y=p.x+offset[1]*c+offset[2]*s,p.y-offset[1]*s+offset[2]*c
                local hit=ray(V(x,y,self.anchor.z+96),V(x,y,self.anchor.z-96))
                if not hit.hit or not hit.hitPos or hit.hitNormal and hit.hitNormal.z<.85 then self:finish('No level floor for NPC '..self.index..'. Move to clearer ground and retry.');return end
                local z=hit.hitPos.z
                if math.abs(z-self.anchor.z)>64 or self.step>1 and math.abs(z-self.heights[self.index])>8 then self:finish('Uneven stage footprint for NPC '..self.index..'.');return end
                if self.step==1 then self.heights[self.index]=z end
            elseif self.step==6 then
                if ray(V(p.x,p.y,self.heights[self.index]+22),V(p.x,p.y,self.heights[self.index]+120),18).hit then self:finish('Blocked headroom for NPC '..self.index..'.');return end
            else
                local a,b=V(self.anchor.x,self.anchor.y,self.anchor.z+75),V(p.x,p.y,self.heights[self.index]+75)
                if ray(a,b).hit then self:finish('A wall separates NPC '..self.index..' from the leader.');return end
            end
            self.step=self.step+1
            if self.step==8 then self.step=1;self.index=self.index+1 end
        end
    end
    return M
end
