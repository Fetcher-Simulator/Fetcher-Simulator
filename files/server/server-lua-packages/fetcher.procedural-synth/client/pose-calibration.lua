-- Local, data-only pose calibration. Never changes score/network/actor state.
return function(Profiles,util,storage)
    local M={names={'keyboard','lute','drum','violin','cello','trumpet','flute','clarinet','timpani'},boneNames={},revision=0,enabled=true}
    local v,T=util.vector3,util.transform
    local defaults,values,cache={},{},{}
    local function copy(t) if type(t)~='table' then return t end;local out={};for k,x in pairs(t) do out[k]=copy(x) end;return out end
    local function xyz(p) return {p.x,p.y,p.z} end
    local function vector(p) return v(p[1],p[2],p[3]) end
    local function rotation(p) return T.rotateZ(math.rad(p[3]))*T.rotateY(math.rad(p[2]))*T.rotateX(math.rad(p[1])) end
    local function finite(n) return type(n)=='number' and n==n and math.abs(n)<math.huge end
    for _,side in ipairs{'L','R'} do
        for _,part in ipairs{'UpperArm','Forearm','Hand'} do M.boneNames[#M.boneNames+1]='Bip01 '..side..' '..part end
        for finger=0,4 do for _,segment in ipairs{'','1','2'} do M.boneNames[#M.boneNames+1]='Bip01 '..side..' Finger'..finger..segment end end
    end
    local bones={};for _,name in ipairs(M.boneNames) do bones[name]=true end
    for _,name in ipairs(M.names) do
        local p=Profiles[name];local d={propPosition=xyz(p.position),propRotation={0,0,0},bones={},extras={}}
        for _,side in ipairs{'L','R'} do
            d[side..'Target']=xyz(p.targets[side]);d[side..'Rotation']={0,0,0};d[side..'Pole']={side=='L' and -1 or 1,0,-.6}
            d[side..'CurlScale']=1;d[side..'CurlBias']=0;d[side..'MotionScale']=1
        end
        for n,e in ipairs(p.extras or {}) do d.extras[n]={position=e.position and xyz(e.position) or {0,0,0},rotation={0,0,0}} end
        defaults[name]=d;values[name]=copy(d)
    end
    function M.spec(path)
        local key=path[1]
        if #path==1 and (key=='LCurlScale' or key=='RCurlScale' or key=='LMotionScale' or key=='RMotionScale') then return {min=0,max=3,step=.05} end
        if #path==1 and (key=='LCurlBias' or key=='RCurlBias') then return {min=-1.5,max=1.5,step=.02} end
        local axis=path[#path];if axis~=1 and axis~=2 and axis~=3 then return end
        if #path==2 then
            if key=='propPosition' or key=='LTarget' or key=='RTarget' then return {min=-200,max=200,step=.25} end
            if key=='propRotation' or key=='LRotation' or key=='RRotation' then return {min=-180,max=180,step=1} end
            if key=='LPole' or key=='RPole' then return {min=-3,max=3,step=.05} end
        elseif #path==4 and ((key=='bones' and bones[path[2]]) or (key=='extras' and type(path[2])=='number' and path[2]>=1 and path[2]<=2 and path[2]%1==0)) then
            if path[3]=='rotation' then return {min=-180,max=180,step=1} end
            if path[3]=='position' then return {min=key=='bones' and -15 or -100,max=key=='bones' and 15 or 100,step=.1} end
        end
    end
    local function read(data,path)
        for _,key in ipairs(path) do if type(data)~='table' then return 0 end;data=data[key] end
        return type(data)=='number' and data or 0
    end
    local function assign(data,path,value)
        for n=1,#path-1 do local key=path[n];data[key]=data[key] or {};data=data[key] end
        data[path[#path]]=value
    end
    function M:get(name,path) return values[name] and read(values[name],path) end
    function M:set(name,path,value)
        local spec=M.spec(path);if not values[name] or not spec or not finite(value) or value<spec.min or value>spec.max then return false end
        if path[1]=='extras' and not defaults[name].extras[path[2]] then return false end
        assign(values[name],path,value);cache[name]=nil;self.revision=self.revision+1;return true
    end
    function M:reset(name) if not defaults[name] then return false end;values[name]=copy(defaults[name]);cache[name]=nil;self.revision=self.revision+1;return true end
    function M:toggle() self.enabled=not self.enabled;self.revision=self.revision+1 end
    function M:profile(name)
        local base=Profiles[name];if not base or not self.enabled then return base end
        if cache[name] then return cache[name] end
        local d=values[name];local p={};for k,x in pairs(base) do p[k]=x end
        p.position=vector(d.propPosition);p.rotation=rotation(d.propRotation)*base.rotation;p.targets={L=vector(d.LTarget),R=vector(d.RTarget)}
        p.tuning={bones={}}
        for _,side in ipairs{'L','R'} do
            p.tuning[side..'Rotation']=rotation(d[side..'Rotation']);p.tuning[side..'Pole']=vector(d[side..'Pole'])
            for _,key in ipairs{'CurlScale','CurlBias','MotionScale'} do p.tuning[side..key]=d[side..key] end
        end
        for name,b in pairs(d.bones) do
            local pos={};local rot={};local nonzero=false
            for n=1,3 do pos[n]=read(b,{'position',n});rot[n]=read(b,{'rotation',n});nonzero=nonzero or pos[n]~=0 or rot[n]~=0 end
            if nonzero then p.tuning.bones[name:lower()]={position=vector(pos),rotation=rotation(rot)} end
        end
        p.extras={}
        for n,extra in ipairs(base.extras or {}) do
            local e={};for k,x in pairs(extra) do e[k]=x end
            e.position=extra.position and vector(d.extras[n].position) or extra.position
            e.rotation=rotation(d.extras[n].rotation)*(extra.rotation or T.identity)
            -- Hand-following mallets use the local offset too.
            if extra.hand then e.position=vector(d.extras[n].position) end
            p.extras[n]=e
        end
        cache[name]=p;return p
    end
    local function restore(saved)
        for _,name in ipairs(M.names) do
            values[name]=copy(defaults[name]);local data=type(saved)=='table' and saved[name]
            local function walk(node,path)
                if type(node)=='number' then M:set(name,path,node)
                elseif type(node)=='table' and #path<4 then for key,item in pairs(node) do
                    local nextPath=copy(path);nextPath[#nextPath+1]=key;walk(item,nextPath)
                end end
            end
            if type(data)=='table' then walk(data,{}) end;cache[name]=nil
        end
        M.revision=M.revision+1
    end
    local function changes(current,base)
        if type(current)~='table' then return current~=base and current or nil end
        local out={};for key,item in pairs(current) do local changed=changes(item,type(base)=='table' and base[key] or nil);if changed~=nil then out[key]=changed end end
        return next(out) and out or nil
    end
    function M:save() storage:set('profiles',changes(values,defaults) or {});storage:set('enabled',self.enabled);storage:set('format',1);return true end
    function M:placedKeyboard()
        local p=self:profile('keyboard');local out={};for k,x in pairs(p) do out[k]=x end
        out.targets={L=p.targets.L-Profiles.keyboard.position,R=p.targets.R-Profiles.keyboard.position};return out
    end
    function M:reload()
        -- get() returns a read-only userdata view in the engine, unlike the
        -- plain Lua tables used by tests. Validation walks a writable copy.
        local saved=storage:get('format')==1 and (storage.getCopy and storage:getCopy('profiles') or storage:get('profiles')) or nil
        restore(saved);self.enabled=storage:get('enabled')~=false;return true
    end
    local function literal(data,indent)
        if type(data)=='number' then return string.format('%.12g',data) end
        if type(data)=='boolean' then return tostring(data) end
        if type(data)=='string' then return string.format('%q',data) end
        local keys={};for k in pairs(data) do keys[#keys+1]=k end;table.sort(keys,function(a,b) return tostring(a)<tostring(b) end)
        local out={'{'};for _,key in ipairs(keys) do out[#out+1]=string.rep(' ',indent+2)..'['..literal(key,0)..']='..literal(data[key],indent+2)..',' end
        out[#out+1]=string.rep(' ',indent)..'}';return table.concat(out,'\n')
    end
    function M:export(name)
        if name and not values[name] then return end
        local text='return '..literal({format=1,basePackage=37,enabled=self.enabled,profiles=name and {[name]=values[name]} or values},0)
        storage:set('lastExport',text);return text
    end
    M:reload();return M
end
