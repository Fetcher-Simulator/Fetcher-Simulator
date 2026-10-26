-- Index the effective record database once, without naming any particular mod.
local C={}
C.bodySlots={{key='neck',label='Neck',part=2},{key='chest',label='Chest',part=3},
 {key='groin',label='Groin',part=4},{key='hands',label='Hands',part=5},{key='wrists',label='Wrists',part=6},
 {key='forearms',label='Forearms',part=7},{key='upperarms',label='Upper arms',part=8},
 {key='feet',label='Feet',part=9},{key='ankles',label='Ankles',part=10},{key='knees',label='Knees',part=11},
 {key='upperlegs',label='Upper legs',part=12},{key='clavicles',label='Clavicles',part=13},{key='tail',label='Tail',part=14}}
local allowedParts={};for _,slot in ipairs(C.bodySlots) do allowedParts[slot.key]=true end
local function validId(s,optional)
 return type(s)=='string' and #s<=256 and (optional or #s>0) and not s:find('[%z\1-\31\127]')
end
function C.validBody(value)
 if value.bodyRace~=nil and not validId(value.bodyRace,true) then return false end
 if value.rig~=nil and (type(value.rig)~='number' or value.rig%1~=0 or value.rig<0 or value.rig>2) then return false end
 if value.tail~=nil and type(value.tail)~='boolean' then return false end
 if value.parts~=nil then
  if type(value.parts)~='table' then return false end
  for key,part in pairs(value.parts) do if not allowedParts[key] or not validId(part,true) then return false end end
 end
 return true
end
local function id(value) return tostring(value or ''):lower() end
local function sort(entries)
    table.sort(entries,function(a,b) return a.label:lower()<b.label:lower() end)
end
function C.valid(value)
    if type(value)~='table' or type(value.isMale)~='boolean' then return false end
    local count=0
    for key in pairs(value) do
        if key~='race' and key~='head' and key~='hair' and key~='isMale' and key~='bodyRace' and key~='rig' and key~='tail' and key~='parts' then return false end
        count=count+1
    end
    if count<4 or count>8 or not C.validBody(value) then return false end
    for _,key in ipairs({'race','head','hair'}) do
        local s=value[key]
        if type(s)~='string' or #s>256 or (key~='hair' and #s==0) or s:find('[%z\1-\31\127]') then return false end
    end
    return true
end
function C.copy(value)
 local parts={};for key,part in pairs(value.parts or {}) do parts[key]=part end
 return {race=value.race,head=value.head,hair=value.hair,isMale=value.isMale,
  bodyRace=value.bodyRace or '',rig=value.rig or 0,tail=value.tail~=false,parts=parts}
end
function C.new(races,parts,slots,current)
    local self={races={},index={},all={heads={},hair={}},allRaces=false,body={}}
    local names={}
    for _,r in ipairs(races) do
        local key=id(r.id)
        names[key]=r.name~='' and r.name or tostring(r.id)
        if r.isPlayable or key==id(current.race) then
            local row={id=tostring(r.id),label=(r.name~='' and r.name or tostring(r.id))..' ['..tostring(r.id)..']',
                male={heads={},hair={}},female={heads={},hair={}}}
            self.index[key]=row;self.races[#self.races+1]=row
        end
    end
    for _,slot in ipairs(C.bodySlots) do self.body[slot.key]={} end
    for _,p in ipairs(parts) do
        if p.isPlayable and not p.isVampire and not p.isFirstPerson and p.type=='skin' and p.model~='' then
            for _,slot in ipairs(C.bodySlots) do if slot.part==p.part then
                local list=self.body[slot.key]
                list[#list+1]={id=tostring(p.id),label=(names[id(p.race)] or tostring(p.race))..' / '
                    ..(p.isFemale and 'Female' or 'Male')..': '..tostring(p.id),search=tostring(p.model)..' '..tostring(p.race)}
            end end
        end
        local race=self.index[id(p.race)]
        if p.isPlayable and not p.isVampire and p.type=='skin' and p.model~=''
            and (p.part==slots.Head or p.part==slots.Hair) then
            local search=tostring(p.id)..' '..tostring(p.model)..' '..tostring(p.race)..' '..(names[id(p.race)] or '')
            local all=p.part==slots.Head and self.all.heads or self.all.hair
            all[#all+1]={id=tostring(p.id),label=(names[id(p.race)] or tostring(p.race))..' / '
                ..(p.isFemale and 'Female' or 'Male')..': '..tostring(p.id),search=search}
            if race then
                local group=p.isFemale and race.female or race.male
                local entries=p.part==slots.Head and group.heads or group.hair
                entries[#entries+1]={id=tostring(p.id),label=tostring(p.id),search=search}
            end
        end
    end
    for _,list in pairs(self.body) do sort(list) end
    sort(self.races)
    sort(self.all.heads);sort(self.all.hair)
    for _,r in ipairs(self.races) do for _,g in ipairs({r.male,r.female}) do sort(g.heads);sort(g.hair) end end
    setmetatable(self,{__index=C})
    -- A saved mixed appearance must open without silently replacing its parts.
    if C.valid(current) and not self:contains(current) then
        self.allRaces=true
        if not self:contains(current) then self.allRaces=false end
    end
    return self
end
function C:choices(race,isMale)
    local r=self.index[id(race)];local g=r and (isMale and r.male or r.female) or {heads={},hair={}}
    if self.allRaces then g=self.all end
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
    local result=C.copy(value);result.race=race;result.isMale=isMale
    result.head=has(head,value.head) and value.head or (head[1] and head[1].id or '')
    result.hair=has(hair,value.hair) and value.hair or (hair[2] and hair[2].id or '')
    return result
end
function C:bodyChoices(key)
 local result={{id='',label='Automatic (body race)'}}
 for _,entry in ipairs(self.body[key] or {}) do result[#result+1]=entry end
 return result
end
function C:contains(value)
    if not C.valid(value) then return false end
    local visual=value.bodyRace and value.bodyRace~='' and value.bodyRace or value.race
    if not self.index[id(visual)] then return false end
    for key,part in pairs(value.parts or {}) do if part~='' and not has(self.body[key] or {},part) then return false end end
    local heads,hair=self:choices(visual,value.isMale)
    return self.index[id(value.race)]~=nil and has(heads,value.head) and has(hair,value.hair)
end
return C
