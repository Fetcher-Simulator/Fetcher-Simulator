-- devilish_cliffracer_global.lua
-- Performance optimized for OpenMW 0.51.
--
-- Key behavior change is internal only:
-- * managed birds are discovered with onActorActive
-- * world.activeActors is scanned once after load, not every 0.5 seconds
-- * cached birds are processed only while in actors processing range
-- * when no referenced birds are active, onUpdate returns immediately
-- * the quest global is polled only while a managed bird is active
--
local world = require('openmw.world')
local util = require('openmw.util')
local types = require('openmw.types')
local mpAvailable, mp = pcall(require, 'openmw.mp')

local config = {
    globalName = 'detd_ylbert_cliffracer',

    leadBirdRecordId = 'detd_ylbert_leadbird',
    followBirdRecordId = 'detd_ylbert_followbird',

    -- Leadbird route mechanic.
    catchDistance = 300,
    jumpDistance = 500,
    routeUpdateInterval = 0.05,

    -- Performance: the script no longer scans world.activeActors repeatedly.
    -- Managed birds are discovered through onActorActive and one startup scan.
    -- This timer only prunes cached references that left actor processing range.
    activePruneInterval = 1.00,

    -- The quest global does not need to be read every rendered frame. While one
    -- of this script's birds is actually active, refresh it at this cadence.
    globalPollInterval = 0.25,

    -- At the beginning, both birds are placed at the same start point,
    -- then the leadbird is immediately placed this far ahead on the route.
    initialLeadDistance = 500,

    -- Lua-driven followbird movement.
    moveFollowBirdByLua = true,

    -- Units per second.
    followBirdSpeed = 900,

    -- Followbird stops this far from the leadbird.
    followBirdStopDistance = 120,

    -- Prevents huge one-frame movement spikes.
    maxFrameMoveDistance = 120,

    -- If leadbird gets too far away, place it near the followbird again.
    maxLeadDistanceFromFollowBird = 2500,

    -- When leashing, put leadbird this far ahead of followbird along route direction.
    leashForwardDistance = 600,

    -- Player mount mechanic.
    attachPlayerToFollowBird = true,

    -- player z 8501.457031 minus followbird z 7876.332520 = 625.124511
    playerVerticalOffset = 625.124511,

    playerForwardOffset = 0,
    playerSideOffset = 0,

    playerFollowStrength = 30,
    playerSnapDistance = 900,

    matchPlayerRotationToFollowBird = false,

    snapBirdsAndPlayerToStart = true,

    completeGlobalValue = 2,

    debug = false,

    ----------------------------------------------------------------
    -- RUIN CIRCLING CLIFFRACERS
    ----------------------------------------------------------------

    -- Any active cliffracer with this recordId will circle the ruin.
    circlingCliffracerRecordId = 'detd_cliffracer_circlin',

    -- Match detd_cliffracer_circle, the legacy script attached to these
    -- creatures. It enables them only for the second skooma trip effect.
    circlingActiveGlobalName = 'detd_skooma_high_2',
    circlingActiveGlobalValue = 1,

    -- ActorSync interpolates the authority's samples for observers. Moving at
    -- a fixed cadence avoids rebuilding actor transforms every rendered frame.
    circlingUpdateInterval = 0.05,
    circlingMinMoveDistance = 4,

    -- The orbit occupies exterior cells (6,-51) and (6,-50). Keeping the
    -- driver within roughly one exterior cell of the ruin prevents cached
    -- actors from being pushed beyond the active scene at the grid fringe.
    circlingPlayerActivationDistance = 8000,

    circlingCenter = util.vector3(
        53601.628906,
        -409641.687500,
        5814.215820
    ),

    circlingRadius = 2700,
    circlingHeightOffset = 0,

    circlingSpeed = 0.35,

    circlingBobAmount = 450,
    circlingBobSpeed = 1.4,

    circlingRadiusWobbleAmount = 350,
    circlingRadiusWobbleSpeed = 0.7,

    circlingFaceDirection = true,
}

local route = {
    util.vector3(51697.054688, -409533.031250, 4500.420898),
    util.vector3(43998.988281, -415037.218750, 5339.236816),
    util.vector3(35836.671875, -419749.062500, 7620.186523),
    util.vector3(24981.541016, -423528.750000, 8901.177734),
    util.vector3(7051.644043, -425588.093750, 10261.681641),
    util.vector3(-6387.708496, -420246.156250, 10876.345703),
    util.vector3(-22373.277344, -412985.531250, 10015.304688),
    util.vector3(-31614.214844, -405214.937500, 9864.573242),
    util.vector3(-42781.023438, -394500.687500, 10246.874023),
    util.vector3(-50574.390625, -388169.500000, 9522.113281),
    util.vector3(-55687.101562, -384795.781250, 7803.076660),
}

----------------------------------------------------------------
-- PRECOMPUTED CONSTANTS / ROUTE DATA
----------------------------------------------------------------
local TWO_PI = math.pi * 2
local EXTERIOR_CELL_NAME = ''

local LEAD_BIRD_RECORD_ID = string.lower(config.leadBirdRecordId)
local FOLLOW_BIRD_RECORD_ID = string.lower(config.followBirdRecordId)
local CIRCLING_RECORD_ID = string.lower(config.circlingCliffracerRecordId)

local CATCH_DISTANCE_SQ = config.catchDistance * config.catchDistance
local MAX_LEAD_DISTANCE_SQ =
    config.maxLeadDistanceFromFollowBird * config.maxLeadDistanceFromFollowBird
local PLAYER_SNAP_DISTANCE_SQ = config.playerSnapDistance * config.playerSnapDistance
local CIRCLING_MIN_MOVE_DISTANCE_SQ =
    config.circlingMinMoveDistance * config.circlingMinMoveDistance
local CIRCLING_PLAYER_ACTIVATION_DISTANCE_SQ =
    config.circlingPlayerActivationDistance
        * config.circlingPlayerActivationDistance

local PLAYER_MOUNT_OFFSET = util.vector3(
    config.playerSideOffset,
    config.playerForwardOffset,
    config.playerVerticalOffset
)

local routeSegments = {}
local routeLength = 0

for i = 1, #route - 1 do
    local a = route[i]
    local b = route[i + 1]
    local vector = b - a
    local length = vector:length()

    routeSegments[i] = {
        a = a,
        vector = vector,
        length = length,
        lengthSq = length * length,
        startDistance = routeLength,
        endDistance = routeLength + length,
    }

    routeLength = routeLength + length
end

local state = {
    started = false,
    complete = false,
    distanceAlongRoute = 0,
    routeLength = routeLength,
    routeTimer = 0,

    circlingTimer = 0,
    circlingUpdateTimer = 0,

    -- Cached quest-global state. It is refreshed only while a managed bird is
    -- active instead of calling getGlobalVariables() every rendered frame.
    globalValue = 0,
    circlingGlobalValue = 0,
    globalKnown = false,
    globalPollTimer = 0,

    -- One active-actor scan is used after load to catch actors that were already
    -- active before this global script received onActorActive callbacks.
    needsInitialScan = true,

    -- onActorActive maintains these references afterward.
    leadBird = nil,
    followBird = nil,
    circlingCliffracers = {},
    circlingByObjectId = {},

    -- Cheap stale-reference pruning. No recurring world.activeActors traversal.
    activePruneTimer = 0,

    -- Rebuilt only when the number of circling birds changes.
    circlingPhaseCount = -1,
    circlingPhases = {},
}

local function logDebug(text)
    if config.debug then
        print('[detd leadbird route] ' .. text)
    end
end

local function getGlobals()
    return world.mwscript.getGlobalVariables()
end

local function getQuestGlobal(globals)
    globals = globals or getGlobals()
    return globals[config.globalName] or 0
end

local function getCirclingGlobal(globals)
    globals = globals or getGlobals()
    return globals[config.circlingActiveGlobalName] or 0
end

local function setQuestGlobal(value)
    local globals = getGlobals()
    globals[config.globalName] = value

    -- Keep the frame cache synchronized immediately when this script changes it.
    state.globalValue = value
    state.globalKnown = true
    state.globalPollTimer = 0
end

local function getPlayer()
    return world.players and world.players[1] or nil
end

local function clearArray(array)
    for i = #array, 1, -1 do
        array[i] = nil
    end
end

local function actorIsInProcessingRange(actor)
    return actor
        and actor:isValid()
        and types.Actor.isInActorsProcessingRange(actor)
end

local function invalidateCirclingPhases()
    state.circlingPhaseCount = -1
end

local function registerManagedActor(actor)
    if not actor or not actor:isValid() or not actor.recordId then
        return false
    end

    local recordId = string.lower(actor.recordId)

    if recordId == LEAD_BIRD_RECORD_ID then
        state.leadBird = actor
        return true
    end

    if recordId == FOLLOW_BIRD_RECORD_ID then
        state.followBird = actor
        return true
    end

    if recordId == CIRCLING_RECORD_ID then
        if types.Actor.isDead(actor) then
            return false
        end

        local objectId = actor.id
        if objectId and state.circlingByObjectId[objectId] then
            return true
        end

        local list = state.circlingCliffracers
        list[#list + 1] = actor

        if objectId then
            state.circlingByObjectId[objectId] = true
        end

        invalidateCirclingPhases()
        return true
    end

    return false
end

local function scanManagedActorsOnce()
    state.leadBird = nil
    state.followBird = nil
    clearArray(state.circlingCliffracers)
    state.circlingByObjectId = {}

    for _, actor in ipairs(world.activeActors) do
        registerManagedActor(actor)
    end

    state.needsInitialScan = false
    invalidateCirclingPhases()
end

local function pruneManagedActors()
    if state.leadBird and not actorIsInProcessingRange(state.leadBird) then
        state.leadBird = nil
    end

    if state.followBird and not actorIsInProcessingRange(state.followBird) then
        state.followBird = nil
    end

    local list = state.circlingCliffracers
    local changed = false

    for i = #list, 1, -1 do
        local actor = list[i]

        if not actorIsInProcessingRange(actor) or types.Actor.isDead(actor) then
            if actor and actor.id then
                state.circlingByObjectId[actor.id] = nil
            end

            table.remove(list, i)
            changed = true
        end
    end

    if changed then
        invalidateCirclingPhases()
    end
end

local function managedRouteBirdsAreActive()
    return actorIsInProcessingRange(state.leadBird)
        and actorIsInProcessingRange(state.followBird)
end

local function anyCirclingBirdIsActive()
    local list = state.circlingCliffracers

    for i = 1, #list do
        if actorIsInProcessingRange(list[i]) then
            return true
        end
    end

    return false
end

local function refreshQuestGlobals(dt)
    state.globalPollTimer = state.globalPollTimer + dt

    if state.globalKnown and state.globalPollTimer < config.globalPollInterval then
        return state.globalValue, state.circlingGlobalValue
    end

    state.globalPollTimer = 0
    local globals = getGlobals()
    state.globalValue = getQuestGlobal(globals)
    state.circlingGlobalValue = getCirclingGlobal(globals)
    state.globalKnown = true
    return state.globalValue, state.circlingGlobalValue
end

local function getPointAtDistance(distanceAlongRoute)
    if distanceAlongRoute <= 0 then
        return route[1]
    end

    for i = 1, #routeSegments do
        local segment = routeSegments[i]

        if distanceAlongRoute <= segment.endDistance then
            local t = (distanceAlongRoute - segment.startDistance) / segment.length
            return segment.a + segment.vector * t
        end
    end

    return route[#route]
end

local function getNearestRouteDistance(position)
    local bestDistanceAlongRoute = 0
    local bestDistanceToRouteSq = math.huge

    for i = 1, #routeSegments do
        local segment = routeSegments[i]

        if segment.lengthSq > 0 then
            local toPosition = position - segment.a
            local t = toPosition:dot(segment.vector) / segment.lengthSq

            if t < 0 then
                t = 0
            elseif t > 1 then
                t = 1
            end

            local closestPoint = segment.a + segment.vector * t
            local difference = position - closestPoint
            local distanceToRouteSq = difference:dot(difference)

            if distanceToRouteSq < bestDistanceToRouteSq then
                bestDistanceToRouteSq = distanceToRouteSq
                bestDistanceAlongRoute = segment.startDistance + segment.length * t
            end
        end
    end

    return bestDistanceAlongRoute
end

local function getRouteDirectionAtDistance(distanceAlongRoute)
    -- Keep the original 100-unit look-ahead behavior, including corner blending.
    local currentPoint = getPointAtDistance(distanceAlongRoute)
    local nextPoint = getPointAtDistance(distanceAlongRoute + 100)
    local direction = nextPoint - currentPoint

    if direction:length() <= 0 then
        return util.vector3(0, 1, 0)
    end

    return direction:normalize()
end

local function teleportObject(object, position, rotation)
    if not object or not object:isValid() or not object.enabled then
        return
    end

    object:teleport(EXTERIOR_CELL_NAME, position, {
        rotation = rotation or object.rotation,
    })
end

local function canMoveAuthorityOwnedActor(actor)
    if not mpAvailable or not mp.isConnected or not mp.isConnected() then
        return true
    end

    return mp.hasActorAuthorityForObject
        and mp.hasActorAuthorityForObject(actor)
end

local function getMountedPlayerPosition(followBird)
    return followBird.position + PLAYER_MOUNT_OFFSET
end

local function softAttachPlayerToFollowBird(player, followBird, dt, forceSnap)
    if not config.attachPlayerToFollowBird then
        return
    end

    if not player or not player:isValid() then
        return
    end

    if not followBird or not followBird:isValid() then
        return
    end

    local targetPosition = getMountedPlayerPosition(followBird)
    local currentPosition = player.position
    local difference = targetPosition - currentPosition
    local distanceSq = difference:dot(difference)

    local finalPosition

    if forceSnap or distanceSq >= PLAYER_SNAP_DISTANCE_SQ then
        finalPosition = targetPosition
    else
        local t = 1 - math.exp(-config.playerFollowStrength * dt)

        if t < 0 then
            t = 0
        elseif t > 1 then
            t = 1
        end

        finalPosition = currentPosition + difference * t
    end

    local rotation = player.rotation

    if config.matchPlayerRotationToFollowBird then
        rotation = followBird.rotation
    end

    teleportObject(player, finalPosition, rotation)
end

local function moveFollowBirdTowardLeadBird(followBird, leadBird, dt)
    if not config.moveFollowBirdByLua then
        return
    end

    if not followBird or not followBird:isValid() then
        return
    end

    if not leadBird or not leadBird:isValid() then
        return
    end

    local currentPosition = followBird.position
    local difference = leadBird.position - currentPosition
    local distance = difference:length()

    if distance <= config.followBirdStopDistance then
        return
    end

    local moveDistance = config.followBirdSpeed * dt

    if moveDistance > config.maxFrameMoveDistance then
        moveDistance = config.maxFrameMoveDistance
    end

    local allowedDistance = distance - config.followBirdStopDistance

    if moveDistance > allowedDistance then
        moveDistance = allowedDistance
    end

    local newPosition = currentPosition + difference * (moveDistance / distance)
    teleportObject(followBird, newPosition)
end

local function startRoute(leadBird, followBird, player)
    state.started = true
    state.complete = false
    state.routeTimer = 0
    state.distanceAlongRoute = config.initialLeadDistance

    local startPosition = route[1]
    local leadStartPosition = getPointAtDistance(state.distanceAlongRoute)

    if config.snapBirdsAndPlayerToStart then
        teleportObject(leadBird, startPosition)
        teleportObject(followBird, startPosition)

        if player and player:isValid() then
            teleportObject(player, startPosition + PLAYER_MOUNT_OFFSET)
        end

        teleportObject(leadBird, leadStartPosition)
    end

    logDebug('route started; followbird and player at start, leadbird placed ahead')
end

local function completeRoute(leadBird, followBird, player)
    state.complete = true
    state.started = false
    state.distanceAlongRoute = state.routeLength

    teleportObject(leadBird, route[#route])

    if followBird and followBird:isValid() then
        softAttachPlayerToFollowBird(player, followBird, 1, true)
    end

    setQuestGlobal(config.completeGlobalValue)
    logDebug('route complete')
end

local function advanceLeadBird(leadBird, followBird, player)
    state.distanceAlongRoute = state.distanceAlongRoute + config.jumpDistance

    if state.distanceAlongRoute >= state.routeLength then
        completeRoute(leadBird, followBird, player)
        return
    end

    teleportObject(leadBird, getPointAtDistance(state.distanceAlongRoute))
    logDebug('leadbird jumped to route distance ' .. tostring(state.distanceAlongRoute))
end

local function leashLeadBirdNearFollowBird(leadBird, followBird)
    local followPosition = followBird.position
    local followDistanceAlongRoute = getNearestRouteDistance(followPosition)

    if followDistanceAlongRoute > state.distanceAlongRoute then
        state.distanceAlongRoute = followDistanceAlongRoute
    end

    local direction = getRouteDirectionAtDistance(state.distanceAlongRoute)
    local newLeadPosition = followPosition + direction * config.leashForwardDistance
    local routePosition = getPointAtDistance(state.distanceAlongRoute)

    newLeadPosition = util.vector3(
        newLeadPosition.x,
        newLeadPosition.y,
        routePosition.z
    )

    teleportObject(leadBird, newLeadPosition)
    state.distanceAlongRoute = getNearestRouteDistance(newLeadPosition)

    logDebug('leadbird leashed back near followbird')
end

local function resetRouteState()
    if not state.started
        and not state.complete
        and state.distanceAlongRoute == 0
        and state.routeTimer == 0
    then
        return
    end

    state.started = false
    state.complete = false
    state.distanceAlongRoute = 0
    state.routeTimer = 0
end

local function updateRoute(dt, globalValue, leadBird, followBird, player)
    if globalValue ~= 1 then
        resetRouteState()
        return
    end

    -- onUpdate already verified both cached route birds are currently in the
    -- actors processing range, so avoid repeating those engine calls here.
    if not leadBird or not followBird then
        return
    end

    if not player then
        logDebug('player not found')
        return
    end

    if not state.started then
        startRoute(leadBird, followBird, player)
        return
    end

    if state.complete then
        return
    end

    moveFollowBirdTowardLeadBird(followBird, leadBird, dt)
    softAttachPlayerToFollowBird(player, followBird, dt, false)

    local birdDifference = followBird.position - leadBird.position
    local birdDistanceSq = birdDifference:dot(birdDifference)

    if birdDistanceSq > MAX_LEAD_DISTANCE_SQ then
        leashLeadBirdNearFollowBird(leadBird, followBird)
        return
    end

    state.routeTimer = state.routeTimer + dt

    if state.routeTimer < config.routeUpdateInterval then
        return
    end

    state.routeTimer = 0

    if birdDistanceSq <= CATCH_DISTANCE_SQ then
        advanceLeadBird(leadBird, followBird, player)
    end
end

----------------------------------------------------------------
-- RUIN CIRCLING CLIFFRACERS
----------------------------------------------------------------
local function circlingCliffracersShouldRun(globalValue)
    return (tonumber(globalValue) or 0) == config.circlingActiveGlobalValue
end

local function playerIsNearCirclingRuin(player)
    if not player
        or not player:isValid()
        or not player.cell
        or not player.cell.isExterior
    then
        return false
    end

    local dx = player.position.x - config.circlingCenter.x
    local dy = player.position.y - config.circlingCenter.y
    return dx * dx + dy * dy <= CIRCLING_PLAYER_ACTIVATION_DISTANCE_SQ
end

local function rebuildCirclingPhases(count)
    if state.circlingPhaseCount == count then
        return
    end

    clearArray(state.circlingPhases)
    state.circlingPhaseCount = count

    local safeCount = count < 1 and 1 or count

    for index = 1, count do
        local phase = ((index - 1) / safeCount) * TWO_PI
        state.circlingPhases[index] = {
            phase = phase,
            sinPhase = math.sin(phase),
            cosPhase = math.cos(phase),
        }
    end
end

local function handleCirclingCliffracers(dt, globalValue, cliffracers)
    if not circlingCliffracersShouldRun(globalValue) then
        state.circlingUpdateTimer = 0
        return
    end

    state.circlingTimer = state.circlingTimer + dt
    state.circlingUpdateTimer = state.circlingUpdateTimer + dt

    if state.circlingUpdateTimer < config.circlingUpdateInterval then
        return
    end

    state.circlingUpdateTimer =
        state.circlingUpdateTimer % config.circlingUpdateInterval

    local count = #cliffracers
    if count == 0 then
        return
    end

    rebuildCirclingPhases(count)

    -- Calculate each time-varying sine/cosine pair once per frame.
    -- Per-bird values are then obtained with angle-addition identities.
    local orbitBase = state.circlingTimer * config.circlingSpeed
    local wobbleBase = state.circlingTimer * config.circlingRadiusWobbleSpeed
    local bobBase = state.circlingTimer * config.circlingBobSpeed

    local orbitSin = math.sin(orbitBase)
    local orbitCos = math.cos(orbitBase)
    local wobbleSin = math.sin(wobbleBase)
    local wobbleCos = math.cos(wobbleBase)
    local bobSin = math.sin(bobBase)
    local bobCos = math.cos(bobBase)

    local center = config.circlingCenter

    for index = 1, count do
        local cliffracer = cliffracers[index]

        if actorIsInProcessingRange(cliffracer)
            and not types.Actor.isDead(cliffracer)
            and cliffracer.enabled
            and canMoveAuthorityOwnedActor(cliffracer)
        then
            local phaseData = state.circlingPhases[index]
            local sinPhase = phaseData.sinPhase
            local cosPhase = phaseData.cosPhase

            local sinAngle = orbitSin * cosPhase + orbitCos * sinPhase
            local cosAngle = orbitCos * cosPhase - orbitSin * sinPhase

            local sinWobble = wobbleSin * cosPhase + wobbleCos * sinPhase
            local radius = config.circlingRadius
                + sinWobble * config.circlingRadiusWobbleAmount

            local sinBob = bobSin * cosPhase + bobCos * sinPhase
            local zBob = sinBob * config.circlingBobAmount

            local position = util.vector3(
                center.x + sinAngle * radius,
                center.y + cosAngle * radius,
                center.z + config.circlingHeightOffset + zBob
            )

            local rotation = nil
            if config.circlingFaceDirection then
                rotation = util.transform.rotateZ(
                    orbitBase + phaseData.phase + math.pi / 2
                )
            end

            local movement = position - cliffracer.position
            if movement:dot(movement) >= CIRCLING_MIN_MOVE_DISTANCE_SQ then
                teleportObject(cliffracer, position, rotation)
            end
        end
    end
end

local function onActorActive(actor)
    -- This is the normal discovery path. It costs essentially nothing for
    -- unrelated actors beyond checking their record ID once when they activate.
    registerManagedActor(actor)
end

local function onPlayerAdded()
    -- A save may already contain active birds before this script receives their
    -- activation callbacks, so perform exactly one startup reconciliation scan.
    state.needsInitialScan = true
    state.globalKnown = false
    state.globalPollTimer = 0
end

local function onUpdate(dt)
    if state.needsInitialScan then
        scanManagedActorsOnce()
    end

    -- Ultra-cheap idle path. Once the startup scan is complete, onActorActive
    -- will wake this system automatically when one of our three record IDs loads.
    if not state.leadBird
        and not state.followBird
        and #state.circlingCliffracers == 0
    then
        return
    end

    -- Remove references that left the actors processing range. This touches only
    -- our tiny cached bird set and never traverses world.activeActors.
    state.activePruneTimer = state.activePruneTimer + dt
    if state.activePruneTimer >= config.activePruneInterval then
        state.activePruneTimer = state.activePruneTimer % config.activePruneInterval
        pruneManagedActors()
    end

    local routeBirdsActive = managedRouteBirdsAreActive()
    local circlingBirdsActive = anyCirclingBirdIsActive()
    local player = getPlayer()
    local circlingShouldDrive = circlingBirdsActive and playerIsNearCirclingRuin(player)

    -- Critical idle path: when none of this script's route actors are active and
    -- the player is not near the circling ruin, do no global reads, route math,
    -- trigonometry, teleports, or actor scans.
    if not routeBirdsActive and not circlingShouldDrive then
        return
    end

    local globalValue, circlingGlobalValue = refreshQuestGlobals(dt)

    if routeBirdsActive then
        updateRoute(
            dt,
            globalValue,
            state.leadBird,
            state.followBird,
            player
        )
    elseif globalValue ~= 1 then
        -- Keep route state coherent if the quest is reset while the route birds
        -- are not both present.
        resetRouteState()
    end

    if circlingShouldDrive then
        handleCirclingCliffracers(
            dt,
            circlingGlobalValue,
            state.circlingCliffracers
        )
    end
end

return {
    engineHandlers = {
        onUpdate = onUpdate,
        onActorActive = onActorActive,
        onPlayerAdded = onPlayerAdded,
    },
}
