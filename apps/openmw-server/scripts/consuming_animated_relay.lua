-- Transient presentation only. No inventory, effects, equipment or storage calls.
local mp = require('mp')
local groups = { drink = 'potionl', eat = 'eatingr', beverage = 'drinkbone', bugmusk = 'bugmusk2',
    skooma = 'skoomapipe', smoke = 'smokepipe1', cigar = 'smoke1r' }
local meshes = {
    drink = { potion = true }, eat = { ingredient = true, item = true },
    beverage = { beverage = true, item = true, goblet = true }, bugmusk = { bugmusk = true },
    skooma = { skooma = true }, smoke = { pipe = true }, cigar = { cigar = true },
}
local sounds = { drink = { drink = 1, shatter = 3 }, eat = { eat = 1 },
    beverage = { drink = 1, shatter = 3 }, bugmusk = { bugmusk = 1 },
    skooma = { bong = 1 }, smoke = { smoke = 1 }, cigar = { smoke = 1 } }
local sessions, budgets = {}, {}
local function number(value, low, high)
    return type(value) == 'number' and value == value and value >= low and value <= high
end
local function integer(value, low, high) return number(value, low, high) and value % 1 == 0 end
local function stringBound(value, limit)
    return type(value) == 'string' and #value > 0 and #value <= limit and not value:find('%c')
end
local function relevant(player, cell)
    if player.cell == cell then return true end
    for _, loaded in ipairs(player.loadedActorCells or {}) do if loaded == cell then return true end end
    return false
end
local function sweep(now)
    for key, session in pairs(sessions) do
        if now > session.expires then sessions[key] = nil end
    end
    for guid, budget in pairs(budgets) do
        if now - budget.time > 60 then budgets[guid] = nil end
    end
end
local function relay(data)
    if type(data) ~= 'table' then return end
    -- Bound even ignored/spoofed fields; never echo input tables or nested values.
    local count = 0
    for k, v in pairs(data) do
        count = count + 1
        if count > 20 or not stringBound(k, 32) then return end
        if type(v) == 'string' then if not stringBound(v, 256) then return end
        elseif type(v) ~= 'number' and type(v) ~= 'boolean' then return end
    end
    local guid = data.pid -- overwritten by the native authenticated event bridge
    if not integer(guid, 1, 4294967295) then return end
    local source = mp.getPlayer(guid)
    if not source then return end
    local now = mp.getUptime()
    sweep(now)
    local budget = budgets[guid] or { tokens = 60, time = now }
    budgets[guid] = budget
    budget.tokens = math.min(60, budget.tokens + math.max(0, now - budget.time) * 12)
    budget.time = now
    if budget.tokens < 1 then return end
    budget.tokens = budget.tokens - 1
    if data.version ~= 1 or not integer(data.token, 1, 9007199254740991) then return end
    local cell = source.cell
    local key = 'p:' .. guid
    if data.actorId ~= nil then
        if not integer(data.actorId, 1, 8589934591) or not stringBound(data.cell, 256) then return end
        local actor = mp.getActorByInstanceId(data.actorId)
        if not actor or not actor.isNpc or actor.isDead or actor.authorityGuid ~= guid
            or actor.cell ~= data.cell or not relevant(source, actor.cell) then return end
        cell, key = actor.cell, 'a:' .. data.actorId
    end
    if not stringBound(cell, 256) then return end
    local session = sessions[key]
    local out = { version = 1, op = data.op, token = data.token, sourceGuid = guid,
        actorId = data.actorId, cell = cell }
    if data.op == 'start' then
        if groups[data.kind] ~= data.group or not groups[data.kind]
            or not stringBound(data.recordId, 128) or data.recordId:find('[/\\]')
            or not number(data.speed, 0.25, 4) or not number(data.duration, 0.1, 30) then return end
        if data.actorId then
            if data.kind ~= 'drink' or type(data.npcGulp) ~= 'boolean'
                or not number(data.gulpVolume, 0, 1) or not number(data.gulpPitch, 0.5, 2) then return end
            out.npcGulp, out.gulpVolume, out.gulpPitch = data.npcGulp, data.gulpVolume, data.gulpPitch
        end
        if session and (now - session.started < 0.5 or session.token == data.token) then return end
        local size = 0
        for _ in pairs(sessions) do size = size + 1 end
        if size >= 512 and not session then return end
        session = { guid = guid, cell = cell, token = data.token, kind = data.kind,
            started = now, expires = now + data.duration, operations = 0, effectsPlayed = false, recipients = {} }
        sessions[key] = session
        out.kind, out.group, out.recordId = data.kind, data.group, data.recordId:lower()
        out.speed, out.duration = data.speed, data.duration
    else
        if not session or session.guid ~= guid or session.cell ~= cell or session.token ~= data.token then return end
        if session.operations >= 24 then return end
        if data.op == 'vfx' then
            if not meshes[session.kind][data.mesh] or (data.bone ~= 'left' and data.bone ~= 'right') then return end
            out.mesh, out.bone = data.mesh, data.bone
        elseif data.op == 'sound' or data.op == 'stopSound' then
            local variants = sounds[session.kind][data.sound]
            if not variants or not integer(data.variant, 1, variants)
                or not number(data.volume, 0, 1) or not number(data.pitch, 0.5, 2) then return end
            out.sound, out.variant, out.volume, out.pitch = data.sound, data.variant, data.volume, data.pitch
        elseif data.op == 'effects' then
            if session.effectsPlayed then return end
            session.effectsPlayed = true
        elseif data.op ~= 'removeVfx' and data.op ~= 'finish' then return end
        session.operations = session.operations + 1
    end
    -- Pin recipients at start. Late join/cell load never replays a partial action.
    for _, target in ipairs(mp.getPlayers()) do
        local targetGuid = target.guid
        if targetGuid ~= guid and relevant(target, cell)
            and (data.op == 'start' or session.recipients[targetGuid]) then
            session.recipients[targetGuid] = true
            mp.send(targetGuid, 'PotionAnim_Presentation', out)
        end
    end
    if data.op == 'finish' then
        -- Retain only a short tombstone to prevent immediate replay of a token.
        session.operations, session.expires = 24, now + 0.5
    end
end
return {
    engineHandlers = { onUpdate = function() sweep(mp.getUptime()) end },
    eventHandlers = {
        PotionAnim_ConsumePresentation = relay,
        OnPlayerDisconnect = function(data)
            budgets[data.guid] = nil
            for key, session in pairs(sessions) do
                if session.guid == data.guid then sessions[key] = nil end
            end
        end,
    },
}
