local animation = require('openmw.animation')
local core = require('openmw.core')
local mp = require('openmw.mp')
local I = require('openmw.interfaces')
local storage = require('openmw.storage')
local types = require('openmw.types')
local world = require('openmw.world')

local IE = require('scripts.ArrowStick.utils.impactEffects')
local consts = require('scripts.ArrowStick.utils.consts')

local settings = storage.globalSection('SettingsArrowStick')
local settingsImpactEffects = storage.globalSection('SettingsArrowStick_impactEffects')
local arrowDespawnScript = 'scripts/ArrowStick/customArrow.lua'
local fProjectileThrownStoreChance = core.getGMST('fProjectileThrownStoreChance')

local ignoredProjectiles = {
    sw_blastbolt = true,
}

local shotArrows = {}
local attachedArrowSerial = 0

local function isSupportedProjectile(record)
    return record and (
        record.type == types.Weapon.TYPE.Arrow
        or record.type == types.Weapon.TYPE.Bolt
        or record.type == types.Weapon.TYPE.MarksmanThrown
    )
end

local function shouldStick(data, record)
    if not data.presentationOnly and not types.Player.objectIsInstance(data.caster) then return false end
    if ignoredProjectiles[string.lower(data.projectile)] then return false end
    if not isSupportedProjectile(record) then return false end

    if not settings:get('stickEnchanted') and record.enchant then
        return false
    end

    local chance = settings:get('stickChance')
    if chance == nil then chance = 100 end
    if chance < 0 then chance = fProjectileThrownStoreChance end
    return math.random() <= chance / 100
end

local function impactMaterial(data)
    if not I.impactEffects then return nil end
    return IE.getMaterial(data.target, data.hitWater)
end

local function spawnImpactEffect(data, material)
    if not I.impactEffects or not settingsImpactEffects:get('impactEffects') then return end
    I.impactEffects.spawnEffect({
        material = material,
        hitPos = data.presentationHitPos or data.hitPos,
    })
end

local function placeWorldProjectile(data, material)
    if data.hitWater and not settings:get('stickUnderwater') then return end
    if material and settingsImpactEffects:get('checkMaterial') and consts.unstickableMaterials[material] then return end

    if mp.isConnected() and mp.worldProjectileRecover and mp.worldProjectileRecover.isAvailable() then
        -- Multiplayer recovery must be server-owned. The server consumes one
        -- short-lived credit created by the actual ranged release, allocates a
        -- durable mpNum, persists the world item, and broadcasts ObjectPlace.
        mp.worldProjectileRecover.request(data.projectile, data.hitPos, data.rotation)
        return
    end

    local newArrow = world.createObject(data.projectile)
    newArrow:teleport(data.caster.cell.name, data.hitPos, data.rotation)

    if settings:get('despawnArrows') then
        newArrow:addScript(arrowDespawnScript)
        shotArrows[newArrow.id] = newArrow
    end

    core.sendGlobalEvent('ArrowStick_ArrowPlaced', {
        arrowSticked = true,
        item = newArrow,
        material = material,
    })
end

local function attachProjectileToActor(data, record)
    if not data.target or not (types.NPC.objectIsInstance(data.target) or types.Creature.objectIsInstance(data.target)) then
        return false
    end
    if not record.model or record.model == '' then return true end

    attachedArrowSerial = attachedArrowSerial + 1
    animation.addVfx(data.target, record.model, {
        vfxId = 'ArrowStick_' .. tostring(attachedArrowSerial),
        loop = true,
        useAmbientLight = false,
        autoTransform = false,
        worldTransform = data.presentationTransform or data.transform,
        attachToNearestBone = true,
        -- Prefer the renderer-refined visual surface point. If refinement misses,
        -- retain a small amount of the Bullet-hit direction instead of snapping
        -- to the bone pivot (the head pivot sits around the mouth on common TES3 rigs).
        nearestBoneOffsetScale = data.presentationRefined and 1 or 0.15,
    })
    return true
end

local function projectileImpact(data)
    local record = types.Weapon.record(data.projectile)
    if not shouldStick(data, record) then return end

    local material = impactMaterial(data)
    spawnImpactEffect(data, material)

    if attachProjectileToActor(data, record) then return end
    if data.presentationOnly then return end
    placeWorldProjectile(data, material)
end

local function arrowPlaced(eventData)
    if I.impactEffects and I.impactEffects.playSoundEffect then
        I.impactEffects.playSoundEffect({
            material = eventData.material,
            soundTarget = eventData.item,
        })
    end
end

local function onSave()
    return { shotArrows = shotArrows }
end

local function onLoad(saveData)
    shotArrows = saveData.shotArrows or {}
end

local function onActivate(obj)
    if shotArrows[obj.id] then
        shotArrows[obj.id] = nil
    end
end

local function arrowInactive(id)
    local arrow = shotArrows[id]
    shotArrows[id] = nil
    if not arrow or not arrow:isValid() then return end
    arrow:remove()
    arrow:removeScript(arrowDespawnScript)
end

return {
    engineHandlers = {
        onActivate = onActivate,
        onSave = onSave,
        onLoad = onLoad,
    },
    eventHandlers = {
        ProjectileImpact = projectileImpact,
        ArrowStick_ArrowInactive = arrowInactive,
        ArrowStick_ArrowPlaced = arrowPlaced,
    },
}
