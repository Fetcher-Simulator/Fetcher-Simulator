local mp = require('mp')
local config = require('config')
if config.PROCEDURAL_SYNTH_ENABLED ~= true then return {} end
local schema = require('procedural_synth_schema')
local policy = require('intent_policy')
local RECORD = 'fetcher_procedural_synth_v1'
local sessions, budgets, epoch = {}, {}, 0
local lastSweep = 0
local function integer(n, low, high)
    return type(n) == 'number' and n == n and n >= low and n <= high and n % 1 == 0
end
local function text(s, max) return type(s) == 'string' and #s > 0 and #s <= max and not s:find('%c') end
local function relevant(player, cell)
    if player.cell == cell then return true end
    for _, c in ipairs(player.loadedActorCells or {}) do if c == cell then return true end end
    return false
end
local function emit(guid, data) data.version = 1; mp.send(guid, 'Synth_Message', data) end
local function broadcast(s, out, except)
    out.id, out.epoch, out.seq, out.revision = s.id, s.epoch, s.outSeq, s.revision
    for _, player in ipairs(mp.getPlayers()) do
        if player.guid ~= except and relevant(player, s.cell) then
            if out.op == 'state' then s.recipients[player.guid] = true end
            if out.op == 'on' and not s.recipients[player.guid] then
                emit(player.guid, {op='state',id=s.id,epoch=s.epoch,seq=s.outSeq-1,revision=s.revision,params=s.params})
                s.recipients[player.guid] = true
            end
            emit(player.guid, out)
        end
    end
end
local function nextEvent(s) s.outSeq = s.outSeq + 1 end
local function release(s)
    nextEvent(s); broadcast(s, {op='close'})
    sessions[s.id] = nil
end
local function objectFor(id)
    local obj = mp.getObjectByMpNum(id)
    return obj and obj.refId == RECORD and obj or nil
end
local function near(player, obj)
    if not obj or player.cell ~= obj.cell then return false end
    local a, b = player.position, obj.position
    if not a or not b then return false end
    return (a.x-b.x)^2 + (a.y-b.y)^2 + (a.z-b.z)^2 <= 300^2
end
local function catalog(guid, character)
    local saved = mp.getCharacterStorageForCharacter(character, 'ProceduralSynth', 'presets')
    if type(saved) ~= 'table' then saved = { version=1, nextId=1, entries={} } end
    if saved.version ~= 1 or type(saved.entries) ~= 'table' or not integer(saved.nextId,1,2147483647) then
        emit(guid, {op='error', message='Unsupported preset library version; existing data was preserved.'})
        return nil
    end
    return saved
end
local function sendCatalog(guid, library)
    local list = {}
    for _, p in ipairs(schema.builtins) do list[#list+1] = {id=p.id, name=p.name} end
    for _, p in ipairs(library.entries) do list[#list+1] = {id=p.id, name=p.name} end
    emit(guid, {op='catalog', entries=list})
end
local function acquire(player, obj)
    local now = mp.getUptime()
    local s = sessions[obj.mpNum]
    if s and (not mp.getPlayer(s.owner) or now > s.expires) then release(s); s = nil end
    if s then
        if s.owner ~= player.guid then emit(player.guid, {op='error', message='This synthesizer is already in use.'}) end
        return
    end
    local count = 0
    for _ in pairs(sessions) do count = count + 1 end
    if count >= 8 then emit(player.guid, {op='error', message='All synthesizer sessions are in use.'}); return end
    epoch = epoch + 1
    s = { id=obj.mpNum, cell=obj.cell, owner=player.guid, epoch=epoch, inSeq=0, outSeq=1,
        revision=1, params=schema.state({}), expires=now+8, lastAlive=now, recipients={}, notes={} }
    sessions[s.id] = s
    broadcast(s, {op='state', params=s.params}, player.guid)
    emit(player.guid, {op='granted', id=s.id, epoch=s.epoch, seq=s.outSeq, revision=s.revision, params=s.params})
end
policy.registerHandler('Activate', 'procedural-synth', function(context)
    local obj = context.object
    if not obj or obj.recordId ~= RECORD then return end
    local verified = objectFor(obj.mpNum or 0)
    if not context.player or not near(context.player, verified) then return {accepted=false, reason='synth_out_of_range'} end
    acquire(context.player, verified)
    return { applyStandardAction=false, action='synth', stop=true }
end)
local function budget(guid, now, category, rate, capacity)
    local b = budgets[guid] or {}; budgets[guid] = b
    local slot = b[category] or {time=now, tokens=capacity}; b[category] = slot
    slot.tokens = math.min(capacity, slot.tokens + math.max(0,now-slot.time)*rate); slot.time = now
    if slot.tokens < 1 then return false end
    slot.tokens = slot.tokens - 1; return true
end
local function request(data)
    if type(data) ~= 'table' or data.version ~= 1 or not integer(data.pid,1,4294967295) then return end
    local fields = 0
    for k,v in pairs(data) do
        fields = fields + 1
        if fields > 14 or not text(k,24) then return end
        if type(v) == 'table' then if k ~= 'params' or not schema.state(v) then return end
        elseif type(v) == 'string' then if not text(v,96) then return end
        elseif type(v) ~= 'number' and type(v) ~= 'boolean' then return end
    end
    local player = mp.getPlayer(data.pid)
    if not player then return end
    local now = mp.getUptime()
    if data.op == 'acquire' then
        if not integer(data.id,1,4294967295) or not budget(data.pid,now,'acquire',1,2) then return end
        local obj=objectFor(data.id)
        if near(player,obj) then acquire(player,obj)
        else emit(data.pid,{op='error',message='Move closer to the synthesizer.'}) end
        return
    end
    if data.op == 'sync' then
        if not budget(data.pid,now,'sync',0.5,2) then return end
        for _, s in pairs(sessions) do
            if relevant(player,s.cell) then
                emit(data.pid,{op='state',id=s.id,epoch=s.epoch,seq=s.outSeq,revision=s.revision,params=s.params})
            end
        end
        return
    end
    if not integer(data.id,1,4294967295) then return end
    local s = sessions[data.id]
    if not s or s.owner ~= data.pid or s.epoch ~= data.epoch or not integer(data.seq,1,2147483647)
        or data.seq <= s.inSeq then return end
    if now > s.expires or not near(player,objectFor(s.id)) then release(s); return end
    s.inSeq = data.seq
    -- Safety operations bypass musical/edit rate limits.
    if data.op == 'close' then release(s); return end
    if data.op == 'heartbeat' then
        if budget(data.pid,now,'heartbeat',1,2) then s.expires=now+8 end
        return
    end
    if data.op == 'off' or data.op == 'panic' then
        if data.op == 'off' and not integer(data.note,0,127) then return end
        if data.op == 'off' then
            if not s.notes[data.note] then return end
            s.notes[data.note] = nil
        else
            if next(s.notes) == nil then return end
            s.notes = {}
        end
        nextEvent(s); broadcast(s,{op=data.op,note=data.note},s.owner); return
    end
    if data.op == 'on' then
        if not integer(data.note,0,127) or type(data.velocity) ~= 'number'
            or data.velocity ~= data.velocity or data.velocity < 0 or data.velocity > 1 then return end
        if not budget(data.pid,now,'notes',40,80) then
            -- End all remote voices when an optimistic local batch is rejected.
            if budget(data.pid,now,'errors',1,1) then
                s.notes = {}
                nextEvent(s); broadcast(s,{op='panic'})
            end
            return
        end
        s.notes[data.note] = true
        nextEvent(s); broadcast(s,{op='on',note=data.note,velocity=data.velocity},s.owner); return
    end
    if data.op == 'edit' then
        local params = schema.state(data.params)
        if not params or not budget(data.pid,now,'edits',12,16) then return end
        s.params = params; s.revision = s.revision + 1
        nextEvent(s); broadcast(s,{op='params',params=params},s.owner); return
    end
    if not budget(data.pid,now,'presets',2,4) or not integer(data.characterId,1,9007199254740991) then return end
    local library = catalog(data.pid,data.characterId)
    if not library then return end
    local selected
    for _, p in ipairs(library.entries) do if p.id == data.presetId then selected = p; break end end
    if data.op == 'list' then sendCatalog(data.pid,library); return end
    if data.op == 'load' or data.op == 'reset' then
        local params = data.op == 'reset' and schema.state({}) or selected and schema.state(selected.state)
        for _, p in ipairs(schema.builtins) do if data.op == 'load' and p.id == data.presetId then params=p.state end end
        if not params then return end
        s.params=params; s.revision=s.revision+1; s.notes={}
        nextEvent(s); broadcast(s,{op='preset',params=params,presetId=data.presetId}); return
    end
    if data.op == 'saveAs' or data.op == 'save' then
        local params = schema.state(data.params)
        if not params or not text(data.name,64) or not data.name:find('%S') then return end
        if data.op == 'save' and not selected then return end
        if data.op == 'saveAs' then
            if #library.entries >= 32 then emit(data.pid,{op='error',message='Personal preset limit: 32.'}); return end
            selected={id='p:'..library.nextId}; library.nextId=library.nextId+1
            library.entries[#library.entries+1]=selected
        end
        selected.name, selected.state = data.name, params
    elseif data.op == 'rename' then
        if not selected or not text(data.name,64) or not data.name:find('%S') then return end
        selected.name=data.name
    elseif data.op == 'delete' then
        if not selected then return end
        for i,p in ipairs(library.entries) do if p.id == selected.id then table.remove(library.entries,i); break end end
    else return end
    if not mp.setCharacterStorageForCharacter(data.characterId,'ProceduralSynth','presets',library) then
        emit(data.pid,{op='error',message='Preset storage failed. Please try again.'}); return
    end
    emit(data.pid,{op='saved',presetId=selected.id,deleted=data.op=='delete'})
    sendCatalog(data.pid,library)
end
local function sweep()
    local now = mp.getUptime()
    if now-lastSweep < 0.5 then return end
    lastSweep=now
    for _, s in pairs(sessions) do
        local owner=mp.getPlayer(s.owner)
        if not owner or now>s.expires or not near(owner,objectFor(s.id)) then release(s)
        elseif now-s.lastAlive >= 2 then
            s.lastAlive=now
            -- New listeners receive parameters only, never a historical note-on.
            for _, p in ipairs(mp.getPlayers()) do
                if relevant(p,s.cell) and p.guid ~= s.owner then
                    if not s.recipients[p.guid] then
                        emit(p.guid,{op='state',id=s.id,epoch=s.epoch,seq=s.outSeq,revision=s.revision,params=s.params})
                        s.recipients[p.guid]=true
                    end
                else s.recipients[p.guid]=nil end
            end
            nextEvent(s); broadcast(s,{op='alive'})
        end
    end
end
return {
    engineHandlers={onUpdate=sweep},
    eventHandlers={
        Synth_Request=request,
        OnServerInit=function()
            -- Runtime record with a vanilla mesh; no client ESP and no asset transfer.
            local ok=mp.upsertDynamicRecord('activator',RECORD,
                {name='OpenMW Synthesizer',model=config.PROCEDURAL_SYNTH_MODEL or 'meshes/m/Misc_de_lute_01.nif'},
                {scope='permanent',persistent=true})
            mp.log('[synth] runtime activator registered: '..tostring(ok))
            -- Placement is opt-in. The disposable fixture pins its expected mpNum
            -- so restarting it does not duplicate its persistent placed object.
            if config.PROCEDURAL_SYNTH_TEST_CELL then
                local existing=config.PROCEDURAL_SYNTH_TEST_MP_NUM and objectFor(config.PROCEDURAL_SYNTH_TEST_MP_NUM)
                if not existing then mp.placeObject(RECORD,1,config.PROCEDURAL_SYNTH_TEST_CELL,
                    config.PROCEDURAL_SYNTH_TEST_POSITION or {x=0,y=0,z=0}) end
            end
        end,
        OnPlayerDisconnect=function(data)
            budgets[data.guid]=nil
            for _,s in pairs(sessions) do if s.owner==data.guid then release(s) end end
        end,
        OnPlayerCellChange=function(data)
            for _,s in pairs(sessions) do
                s.recipients[data.guid]=nil
                if s.owner==data.guid then release(s) end
            end
        end,
    },
}
