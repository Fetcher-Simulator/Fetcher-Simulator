-- Short, bounded local collision preflight. Server still selects all grid X/Y/yaw.
return function(nearby,util,Layout)
    local M={};local V=util.vector3
    local function finite(n) return type(n)=='number' and n==n and math.abs(n)<1e8 end
    function M.new(anchor,count,complete,wide,plan,formation,home)
        local invalid=type(count)~='number' or count%1~=0 or count<0 or count>32
        if not invalid then invalid=not Layout.position(anchor,1,math.max(1,count)) end
        local positions=plan and Layout.positions(anchor,plan,formation);if plan and (not positions or #positions~=count) then invalid=true end
        return setmetatable({home=home,positions=positions,anchor=anchor,count=count,complete=complete,wide=wide,index=1,step=1,heights={},rays=0,invalid=invalid}, {__index=M})
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
            local p=self.positions and self.positions[self.index] or Layout.position(self.anchor,self.index,self.count,self.wide)
            local function ray(a,b,radius)
                self.rays=self.rays+1;return nearby.castRay(a,b,{collisionType=mask,radius=radius or 0})
            end
            local advance=true
            if self.step<=5 then
                local offset=offsets[self.step];local s,c=math.sin(p.rz),math.cos(p.rz)
                local x,y=p.x+offset[1]*c+offset[2]*s,p.y-offset[1]*s+offset[2]*c
                local limit=Layout.floorLimit(self.anchor,{x=x,y=y})
                -- Start near the expected floor to avoid picking a roof above an
                -- indoor stage. A higher uphill surface gets one bounded retry.
                local top=self.retry and self.anchor.z+limit+32 or (self.step>1 and self.heights[self.index] or self.anchor.z)+96
                local hit=ray(V(x,y,top),V(x,y,self.anchor.z-limit-32))
                if not hit.hit and not self.retry then self.retry=true;advance=false
                else
                    self.retry=nil
                    local normal=hit.hitNormal or {x=0,y=0,z=1}
                    if not hit.hit or not hit.hitPos or not finite(hit.hitPos.z) or not finite(normal.z)
                        or normal.z<Layout.floorNormalZ or not finite(normal.x or 0) or not finite(normal.y or 0) then
                        self:finish('No walkable floor for NPC '..self.index..'. Move away from cliffs or steep slopes and retry.');return
                    end
                    local z=hit.hitPos.z
                    if math.abs(z-self.anchor.z)>limit then self:finish('Stage floor is too far above or below the leader for NPC '..self.index..'.');return end
                    if self.step==1 then
                        if self.home and not Layout.inRange(self.home,{x=p.x,y=p.y,z=z}) then print(string.format('[SynthStageRange] client home=%s,%s,%s slot=%s,%s,%s limit=%s',self.home.x,self.home.y,self.home.z,p.x,p.y,z,Layout.stageRange));self:finish('Formation exceeds the keyboard range (2000 units). Reduce spacing/distance or choose a smaller preset.');return end
                        self.heights[self.index]=z;self.floor={x=normal.x or 0,y=normal.y or 0,z=normal.z}
                    else
                        local center=self.heights[self.index];local n=self.floor
                        local expected=center-(n.x*(x-p.x)+n.y*(y-p.y))/n.z
                        local distance=math.sqrt((x-p.x)^2+(y-p.y)^2)
                        if math.abs(z-expected)>Layout.floorRoughness or math.abs(z-center)>Layout.floorRoughness+distance*Layout.floorSlope then
                            self:finish('A ledge or broken floor crosses NPC '..self.index..'\'s footprint. Move to smoother ground and retry.');return
                        end
                    end
                end
            elseif self.step==6 then
                if ray(V(p.x,p.y,self.heights[self.index]+28),V(p.x,p.y,self.heights[self.index]+120),18).hit then self:finish('Blocked headroom for NPC '..self.index..'.');return end
            else
                local a,b=V(self.anchor.x,self.anchor.y,self.anchor.z+75),V(p.x,p.y,self.heights[self.index]+75)
                if ray(a,b).hit then self:finish('A wall separates NPC '..self.index..' from the leader.');return end
            end
            if advance then
                self.step=self.step+1
                if self.step==8 then self.step=1;self.index=self.index+1;self.floor=nil end
            end
        end
    end
    return M
end
