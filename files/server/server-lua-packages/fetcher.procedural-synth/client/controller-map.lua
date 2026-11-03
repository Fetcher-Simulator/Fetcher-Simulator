-- Local absolute 7-bit CC bindings. No device identifiers or bindings go on the wire.
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=require(prefix..'shared.schema');local F=require(prefix..'shared.project')(schema)
local M={};M.__index=M;M.maxBindings=16;M.targets={};M.order={}
local function add(id,title,spec,kind,name,automatable)
    M.targets[id]={id=id,label=title,spec=spec,kind=kind,name=name,automatable=automatable}
    M.order[#M.order+1]=M.targets[id]
end
for _,name in ipairs(schema.order) do
    -- Use the same logarithmic travel for frequency/time controls as the synth UI.
    local s=F.automationSpecs[name];local spec={min=s.min,max=s.max,discrete=s.discrete}
    spec.log=spec.min>0 and not spec.discrete and (name:lower():find('frequency') or name:lower():find('cutoff') or name:lower():find('attack') or name:lower():find('decay') or name:lower():find('release'))~=nil
    add('param:'..name,'Synth / '..name,spec,'param',name,true)
end
add('level','Mixer / track level',{min=0,max=1},'level','trackLevel',true)
for _,name in ipairs(F.mixerOrder) do local s=F.mixerSpecs[name]
    add('mixer:'..name,'Mixer / '..name,{min=s[1],max=s[2],discrete=name=='eqEnabled' or name=='compEnabled'},'mixer',name,name=='delaySend' or name=='reverbSend')
end
add('master','Mixer / master',{min=0,max=1},'master')
for _,name in ipairs(F.effectsOrder) do local s=F.effectsSpecs[name]
    add('effects:'..name,'Returns / '..name,{min=s[1],max=s[2],discrete=name:find('Enabled')~=nil},'effects',name)
end
local function integer(v,a,b) return type(v)=='number' and v==v and v>=a and v<=b and v%1==0 end
function M.options(input,mapping)
    if input==nil then input={} end;if type(input)~='table' then return nil end
    local out,used={},{}
    for _,key in ipairs{'articulationCC','stringCC','pickCC'} do local cc=mapping and mapping[key];if cc and cc>=0 then used[cc]=true end end
    local count=0;for key in pairs(input) do if not integer(key,1,M.maxBindings) then return nil end;count=count+1 end
    if count~=#input then return nil end
    for _,entry in ipairs(input) do
        if type(entry)~='table' then return nil end
        for key in pairs(entry) do if key~='cc' and key~='target' and key~='track' and key~='pickup' then return nil end end
        if not integer(entry.cc,-1,119) or entry.cc==64 or entry.cc>=0 and used[entry.cc]
            or not M.targets[entry.target] or not integer(entry.track,0,F.maxTracks) or type(entry.pickup)~='boolean' then return nil end
        out[#out+1]={cc=entry.cc,target=entry.target,track=entry.track,pickup=entry.pickup}
        if entry.cc>=0 then used[entry.cc]=true end
    end
    return out
end
function M.value(raw,spec)
    if spec.discrete then return math.min(spec.max,spec.min+math.floor(raw*(spec.max-spec.min+1)/128)) end
    local t=raw/127
    return spec.log and spec.min*(spec.max/spec.min)^t or spec.min+(spec.max-spec.min)*t
end
function M.normal(value,spec)
    return spec.log and math.log(math.max(spec.min,value)/spec.min)/math.log(spec.max/spec.min) or (value-spec.min)/(spec.max-spec.min)
end
function M.new(r,saved) return setmetatable({r=r,bindings=M.options(saved,r.mapping) or {},state={}},M) end
function M:reset() self.state={};self.status=nil end
function M:edit(index,changes,remove)
    local r=self.r
    if r.w.recording or r.w.automationWriting or not integer(index,1,#self.bindings+(remove and 0 or 1)) or index>M.maxBindings then return false end
    local list={};for n,b in ipairs(self.bindings) do list[n]={cc=b.cc,target=b.target,track=b.track,pickup=b.pickup} end
    if remove then table.remove(list,index) else
        local b=list[index] or {cc=-1,target='param:cutoff',track=0,pickup=true};list[index]=b
        for k,v in pairs(changes) do b[k]=v end
    end
    list=M.options(list,r.mapping)
    if not list then r.message('Choose unique CCs from 0–119; CC 64 is sustain.');return false end
    self.bindings=list;self:reset();r.storage:set('controllerBindings',list);r.learn=nil;r.w.revision=r.w.revision+1;return true
end
function M:context(b)
    local r=self.r;local w=r.w;local target=M.targets[b.target];local index=b.track==0 and r:target() or b.track
    local t=w.project.tracks[index];local object,value
    if target.kind=='param' then
        if index==0 then object=w.cb.currentParams and w.cb.currentParams() else object=t and t.params end
        value=object and object[target.name]
    elseif target.kind=='level' then object=t;value=t and t.level
    elseif target.kind=='mixer' then object=t;value=t and t.mixer and t.mixer[target.name]
    elseif target.kind=='master' then object=w.project;value=w.project.master
    elseif target.kind=='effects' then object=w.project;value=w.project.effects and w.project.effects[target.name] end
    return target,index,object,value
end
function M:event(e)
    for slot,b in ipairs(self.bindings) do if b.cc==e.a then
        local r=self.r;local w=r.w;local target,index,object,current=self:context(b)
        if current==nil then self.state[slot]=nil;return true end -- missing fixed track: never redirect it
        local s=self.state[slot]
        if not s or s.project~=w.project or s.object~=object or s.index~=index or s.expected~=current then
            s={project=w.project,object=object,index=index,expected=current};self.state[slot]=s
        end
        local spec=target.spec
        if target.kind=='param' and target.name=='polyphony' and index==0 then spec=schema.specs.polyphony end
        local value=M.value(e.b,spec);local at=M.normal(value,spec);local wanted=M.normal(current,spec)
        if not s.acquired and b.pickup then
            s.acquired=math.abs(at-wanted)<=1/127 or s.previous and (s.previous-wanted)*(at-wanted)<=0
            s.previous=at
            if not s.acquired then self.status='Pickup: '..target.label;return true end
        end
        s.acquired=true
        -- Mixer/return faders edit the document. Level and sends have native score lanes; EQ/returns remain saved live edits.
        if target.automatable and w.automationWriting and index>0 then
            if not r:recordMapped(index,target.name,value,e.age) then return true end
        end
        if current~=value then
            if target.kind=='param' then w:setParameter(index,target.name,value)
            elseif target.kind=='level' then w:setTrack(index,'level',value,true)
            elseif target.kind=='mixer' then w:setMixer(index,target.name,value,true)
            elseif target.kind=='master' then w:setMaster(value)
            elseif target.kind=='effects' then w:setEffect(target.name,value) end
        end
        local _,_,newObject,newValue=self:context(b);s.object=newObject;s.expected=newValue
        self.status=target.label..' = '..string.format('%.4g',value);return true
    end end
    return false
end
return M
