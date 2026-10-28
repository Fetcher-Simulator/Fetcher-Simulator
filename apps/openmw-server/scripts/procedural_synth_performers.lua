-- Session-scoped player consent and character-owned Studio NPCs.
return function(mp,format,emit)
local types=require('openmw.types')
local Layout=require('procedural_synth_band_layout')
local P={held={},pending={},accepted={},serial=0,registries={},spawning={}}
local function same(a,s,slot) return a and a.id==s.id and a.epoch==s.epoch and a.slot==slot end
local function playerFor(character)
    for _,p in ipairs(mp.getPlayers()) do if p.characterId==character then return p end end
end
local function inventory(p)
    local out={};for _,item in ipairs(types.Actor.inventory(p)) do if item.count>0 then out[(item.refId or item.id):lower()]=true end end
    return out
end
local function nearby(p,s)
    local home=mp.getObjectByMpNum(s.id);local a,b=p and p.position,home and home.position
    return p and home and p.cell==s.cell and a and b and (a.x-b.x)^2+(a.y-b.y)^2+(a.z-b.z)^2<=2000^2
end
function P:registry(character)
    if self.registries[character] then return self.registries[character] end
    local value=mp.getCharacterStorageForCharacter(character,'ProceduralSynth','performers') or {version=1,entries={}}
    if value.version~=1 or type(value.entries)~='table' or #value.entries>format.maxMembers then return nil end
    self.registries[character]=value;return value
end
-- Remove only registered identities; never delete an unrelated actor reusing an ID.
function P:removeEntry(registry,index,cell)
    local entry=registry.entries[index];if not entry then return false end
    local actor=entry.mpNum and mp.getActorByMpNum(entry.mpNum)
    if actor and actor.refId~=entry.refId then return false end
    if actor and not mp.removeActor(entry.mpNum,actor.cell or entry.cell or cell) then return false end
    table.remove(registry.entries,index);return true
end
function P:state(guid)
    local p=mp.getPlayer(guid);if not p then return end
    local available={};local items=inventory(p)
    for name,id in pairs(format.instruments) do if items[id] then available[name]=true end end
    local held=self.held[guid];if held and (not available[held] or p.health<=0) then self.held[guid]=nil;held=nil end
    local invite=self.pending[guid]
    if invite and mp.getUptime()>invite.expires then self.pending[guid]=nil;invite=nil end
    local accepted=self.accepted[guid]
    emit(guid,{op='performer',guid=guid,characterId=p.characterId,held=held or '',available=available,
        invitation=invite and {token=invite.token,leader=invite.leader,instrument=invite.instrument,slot=invite.slot} or nil,
        membership=accepted and {leader=accepted.leader,id=accepted.id,slot=accepted.slot} or nil})
end
function P:release(s)
    for guid,a in pairs(self.accepted) do if a.id==s.id and a.epoch==s.epoch then self.accepted[guid]=nil;self:state(guid) end end
    for guid,a in pairs(self.pending) do if a.id==s.id and a.epoch==s.epoch then self.pending[guid]=nil;self:state(guid) end end
end
function P:invite(s,slot)
    local t=s.band[slot] and s.band[slot].target
    if not t or t.kind~='player' then return end
    local p=playerFor(t.characterId);local leader=mp.getPlayer(s.owner)
    if not p or not leader or not nearby(p,s) or p.guid==s.owner then return end
    if self.accepted[p.guid] then return end
    if self.pending[p.guid] and self.pending[p.guid].expires>mp.getUptime() then return end
    self.serial=self.serial+1
    self.pending[p.guid]={id=s.id,epoch=s.epoch,slot=slot,token=s.epoch..':'..self.serial,
        instrument=t.instrument,leader=leader.name,expires=mp.getUptime()+30,key=format.targetKey(t)}
    s.invited=s.invited or {};s.invited[slot]=format.targetKey(t)
    emit(p.guid,{op='inviteNotice',leader=leader.name,instrument=t.instrument});self:state(p.guid)
end
function P:bind(s)
    for guid,a in pairs(self.accepted) do if a.id==s.id and (not s.band[a.slot].target or format.targetKey(s.band[a.slot].target)~=a.key) then
        self.accepted[guid]=nil;self:state(guid)
    end end
    for guid,a in pairs(self.pending) do if a.id==s.id and (not s.band[a.slot].target or format.targetKey(s.band[a.slot].target)~=a.key) then
        self.pending[guid]=nil;self:state(guid)
    end end
    s.invited=s.invited or {}
    for slot,m in ipairs(s.band) do if m.target and m.target.kind=='player' and s.invited[slot]~=format.targetKey(m.target) then self:invite(s,slot) end end
end
function P:resolve(s,slot,t)
    if t.kind=='player' then
        local p=playerFor(t.characterId);if not p then return 'Offline' end
        if p.health<=0 then self.accepted[p.guid]=nil;return 'Dead' end
        if not nearby(p,s) then return 'Out of range' end
        local a=self.accepted[p.guid]
        if self.pending[p.guid] and mp.getUptime()>self.pending[p.guid].expires then self.pending[p.guid]=nil end
        if p.guid~=s.owner and not same(a,s,slot) then
            if a then return 'Busy' end
            return same(self.pending[p.guid],s,slot) and 'Invitation pending' or 'Invite required'
        end
        if self.held[p.guid]~=t.instrument or not inventory(p)[format.instruments[t.instrument]] then return 'Choose '..t.instrument..' [F9]' end
        return 'Ready',{kind='player',guid=p.guid,characterId=p.characterId,name=p.name,instrument=t.instrument},'player:'..t.characterId
    elseif t.kind=='npc' then
        local registry=self:registry(s.characterId);local owned
        for _,entry in ipairs(registry and registry.entries or {}) do if entry.refId==t.refId and entry.mpNum==t.mpNum then owned=entry end end
        if not owned then return 'Not your performer' end
        local actor=mp.getActorByMpNum(t.mpNum)
        if not actor or actor.refId~=t.refId then return 'Missing' end
        if actor.isDead then return 'Dead' end
        if not nearby(actor,s) then return 'Out of range' end
        local seated=owned.posture=='seated' and (t.instrument=='keyboard' or t.instrument=='cello')
        return 'Ready',{kind='npc',mpNum=t.mpNum,refId=t.refId,name='Studio performer',instrument=t.instrument,posture=seated and 'seated' or nil},'npc:'..t.mpNum
    end
end
function P:roster(s)
    local choices={}
    for _,p in ipairs(mp.getPlayers()) do if nearby(p,s) then
        for name in pairs(format.instruments) do choices[#choices+1]={label=p.name..' / '..name,target={kind='player',characterId=p.characterId,instrument=name}} end
    end end
    local registry=self:registry(s.characterId)
    for _,entry in ipairs(registry and registry.entries or {}) do
        -- Recover a native spawn committed just before a server interruption.
        if not entry.mpNum and mp.getActorsInCell then
            local match
            for _,a in ipairs(mp.getActorsInCell(s.cell)) do if a.refId==entry.refId then if match then match=false;break else match=a end end end
            if match then entry.mpNum=match.mpNum;mp.setCharacterStorageForCharacter(s.characterId,'ProceduralSynth','performers',registry) end
        end
        local actor=entry.mpNum and mp.getActorByMpNum(entry.mpNum)
        if actor and nearby(actor,s) then for name in pairs(format.instruments) do
            choices[#choices+1]={label='NPC #'..entry.mpNum..' / '..name,target={kind='npc',mpNum=entry.mpNum,refId=entry.refId,instrument=name}}
        end end
    end
    table.sort(choices,function(a,b) return a.label<b.label end)
    emit(s.owner,{op='roster',id=s.id,epoch=s.epoch,choices=choices})
end
function P:hire(s,slot,options)
    options=options or {}
    local registry=self:registry(s.characterId);local home=mp.getObjectByMpNum(s.id)
    if not registry or #registry.entries>=format.maxMembers or not home then emit(s.owner,{op='error',message='NPC performer limit reached or roster unavailable.'});return false end
    local leader=mp.getPlayer(s.owner)
    local position=options.position or Layout.position(leader and leader.position or home.position,slot,format.maxMembers)
    if not position then return false end
    local serial=registry.nextId or (#registry.entries+1);registry.nextId=serial+1
    local id='fetcher_synth_bard_'..s.characterId..'_'..serial
    local entry={refId=id,cell=s.cell};registry.entries[#registry.entries+1]=entry
    if not mp.setCharacterStorageForCharacter(s.characterId,'ProceduralSynth','performers',registry) then table.remove(registry.entries);return false end
    if not mp.upsertDynamicRecord('npc',id,{baseId='fargoth',name='Studio Performer '..#registry.entries,mwscript='',isEssential=false,isRespawning=false},
        {scope='permanent',persistent=true}) then
        table.remove(registry.entries);mp.setCharacterStorageForCharacter(s.characterId,'ProceduralSynth','performers',registry);return false
    end
    self.spawning[id]={id=s.id,epoch=s.epoch,slot=slot,character=s.characterId,instrument=options.instrument or 'lute',complete=options.complete}
    if not mp.spawnActor(id,0,0,s.cell,position,{persistent=true,authorityGuid=s.owner}) then
        self.spawning[id]=nil;table.remove(registry.entries)
        mp.setCharacterStorageForCharacter(s.characterId,'ProceduralSynth','performers',registry);return false
    end
    return true
end
function P:dismiss(s,slot)
    local t=s.band[slot] and s.band[slot].target
    if not t or t.kind~='npc' then return end
    local registry=self:registry(s.characterId)
    for n,e in ipairs(registry and registry.entries or {}) do if e.mpNum==t.mpNum and e.refId==t.refId then
        if not self:removeEntry(registry,n,s.cell) then return end
        mp.setCharacterStorageForCharacter(s.characterId,'ProceduralSynth','performers',registry)
        emit(s.owner,{op='npcDismissed',id=s.id,epoch=s.epoch,slot=slot,mpNum=t.mpNum});self:roster(s);return
    end end
end
function P:spawned(data,sessions)
    if data.pid~=nil then return end
    local pending=self.spawning[data.refId];if not pending then return end
    self.spawning[data.refId]=nil
    local registry=self:registry(pending.character)
    if not registry then return end
    local index
    for n,e in ipairs(registry.entries) do if e.refId==data.refId then e.mpNum=data.mpNum;index=n end end
    if not index then
        local actor=mp.getActorByMpNum(data.mpNum)
        if actor and actor.refId==data.refId then mp.removeActor(data.mpNum,actor.cell) end
        return
    end
    mp.setCharacterStorageForCharacter(pending.character,'ProceduralSynth','performers',registry)
    local s=sessions[pending.id]
    if s and s.epoch==pending.epoch then
        local target={kind='npc',mpNum=data.mpNum,refId=data.refId,instrument=pending.instrument}
        if pending.complete then
            if pending.complete(target)==false then
                if index then self:removeEntry(registry,index,s.cell)
                else local actor=mp.getActorByMpNum(data.mpNum);if actor and actor.refId==data.refId then mp.removeActor(data.mpNum,actor.cell) end end
                mp.setCharacterStorageForCharacter(pending.character,'ProceduralSynth','performers',registry)
            end
        else emit(s.owner,{op='npcCreated',id=s.id,epoch=s.epoch,slot=pending.slot,target=target});self:roster(s) end
    elseif pending.complete then
        if index then self:removeEntry(registry,index,data.cell)
        else local actor=mp.getActorByMpNum(data.mpNum);if actor and actor.refId==data.refId then mp.removeActor(data.mpNum,actor.cell) end end
        mp.setCharacterStorageForCharacter(pending.character,'ProceduralSynth','performers',registry)
    end
end
function P:request(data,player,sessions)
    local guid=player.guid
    if data.op=='performer' then self:state(guid)
    elseif data.op=='hold' then
        if data.instrument=='none' then self.held[guid]=nil
        elseif format.instruments[data.instrument] and inventory(player)[format.instruments[data.instrument]] and player.health>0 then self.held[guid]=data.instrument
        else emit(guid,{op='error',message='Carry that instrument in your inventory before readying it.'}) end
        self:state(guid)
    elseif data.op=='leaveBand' then
        self.accepted[guid]=nil;self.pending[guid]=nil;self.held[guid]=nil;self:state(guid)
    elseif data.op=='respond' then
        local a=self.pending[guid];local s=a and sessions[a.id]
        if not a or a.token~=data.token or mp.getUptime()>a.expires or not s or s.epoch~=a.epoch
            or not nearby(player,s) or not s.band[a.slot].target or format.targetKey(s.band[a.slot].target)~=a.key then return true end
        self.pending[guid]=nil
        if data.accept==true and not self.accepted[guid] then self.accepted[guid]=a end
        self:state(guid)
    else return false end
    return true
end
function P:disconnect(guid) self.held[guid]=nil;self.pending[guid]=nil;self.accepted[guid]=nil end
return P
end
