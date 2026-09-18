-- Decorates the installed script; resolution, random choices, timers and local
-- controls still run in the original mod. There is no second onConsume handler.
return function(loadOriginal, player)
    local mp = require('openmw.mp')
    local self = require('openmw.self')
    local core = require('openmw.core')
    local anim = require('openmw.animation')
    local async = require('openmw.async')
    local types = require('openmw.types')
    local I = require('openmw.interfaces')
    local storage = player and require('openmw.storage') or nil
    local resources = require('scripts.multiplayer.fetcher.consuming-animated-mp.client.resources')
    local function proxy(base, overrides) return setmetatable(overrides, { __index = base }) end
    local active, itemId, token = nil, nil, 0
    local npcAudio = { enabled = true, volume = 1, pitch = 1 }
    local function playerAnimationSpeed()
        if not storage then return 1 end
        local section = storage.playerSection('Settings_PotionAnimations_General')
        local timing = section and section:get('TIMING')
        local speed = timing and tonumber(timing.SPEED)
        return speed and speed > 0 and speed or 1
    end
    local function owner()
        return player or (not tostring(self.recordId):match('^mp_remote_') and mp.hasActorAuthorityForObject(self))
    end
    local function localAllowed() return not mp.isConnected() or owner() end
    local function send(op, fields)
        if not active or not mp.isConnected() or not owner() then return end
        local data = fields or {}
        data.version, data.op, data.token = 1, op, active.token
        data.actorId, data.cell = active.actorId, active.cell
        mp.sendToServer('PotionAnim_ConsumePresentation', data)
    end
    local function finish()
        send('finish')
        active = nil
    end
    local function begin(group, options)
        if not resources.available or not mp.isConnected() or not owner() or not itemId then return end
        local kind = resources.groups[group]
        if not kind then return end -- unknown custom groups require an explicit server policy
        local actorId = not player and mp.getActorInstanceId(self) or nil
        local cell = mp.getActorAuthorityCell(self)
        if not player and (not actorId or actorId <= 0 or not cell) then return end
        finish()
        token = math.max(token + 1, math.floor(core.getRealTime() * 1000))
        active = { token = token, actorId = actorId, cell = not player and cell or nil }
        local start = { recordId = itemId, kind = kind, group = group, speed = options.speed or 1, duration = 30 }
        if not player and group == 'potionl' then
            start.npcGulp = npcAudio.enabled
            start.gulpVolume = npcAudio.volume
            start.gulpPitch = npcAudio.pitch
        end
        send('start', start)
    end
    local function sound(key, index, options, stop)
        if key then
            options = options or {}
            send(stop and 'stopSound' or 'sound', { sound = key, variant = index or 1,
                volume = options.volume or 1, pitch = options.pitch or 1 })
        end
    end
    local soundProxy = proxy(core.sound, {
        playSoundFile3d = function(file, object, options)
            if not localAllowed() then return end
            local key, index = resources.soundKey(file)
            if object == self or object == self.object then sound(key, index, options) end
            return core.sound.playSoundFile3d(file, object, options)
        end,
        stopSoundFile3d = function(file, object)
            local key, index = resources.soundKey(file)
            if object == self or object == self.object then sound(key, index, nil, true) end
            return core.sound.stopSoundFile3d(file, object)
        end,
        playSound3d = function(id, object, options)
            if not localAllowed() then return end
            if id == 'drink' and (object == self or object == self.object) then sound('drink', 1, options) end
            return core.sound.playSound3d(id, object, options)
        end,
    })
    local controller = proxy(I.AnimationController, {
        playBlendedAnimation = function(group, options)
            if not localAllowed() then return end
            local result = I.AnimationController.playBlendedAnimation(group, options)
            begin(group, options)
            if not player and group == 'potionl' and active and npcAudio.enabled then
                local myToken = active.token
                local speed = options.speed or 1
                local volume, pitch = npcAudio.volume, npcAudio.pitch
                async:newUnsavableSimulationTimer(0.7 / speed, function()
                    -- Do not tie the local gulp to `active`: the watcher may emit its
                    -- stop key before this timer while the consume token is still current.
                    if token == myToken and localAllowed() then
                        core.sound.playSound3d('drink', self, { volume = volume, pitch = pitch })
                    end
                end)
            end
            return result
        end,
        addTextKeyHandler = function(group, callback)
            I.AnimationController.addTextKeyHandler(group, function(name, key)
                if localAllowed() then callback(name, key) end
                if key == 'stop' then finish() end
            end)
        end,
    })
    local base = loadOriginal({
        ['openmw.interfaces'] = proxy(I, { AnimationController = controller }),
        ['openmw.core'] = proxy(core, {
            sound = soundProxy,
            sendGlobalEvent = function(name, data)
                if name == 'PotionAnim_PlayerDrinkEnd' then finish() end
                if player and name == 'PotionAnim_SettingsUpdated' and type(data) == 'table' then
                    local forwarded = {}
                    for key, value in pairs(data) do forwarded[key] = value end
                    forwarded.MP_PLAYER_ANIMATION_SPEED = playerAnimationSpeed()
                    data = forwarded
                end
                return core.sendGlobalEvent(name, data)
            end,
        }),
        ['openmw.types'] = proxy(types, { Actor = proxy(types.Actor, {
            setEquipment = function(...)
                if localAllowed() then return types.Actor.setEquipment(...) end
            end,
        }) }),
        ['openmw.animation'] = proxy(anim, {
            addVfx = function(object, mesh, options)
                if not localAllowed() then return end
                if active and options and options.vfxId == 'potionanim_drink' then
                    local key = resources.meshKey(mesh, itemId)
                    local bone = options.boneName == 'Shield Bone' and 'left'
                        or options.boneName == 'Weapon Bone' and 'right'
                    if key and bone then send('vfx', { mesh = key, bone = bone }) end
                end
                return anim.addVfx(object, mesh, options)
            end,
            removeVfx = function(object, id)
                if id == 'potionanim_drink' then send('removeVfx') end
                return anim.removeVfx(object, id)
            end,
        }),
        ['openmw.async'] = proxy(async, {
            -- async is a native userdata; its methods require the original receiver.
            callback = function(_, ...) return async:callback(...) end,
            newUnsavableSimulationTimer = function(_, delay, callback)
                return async:newUnsavableSimulationTimer(delay, function()
                    if localAllowed() then callback() end
                    if not player then finish() end -- watcher has only its end fallback timer
                end)
            end,
        }),
    })
    if not player then
        local settingsUpdated = base.eventHandlers.PotionAnim_SettingsUpdated
        if settingsUpdated then
            base.eventHandlers.PotionAnim_SettingsUpdated = function(data)
                if type(data) == 'table' then
                    local forwarded = {}
                    for key, value in pairs(data) do forwarded[key] = value end
                    local playerSpeed = tonumber(forwarded.MP_PLAYER_ANIMATION_SPEED)
                    if playerSpeed and playerSpeed > 0 then forwarded.NPC_ANIMATION_SPEED = playerSpeed end
                    forwarded.MP_PLAYER_ANIMATION_SPEED = nil
                    npcAudio.enabled = forwarded.NPC_SOUND_ENABLE ~= false
                    npcAudio.volume = math.max(0, math.min(1, (tonumber(forwarded.NPC_SOUND_VOLUME) or 100) / 100))
                    npcAudio.pitch = math.max(0.5, math.min(2, (tonumber(forwarded.NPC_SOUND_PITCH) or 100) / 100))
                    data = forwarded
                end
                return settingsUpdated(data)
            end
        end
    end
    local consume = base.engineHandlers.onConsume
    base.engineHandlers.onConsume = function(item)
        if not localAllowed() then return end
        itemId = item and item.recordId:lower()
        local result = consume(item)
        -- The engine applies potion magic before the queued onConsume handler runs.
        -- Relay only a semantic marker here; observers resolve the already-validated
        -- potion record locally and replay its presentation without applying gameplay.
        if item and types.Potion.objectIsInstance(item) then send('effects') end
        return result
    end
    if player then
        local play = base.interface.playForItem
        base.interface.playForItem = function(item, options)
            itemId = item and item.recordId:lower()
            return play(item, options)
        end
    end
    for _, name in ipairs({'onInactive', 'onLoad'}) do
        local handler = base.engineHandlers[name]
        base.engineHandlers[name] = function(...)
            finish()
            if handler then return handler(...) end
        end
    end
    return base
end
