-- Run from repository root: lua scripts/test_consuming_animated_relay.lua
local tests, assertions = 0, 0
local function check(value, message) assertions = assertions + 1; assert(value, message) end
local function test(name, fn) fn(); tests = tests + 1; print('PASS ' .. name) end
local now, sent, actors, players, handler, module
local function reset()
    now, sent = 100, {}
    actors = { [7] = { isNpc = true, isDead = false, authorityGuid = 1, cell = 'INT:A' } }
    players = {
        { guid = 1, cell = 'INT:A' }, { guid = 2, cell = 'INT:A' },
        { guid = 3, cell = 'INT:B', loadedActorCells = {'INT:A'} }, { guid = 4, cell = 'INT:C' },
    }
    package.loaded.mp = {
        getPlayer = function(guid) for _, p in ipairs(players) do if p.guid == guid then return p end end end,
        getPlayers = function() return players end, getUptime = function() return now end,
        getActorByInstanceId = function(id) return actors[id] end,
        send = function(guid, event, data) sent[#sent + 1] = { guid = guid, event = event, data = data } end,
    }
    module = dofile('apps/openmw-server/scripts/consuming_animated_relay.lua')
    handler = module.eventHandlers.PotionAnim_ConsumePresentation
end
local function start(extra)
    local data = { pid = 1, version = 1, token = 100, op = 'start', kind = 'drink', group = 'potionl',
        recordId = 'potion_test', speed = 1, duration = 30 }
    for k, v in pairs(extra or {}) do data[k] = v end
    if data.actorId ~= nil then
        if data.npcGulp == nil then data.npcGulp = true end
        if data.gulpVolume == nil then data.gulpVolume = 1 end
        if data.gulpPitch == nil then data.gulpPitch = 1 end
    end
    handler(data)
end
local function op(name, extra)
    local data = { pid = 1, version = 1, token = 100, op = name }
    for k, v in pairs(extra or {}) do data[k] = v end
    handler(data)
end
test('authenticated sender, spoof ignored, no source echo, loaded cells only', function()
    reset(); start({ sourceGuid = 4 })
    check(#sent == 2); check(sent[1].guid == 2); check(sent[2].guid == 3)
    check(sent[1].data.sourceGuid == 1); check(sent[1].data.pid == nil)
end)
test('unknown or unauthenticated sender rejected', function()
    reset(); start({pid = 99}); check(#sent == 0)
    start({pid = 0}); check(#sent == 0)
end)
test('category, group, oversized fields and paths rejected', function()
    for _, extra in ipairs({{kind = 'execute'}, {group = 'attack1'}, {recordId = string.rep('a', 129)},
        {sourceGuid = string.rep('a', 257)}, {recordId = '../script'}, {extra = {}}, {kind = false}}) do
        reset(); start(extra); check(#sent == 0)
    end
end)
test('invalid speed/duration including NaN and infinity rejected', function()
    for _, extra in ipairs({{speed = 0}, {speed = 4.01}, {speed = 0/0}, {speed = math.huge},
        {duration = -1}, {duration = 31}, {speed = '1'}}) do reset(); start(extra); check(#sent == 0) end
end)
test('NPC identity, cell, authority, type and death enforced', function()
    reset(); start({actorId = 999, cell = 'INT:A'}); check(#sent == 0)
    start({actorId = 7, cell = 'INT:B'}); check(#sent == 0)
    actors[7].authorityGuid = 2; start({actorId = 7, cell = 'INT:A'}); check(#sent == 0)
    actors[7].authorityGuid = 1; actors[7].isNpc = false
    start({actorId = 7, cell = 'INT:A'}); check(#sent == 0)
    actors[7].isNpc = true; actors[7].isDead = true
    start({actorId = 7, cell = 'INT:A'}); check(#sent == 0)
    actors[7].isDead = false; start({actorId = 7, cell = 'INT:A'}); check(#sent == 2)
end)
test('NPC gulp metadata is validated and forwarded on start', function()
    reset(); start({actorId = 7, cell = 'INT:A', npcGulp = true, gulpVolume = 0.65, gulpPitch = 1.1})
    check(#sent == 2); check(sent[1].data.npcGulp == true)
    check(sent[1].data.gulpVolume == 0.65); check(sent[1].data.gulpPitch == 1.1)
    for _, extra in ipairs({
        {npcGulp = 'yes'}, {gulpVolume = -0.1}, {gulpVolume = 1.1}, {gulpPitch = 0.49}, {gulpPitch = 2.01}
    }) do
        reset(); extra.actorId, extra.cell = 7, 'INT:A'; start(extra); check(#sent == 0)
    end
end)
test('authority handoff rejects old owner and allows new owner', function()
    reset(); start({actorId = 7, cell = 'INT:A'}); sent = {}
    actors[7].authorityGuid = 2
    op('removeVfx', {actorId = 7, cell = 'INT:A'}); check(#sent == 0)
    now = now + 1; start({pid = 2, token = 101, actorId = 7, cell = 'INT:A'})
    check(#sent == 2); check(sent[1].data.sourceGuid == 2)
end)
test('placed reference and spawned MP number with identical low bits stay separate', function()
    reset()
    actors[4294967303] = { isNpc = true, isDead = false, authorityGuid = 2, cell = 'INT:A' }
    start({actorId = 7, cell = 'INT:A'})
    start({pid = 2, actorId = 4294967303, cell = 'INT:A'})
    check(#sent == 4)
    check(sent[1].data.actorId == 7)
    check(sent[3].data.actorId == 4294967303)
    sent = {}
    op('removeVfx', {actorId = 4294967303, cell = 'INT:A'})
    check(#sent == 0, 'placed reference authority cannot impersonate spawned actor')
end)
test('potion effects presentation is one-shot per validated session', function()
    reset(); start(); sent = {}
    op('effects'); check(#sent == 2)
    sent = {}; op('effects'); check(#sent == 0)
end)
test('resource keys and numeric sound variant whitelist', function()
    reset(); start(); sent = {}
    op('vfx', {mesh = '../../bad.nif', bone = 'left'}); check(#sent == 0)
    op('vfx', {mesh = 'potion', bone = 'head'}); check(#sent == 0)
    op('vfx', {mesh = 'pipe', bone = 'left'}); check(#sent == 0)
    op('sound', {sound = 'shatter', variant = 4, volume = 1, pitch = 1}); check(#sent == 0)
    op('sound', {sound = 'shatter', variant = 2, volume = 1, pitch = 1}); check(#sent == 2)
    check(sent[1].data.variant == sent[2].data.variant)
end)
test('no partial action to late join or newly loaded observer', function()
    reset(); start(); sent = {}; players[4].cell = 'INT:A'
    players[#players + 1] = {guid = 5, cell = 'INT:A'}
    op('removeVfx'); check(#sent == 2)
    players[2].cell = 'INT:B'; sent = {}; op('removeVfx'); check(#sent == 1); check(sent[1].guid == 3)
end)
test('session expiry and disconnect remove transient state without persistence', function()
    reset(); start(); sent = {}; now = now + 31; module.engineHandlers.onUpdate()
    op('removeVfx'); check(#sent == 0); check(not module.engineHandlers.onSave)
    start({token = 101}); sent = {}; module.eventHandlers.OnPlayerDisconnect({guid = 1})
    op('removeVfx', {token = 101}); check(#sent == 0)
end)
test('rate limit and per-action operation budget', function()
    reset(); start(); sent = {}
    for _ = 1, 100 do op('removeVfx') end
    check(#sent == 48)
    sent = {}; start({token = 101}); check(#sent == 0)
    now = now + 10; start({token = 102}); check(#sent == 2)
end)
test('token isolation, finish tombstone and no arbitrary operations', function()
    reset(); start(); sent = {}
    op('removeVfx', {token = 99}); op('execute'); check(#sent == 0)
    op('finish'); check(#sent == 2); sent = {}; op('removeVfx'); start(); check(#sent == 0)
end)
print(string.format('%d tests, %d assertions passed', tests, assertions))
