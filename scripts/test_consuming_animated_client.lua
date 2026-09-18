-- Executes the installed, unmodified mod in a deterministic API harness.
-- lua scripts/test_consuming_animated_client.lua "<OpenMWConsumingAnimated directory>"
local modRoot = assert(arg[1], 'Pass the installed OpenMWConsumingAnimated directory')
local root = 'files/server/server-lua-packages/fetcher.consuming-animated-mp/'
local prefix = 'scripts.multiplayer.fetcher.consuming-animated-mp.client.'
local function loadEnv(path, env)
    local fn = assert(loadfile(path)); setfenv(fn, env); return fn()
end
local assertions = 0
local function check(value, message) assertions = assertions + 1; assert(value, message) end
local function harness(player, recordId, options)
    options = options or {}
    local state = { time = 100, events = {}, globalEvents = {}, timers = {}, text = {}, animations = {}, sounds = {}, magicEffects = {}, equipmentWrites = 0,
        connected = true, authority = true }
    local self = { recordId = player and 'player' or 'npc_test', controls = {} }
    self.object = self
    function self:isValid() return true end
    local item = { recordId = recordId, ingredient = options.ingredient }
    function item:isValid() return true end
    local records = setmetatable({}, {__index = function(_, id)
        local effects = {}
        if id == 'potion_test' then
            effects = {{id = 'restore health', duration = 0, effect = {continuousVfx = false}}}
        end
        return {model = 'meshes/' .. id .. '.nif', effects = effects}
    end})
    local inventory = {}
    if options.pipe then inventory[1] = {recordId = options.pipe} end
    local function recordSound(file) state.sounds[#state.sounds + 1] = {file = file, time = state.time} end
    local function equipment() return {} end
    local modules = {}
    modules['openmw.self'] = self
    modules['openmw.mp'] = {
        isConnected = function() return state.connected end,
        hasActorAuthorityForObject = function() return state.authority end,
        getActorInstanceId = function() return 7 end,
        getActorAuthorityCell = function() return 'INT:A' end,
        playMagicEffectPresentation = function(_, effectId)
            state.magicEffects[#state.magicEffects + 1] = effectId
            return true
        end,
        sendToServer = function(_, data) state.events[#state.events + 1] = {data = data, time = state.time} end,
    }
    modules['openmw.core'] = {
        getRealTime = function() return state.time end, getSimulationTime = function() return state.time end,
        contentFiles = {has = function() return true end}, sendGlobalEvent = function(name, data)
            state.globalEvents[#state.globalEvents + 1] = {name = name, data = data}
        end,
        sound = {playSoundFile3d = recordSound, playSound3d = recordSound,
            stopSoundFile3d = function() end, stopSound3d = function() end},
    }
    modules['openmw.animation'] = {
        BONE_GROUP = {LeftArm = 1, Torso = 2, RightArm = 3}, PRIORITY = {Scripted = 10},
        BLEND_MASK = {LeftArm = 1, Torso = 2, RightArm = 4},
        addVfx = function() end, removeVfx = function() end, cancel = function() end,
    }
    modules['openmw.async'] = {
        callback = function(receiver, fn)
            assert(receiver == modules['openmw.async'], 'native async receiver identity'); return fn
        end,
        newUnsavableSimulationTimer = function(receiver, delay, fn)
            assert(receiver == modules['openmw.async'], 'native timer receiver identity')
            state.timers[#state.timers + 1] = {time = state.time + delay, fn = fn}
        end,
    }
    modules['openmw.storage'] = {playerSection = function(name)
        return {asTable = function() return options.settings or {} end,
            subscribe = function() end, get = function(_, key)
                if key == 'TIMING' then return options.playerTiming or {COOLDOWN = 2, SPEED = 1} end
                if key == 'USER_MOD_SPECIFIC_ANIM' then return options.custom or {} end
            end}
    end}
    modules['openmw.types'] = {
        Potion = {records = records, objectIsInstance = function(o) return not o.ingredient end},
        Ingredient = {records = records, objectIsInstance = function(o) return o.ingredient end},
        Actor = {isDead = function() return false end, getEquipment = equipment,
            setEquipment = function() state.equipmentWrites = state.equipmentWrites + 1 end,
            getStance = function() return 0 end, setStance = function() end, STANCE = {Nothing = 0},
            EQUIPMENT_SLOT = {CarriedLeft = 1}, inventory = function()
                return {getAll = function() return inventory end, find = function() return true end}
            end},
    }
    modules['openmw.ambient'] = {}
    modules['openmw.input'] = {getRangeActionValue = function() return 0 end}
    modules['openmw.camera'] = {getMode = function() return 1 end, setMode = function() end,
        MODE = {FirstPerson = 1, ThirdPerson = 2, Preview = 3}}
    modules['openmw.interfaces'] = {
        AnimationController = {
            playBlendedAnimation = function(group, opts) state.animations[#state.animations + 1] = {group = group, options = opts} end,
            addTextKeyHandler = function(group, fn) state.text[group] = fn end,
        },
        Controls = {overrideCombatControls = function() end, overrideMovementControls = function() end},
        Camera = {getPrimaryMode = function() return 1 end, enableStandingPreview = function() end,
            disableStandingPreview = function() end},
    }
    local function environment(proxies)
        local env = setmetatable({}, {__index = _G})
        local cache = {}
        env.require = function(name)
            if proxies and proxies[name] then return proxies[name] end
            if modules[name] then return modules[name] end
            if cache[name] then return cache[name] end
            local path
            if name:sub(1, #prefix) == prefix then path = root .. 'client/' .. name:sub(#prefix + 1) .. '.lua'
            else path = modRoot .. '/' .. name:gsub('%.', '/') .. '.lua' end
            cache[name] = loadEnv(path, env)
            return cache[name]
        end
        return env
    end
    local env = environment()
    local wrap = loadEnv(root .. 'client/source.lua', env)
    local base = wrap(function(proxies)
        return loadEnv(modRoot .. '/scripts/potionanim_' .. (player and 'player' or 'watcher') .. '.lua', environment(proxies))
    end, player)
    if base.engineHandlers.onInit then base.engineHandlers.onInit() end
    if not player then base.eventHandlers.PotionAnim_SettingsUpdated({NPC_ENABLE = true,
        NPC_ANIMATION_COOLDOWN = 2, NPC_ANIMATION_SPEED = 3, MP_PLAYER_ANIMATION_SPEED = 1,
        NPC_SOUND_ENABLE = true, NPC_SOUND_VOLUME = 100, NPC_SOUND_PITCH = 100}) end
    function state.consume() base.engineHandlers.onConsume(item) end
    function state.advance(seconds)
        local target = state.time + seconds
        while true do
            table.sort(state.timers, function(a, b) return a.time < b.time end)
            if not state.timers[1] or state.timers[1].time > target then break end
            local timer = table.remove(state.timers, 1); state.time = timer.time; timer.fn()
        end
        state.time = target
    end
    state.base, state.modules, state.env = base, modules, env
    return state
end
local function event(state, op)
    for _, entry in ipairs(state.events) do if entry.data.op == op then return entry end end
end
for _, case in ipairs({
    {'potion_test', {}, 'potionl', 'potion', 'drink'},
    {'ingred_test', {ingredient = true}, 'eatingr', 'item', 'eat'},
    {'potion_local_brew_01', {}, 'drinkbone', 'beverage', 'beverage'},
    {'potion_local_liquor_01', {}, 'drinkbone', 'beverage', 'beverage'},
    {'potion_skooma_01', {}, 'drinkbone', 'beverage', 'beverage'},
    {'potion_t_bug_musk_01', {}, 'bugmusk2', 'bugmusk', 'bugmusk'},
    {'custom_test', {custom = {custom_test = 'skooma pipe'}}, 'skoomapipe', 'skooma', 'skooma'},
    {'custom_test', {custom = {custom_test = 'pipe'}}, 'smokepipe1', 'pipe', 'smoke'},
    {'custom_test', {custom = {custom_test = 'cigar'}}, 'smoke1r', 'cigar', 'cigar'},
}) do
    local state = harness(true, case[1], case[2]); state.consume()
    check(#state.animations == 1, 'exactly one local animation')
    check(event(state, 'start').data.group == case[3], case[1] .. ' group')
    check(event(state, 'start').data.kind == case[5], case[1] .. ' category')
    check(event(state, 'vfx').data.mesh == case[4], case[1] .. ' semantic mesh')
    if not case[2].ingredient then check(event(state, 'effects') ~= nil, case[1] .. ' potion effects marker')
    else check(event(state, 'effects') == nil, case[1] .. ' ingredient has no potion effects marker') end
    check(state.animations[1].options.blendMask == (case[3] == 'potionl' and 3 or 7))
    state.advance(31)
    check(event(state, 'sound') ~= nil, 'sound emitted')
    check(event(state, 'finish') ~= nil, 'fallback cleanup')
    check(state.base.interface.isAnimating() == false)
    local resources = state.env.require(prefix .. 'resources')
    local selected = event(state, 'sound')
    local file = selected.data.sound == 'drink' and 'drink' or resources.sound(selected.data.sound, selected.data.variant)
    check(file == state.sounds[1].file, 'actual source sound selection preserved')
    check(selected.time == state.sounds[1].time, 'actual source sound timing preserved')
    print('PASS original player ' .. case[3])
end
do
    local state = harness(true, 'potion_test'); state.connected = false; state.consume(); state.advance(31)
    check(#state.events == 0); check(#state.animations == 1); print('PASS disconnected original behavior')
    state = harness(true, 'potion_test', {settings = {ENABLE = false}}); state.consume()
    check(#state.events == 0); check(#state.animations == 0); print('PASS disabled original behavior')
    state = harness(false, 'potion_test'); state.consume()
    check(#state.animations == 1); check(event(state, 'start').data.actorId == 7)
    check(state.animations[1].options.speed == 1, 'NPC animation normalized to player speed')
    check(event(state, 'start').data.npcGulp == true, 'NPC start carries gulp presentation metadata')
    state.advance(0.69); check(#state.sounds == 0, 'NPC gulp does not fire early')
    state.advance(0.02); check(#state.sounds == 1 and state.sounds[1].file == 'drink', 'NPC authority plays vanilla gulp locally')
    check(math.abs(state.sounds[1].time - 100.7) < 0.001, 'NPC gulp uses player timing point')
    state.advance(3.29)
    check(event(state, 'finish') ~= nil); print('PASS original NPC authority playback with normalized speed and local gulp')
    state.consume(); state.advance(4)
    check(#state.animations == 2, 'NPC can drink repeatedly after simulation cooldown')
    print('PASS repeated NPC consumption after cooldown')
    state = harness(false, 'potion_test'); state.authority = false; state.consume()
    check(#state.animations == 0); check(#state.events == 0); check(state.equipmentWrites == 0)
    print('PASS non-authority NPC suppressed')
    state = harness(false, 'potion_test'); state.consume(); state.authority = false; state.advance(4)
    check(#state.sounds == 0); print('PASS NPC loses authority during fallback')
end
do
    local state = harness(true, 'potion_test')
    local calls = {}
    local animation = state.modules['openmw.animation']
    animation.playBlended = function(_, group, options) calls[#calls + 1] = {group = group, options = options} end
    animation.addVfx = function(_, mesh, options) calls.mesh, calls.bone = mesh, options.boneName end
    animation.removeVfx = function() calls.removed = true end
    animation.cancel = function() calls.cancelled = true end
    state.modules['openmw.types'].Actor.setEquipment = function() error('receiver wrote equipment') end
    local receiver = loadEnv(root .. 'client/receiver.lua', state.env)
    local receive = receiver.eventHandlers.ConsumingAnimatedMP_Play
    receive({op = 'start', token = 1, sourceGuid = 2, group = 'potionl', kind = 'drink',
        recordId = 'potion_test', speed = 1.5, duration = 30})
    check(#calls == 1); check(calls[1].options.blendMask == 3); check(calls[1].options.speed == 1.5)
    receive({op = 'vfx', token = 1, sourceGuid = 2, mesh = 'potion', bone = 'left'})
    check(calls.mesh:find('_3rd.nif', 1, true) ~= nil); check(calls.bone == 'Shield Bone')
    receive({op = 'effects', token = 1, sourceGuid = 2})
    check(#state.magicEffects == 1 and state.magicEffects[1] == 'restore health', 'native potion magic presentation replayed')
    receive({op = 'sound', token = 1, sourceGuid = 2, sound = 'shatter', variant = 3, volume = 0.6, pitch = 1})
    check(#state.sounds == 1)
    receive({op = 'finish', token = 1, sourceGuid = 2}); check(calls.removed and calls.cancelled)
    local soundsBeforeNpcGulp = #state.sounds
    receive({op = 'start', token = 9, sourceGuid = 2, group = 'potionl', kind = 'drink',
        recordId = 'potion_test', speed = 1, duration = 30, npcGulp = true, gulpVolume = 0.8, gulpPitch = 1.1})
    state.advance(0.7)
    check(#state.sounds == soundsBeforeNpcGulp + 1 and state.sounds[#state.sounds].file == 'drink',
        'receiver schedules NPC gulp directly from validated start')
    receive({op = 'finish', token = 9, sourceGuid = 2})
    receive({op = 'start', token = 2, sourceGuid = 2, group = 'eatingr', kind = 'eat',
        recordId = 'ingred_test', speed = 1, duration = 30})
    check(calls[#calls].options.blendMask == 7)
    calls.removed = false
    receive({op = 'finish', token = 1, sourceGuid = 2}); check(not calls.removed)
    state.connected = false; receiver.engineHandlers.onUpdate(); check(calls.removed)
    check(#state.events == 0, 'receiver never relays')
    check(receiver.engineHandlers.onSave == nil, 'receiver never persists')
    print('PASS receiver partial blending, semantic VFX, sound, stale token and disconnect cleanup')
end
print(assertions .. ' assertions passed against installed original scripts and receiver')
