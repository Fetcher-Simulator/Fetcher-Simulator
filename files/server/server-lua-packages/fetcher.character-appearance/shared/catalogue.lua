-- Index the effective record database once, without naming any particular mod.
local C={}
local function id(value) return tostring(value or ''):lower() end
local function sort(entries)
    table.sort(entries,function(a,b) return a.label:lower()<b.label:lower() end)
end
function C.valid(value)
    if type(value)~='table' or type(value.isMale)~='boolean' then return false end
    local count=0
    for key in pairs(value) do
        if key~='race' and key~='head' and key~='hair' and key~='isMale' then return false end
        count=count+1
    end
    if count~=4 then return false end
    for _,key in ipairs({'race','head','hair'}) do
        local s=value[key]
        if type(s)~='string' or #s>256 or (key~='hair' and #s==0) or s:find('[%z\1-\31\127]') then return false end
    end
    return true
end
function C.copy(value) return {race=value.race,head=value.head,hair=value.hair,isMale=value.isMale} end
function C.new(races,parts,slots,current)
    local self={races={},index={}}
    for _,r in ipairs(races) do
        local key=id(r.id)
        if r.isPlayable or key==id(current.race) then
            local row={id=tostring(r.id),label=(r.name~='' and r.name or tostring(r.id))..' ['..tostring(r.id)..']',
                male={heads={},hair={}},female={heads={},hair={}}}
            self.index[key]=row;self.races[#self.races+1]=row
        end
    end
    for _,p in ipairs(parts) do
        local race=self.index[id(p.race)]
        if race and p.isPlayable and not p.isVampire and p.type=='skin' and p.model~=''
            and (p.part==slots.Head or p.part==slots.Hair) then
            local group=p.isFemale and race.female or race.male
            local entries=p.part==slots.Head and group.heads or group.hair
            entries[#entries+1]={id=tostring(p.id),label=tostring(p.id),search=tostring(p.id)..' '..tostring(p.model)}
        end
    end
    sort(self.races)
    for _,r in ipairs(self.races) do for _,g in ipairs({r.male,r.female}) do sort(g.heads);sort(g.hair) end end
    return setmetatable(self,{__index=C})
end
function C:choices(race,isMale)
    local r=self.index[id(race)];local g=r and (isMale and r.male or r.female) or {heads={},hair={}}
    local hair={{id='',label='No hair / bald'}}
    for _,entry in ipairs(g.hair) do hair[#hair+1]=entry end
    return g.heads,hair
end
local function has(entries,key)
    for _,entry in ipairs(entries) do if id(entry.id)==id(key) then return true end end
    return false
end
function C:choose(value,race,isMale)
    local head,hair=self:choices(race,isMale)
    return {race=race,isMale=isMale,head=has(head,value.head) and value.head or (head[1] and head[1].id or ''),
        hair=has(hair,value.hair) and value.hair or (hair[2] and hair[2].id or '')}
end
function C:contains(value)
    if not C.valid(value) then return false end
    local heads,hair=self:choices(value.race,value.isMale)
    return self.index[id(value.race)]~=nil and has(heads,value.head) and has(hair,value.hair)
end
return C
