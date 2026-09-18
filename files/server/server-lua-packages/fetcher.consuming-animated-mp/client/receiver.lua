local mp = require('openmw.mp')
local self = require('openmw.self')
local core = require('openmw.core')
local async = require('openmw.async')
local anim = require('openmw.animation')
local types = require('openmw.types')
local resources = require('scripts.multiplayer.fetcher.consuming-animated-mp.client.resources')
local id = 'fetcher_consuming_animated_mp'
local active
local gulpSerial = 0
local effectTimers = {}
local function removeVfx() anim.removeVfx(self, id) end
local function cleanup()
    if not active then return end
    removeVfx()
    anim.cancel(self, active.group)
    for _, file in pairs(active.sounds) do core.sound.stopSoundFile3d(file, self) end
    core.sound.stopSound3d('drink', self)
    active = nil
end
local function clearEffectVfx()
    for effectId in pairs(effectTimers) do anim.removeVfx(self, effectId) end
    effectTimers = {}
end
local function cleanupAll()
    gulpSerial = gulpSerial + 1
    cleanup()
    clearEffectVfx()
end
local function playPotionEffects(recordId)
    local potion = types.Potion.records[recordId]
    if not potion then return end
    local now = core.getRealTime()
    for _, params in ipairs(potion.effects or {}) do
        local effect = params.effect
        if effect and mp.playMagicEffectPresentation(self, params.id)
            and effect.continuousVfx and params.duration > 0 then
            effectTimers[params.id] = math.max(effectTimers[params.id] or 0, now + params.duration)
        end
    end
end
local function play(data)
    if not mp.isConnected() or not resources.available then cleanup(); return end
    if data.op == 'start' then
        if resources.groups[data.group] ~= data.kind then return end
        cleanup()
        active = { token = data.token, sourceGuid = data.sourceGuid, group = data.group,
            recordId = data.recordId, expires = core.getRealTime() + data.duration, sounds = {} }
        local priority = { [anim.BONE_GROUP.LeftArm] = anim.PRIORITY.Scripted,
            [anim.BONE_GROUP.Torso] = anim.PRIORITY.Scripted }
        local mask = anim.BLEND_MASK.LeftArm + anim.BLEND_MASK.Torso
        if data.group ~= 'potionl' then
            mask = mask + anim.BLEND_MASK.RightArm
            priority[anim.BONE_GROUP.RightArm] = anim.PRIORITY.Scripted
        end
        anim.playBlended(self, data.group, { startKey = 'start', stopKey = 'stop', priority = priority,
            autoDisable = true, blendMask = mask, speed = data.speed })
        if data.npcGulp then
            gulpSerial = gulpSerial + 1
            local serial = gulpSerial
            local volume, pitch = data.gulpVolume, data.gulpPitch
            async:newUnsavableSimulationTimer(0.7 / data.speed, function()
                -- A normal finish must not cancel the gulp; only a newer consume does.
                if serial == gulpSerial and mp.isConnected() then
                    core.sound.playSound3d('drink', self, { volume = volume, pitch = pitch })
                end
            end)
        else
            gulpSerial = gulpSerial + 1
        end
        return
    end
    if not active or active.token ~= data.token or active.sourceGuid ~= data.sourceGuid then return end
    if data.op == 'finish' then
        -- Short one-shot sounds (including the shatter emitted just before finish)
        -- must finish naturally. Only pipe sounds are stopped by explicit events.
        removeVfx()
        anim.cancel(self, active.group)
        active = nil
    elseif data.op == 'removeVfx' then removeVfx()
    elseif data.op == 'effects' then playPotionEffects(active.recordId)
    elseif data.op == 'vfx' then
        local mesh = resources.mesh(data.mesh, active.recordId)
        if mesh then
            removeVfx()
            anim.addVfx(self, mesh, { loop = true, vfxId = id,
                boneName = data.bone == 'left' and 'Shield Bone' or 'Weapon Bone', useAmbientLight = false })
        end
    elseif data.op == 'sound' or data.op == 'stopSound' then
        local options = { volume = data.volume, pitch = data.pitch }
        if data.sound == 'drink' then
            if data.op == 'sound' then core.sound.playSound3d('drink', self, options)
            else core.sound.stopSound3d('drink', self) end
        else
            local file = resources.sound(data.sound, data.variant)
            if file then
                if data.op == 'sound' then
                    active.sounds[data.sound .. data.variant] = file
                    core.sound.playSoundFile3d(file, self, options)
                else core.sound.stopSoundFile3d(file, self) end
            end
        end
    end
end
return {
    engineHandlers = {
        onUpdate = function()
            local now = core.getRealTime()
            if not mp.isConnected() or types.Actor.isDead(self) then cleanupAll(); return end
            if active and now > active.expires then cleanup() end
            for effectId, expires in pairs(effectTimers) do
                if now >= expires then
                    anim.removeVfx(self, effectId)
                    effectTimers[effectId] = nil
                end
            end
        end,
        onInactive = cleanupAll, onLoad = cleanupAll,
    },
    eventHandlers = { ConsumingAnimatedMP_Play = play, Died = cleanupAll },
}
