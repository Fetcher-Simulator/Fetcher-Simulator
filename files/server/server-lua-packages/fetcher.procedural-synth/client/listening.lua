-- Local monitoring and bounded collision probes; never edits/network-sends music.
return function(synth,nearby,util,storage)
    local L={available=type(synth.setListening)=='function',nextRoom=-1,nextMark=-1,nextProbe=-1,cursor=1,outputs={},bands={},marked=setmetatable({},{__mode='k'})}
    local V=util.vector3
    L.specs={reference={min=50,max=1000,step=10},rolloff={min=.1,max=2,step=.05},wet={min=0,max=1,unit='%',step=.02},absorption={min=.05,max=.95,step=.05}}
    L.options={balanced=storage:get('balanced')==true,room=storage:get('room')~=false,reference=250,rolloff=.45,wet=.3,absorption=.35}
    for key,spec in pairs(L.specs) do local value=storage:get(key);if type(value)=='number' and value==value and value>=spec.min and value<=spec.max then L.options[key]=value end end
    L.geometry={width=12,depth=12,height=4,enclosure=0,scattering=.3}
    local dirs={}
    if L.available then
        dirs={V(1,0,0),V(-1,0,0),V(0,1,0),V(0,-1,0),V(0,0,1),V(0,0,-1)}
        for _,x in ipairs{-1,1} do for _,y in ipairs{-1,1} do dirs[#dirs+1]=V(x,y,0)/math.sqrt(2);dirs[#dirs+1]=V(x,y,1)/math.sqrt(3) end end
    end
    local function clamp(x,a,b) return math.max(a,math.min(b,x)) end
    function L.fit(rays,units)
        local hit=0;for _,r in ipairs(rays) do if r.hit then hit=hit+1 end end
        local enclosure=rays[5].hit and clamp((hit-2)/12,0,1) or 0
        local function size(a,b,max) return clamp((rays[a].distance+rays[b].distance)/units,1,max) end
        local scatter=0;local index=7
        for _,x in ipairs{2,1} do for _,y in ipairs{4,3} do
            local expected=math.min(rays[x].distance,rays[y].distance)*math.sqrt(2)
            scatter=scatter+math.abs(rays[index].distance-expected)/math.max(1,expected);index=index+2
        end end
        return {width=size(1,2,200),depth=size(3,4,200),height=size(5,6,100),enclosure=enclosure,scattering=clamp(scatter/4,0,1)}
    end
    function L:apply()
        if not self.available then return end
        local values={};for k,v in pairs(self.options) do values[k]=v end;for k,v in pairs(self.geometry) do values[k]=v end
        synth.setListening(values)
    end
    function L:set(key,value)
        local spec=self.specs[key]
        if key=='balanced' or key=='room' then if type(value)~='boolean' then return false end
        elseif not spec or type(value)~='number' or value~=value or value<spec.min or value>spec.max then return false end
        self.options[key]=value;storage:set(key,value);self:apply();return true
    end
    function L:status()
        local state=self.available and synth.getListening() or {}
        local g=self.geometry
        return string.format('Estimated %.1f x %.1f x %.1f m | Enclosure %.0f%% | Decay %.2fs\n%s | Monitor band: %s | Balanced outputs: %d',g.width,g.depth,g.height,g.enclosure*100,state.decay or 0,
            state.reflectionsAvailable and 'Shared early/late reflections available' or 'Reflections unavailable (dry fallback)',self.scope and tostring(self.scope) or 'none selected/accepted',state.balancedOutputs or 0)
    end
    function L:reset() self.selected=nil;self.scope=nil;self.probe=nil;self.cell=nil;self.geometry.enclosure=0;self.outputs={};self.bands={};self:apply() end
    function L:frame(instances,session,performer,transport,now,cell)
        if not self.available then return end
        local scope=self.selected or session and session.id or performer.membership and performer.membership.id
        self.scope=scope
        if now>=self.nextMark then
            self.nextMark=now+.25;self.outputs={};self.bands={}
            for id,i in pairs(instances) do
                self.bands[#self.bands+1]={id=id,label='Keyboard / band '..id}
                local function add(a,source)
                    if a and not a.closed and a.setMonitorEligible then
                        local selected=id==scope
                        if self.marked[a]~=selected then a:setMonitorEligible(selected);self.marked[a]=selected end
                        self.outputs[#self.outputs+1]={audio=a,source=source}
                    end
                end
                add(i.audio,i.source)
                transport:each(i,function(a,slot) add(a,slot==0 and i.source or i.outputs[slot].source) end)
            end
            table.sort(self.bands,function(a,b) return a.id<b.id end)
        end
        if #self.outputs==0 or not nearby.castRay then return end
        if now<self.nextProbe then return end;self.nextProbe=now+.05
        local state=synth.getListening();local position=state.listenerPosition
        if not position then return end
        if cell~=self.cell then self.cell=cell;self.probe=nil;self.nextRoom=-1;self.geometry.enclosure=0;self:apply() end
        local mask=util.bitOr(nearby.COLLISION_TYPE.World,nearby.COLLISION_TYPE.Door,nearby.COLLISION_TYPE.HeightMap)
        local function ray(from,to)
            local result=nearby.castRay(from,to,{collisionType=mask})
            return result.hit and result.hitPos and (result.hitPos-from):length() or (to-from):length(),result.hit
        end
        if not self.probe and now>=self.nextRoom then self.probe={origin=position,rays={}} end
        if self.probe then
            -- At most two geometry rays this frame, regardless of band size.
            for _=1,2 do
                local p=self.probe;local n=#p.rays+1
                local distance,hit=ray(p.origin,p.origin+dirs[n]*5000)
                p.rays[n]={distance=math.max(1,distance),hit=hit}
                if n==#dirs then self.geometry=L.fit(p.rays,synth.unitsPerMeter or 69.99125109);self.probe=nil;self.nextRoom=now+1.5;self:apply();break end
            end
        end
        -- One direct-path test per frame, round-robin across current outputs.
        self.cursor=self.cursor%#self.outputs+1;local item=self.outputs[self.cursor]
        if item.source and item.source:isValid() and item.audio.setObstruction then
            local target=item.source.position+V(0,0,40);local length=(target-position):length()
            if not self.options.room then item.audio:setObstruction(0)
            elseif length<3000 then local distance,hit=ray(position,target);item.audio:setObstruction(hit and distance<length-24 and 1 or 0) end
        end
    end
    L:apply();return L
end
