local types = require('openmw.types')
local I = require('openmw.interfaces')
local world = require('openmw.world')
local core = require('openmw.core')
local util = require('openmw.util')
local mp = require('openmw.mp')

local CellUtils = require('scripts.InventoryExtender.util.cell')

local helpers = require('scripts.InventoryExtender.util.helpers')

local disposedBodies = {}

---@type {func: function, frameDelay: number}[]
local delayedJobs = {}
local function queueDelayedJob(func, frameDelay)
    table.insert(delayedJobs, { func = func, frameDelay = frameDelay })
end

local function isStealing(actor, ownerInfo)
    if not ownerInfo then
        return false
    end
    local victim = {}
    if ownerInfo.recordId and ownerInfo.recordId ~= actor.recordId then
        local isDead = disposedBodies[ownerInfo.recordId] or false
        for _, actor in ipairs(world.activeActors) do
            if actor.recordId == ownerInfo.recordId and types.Actor.isDead(actor) then
                isDead = true
                break
            end
        end

        if not isDead then
            victim.recordId = ownerInfo.recordId
        end
    end

    if ownerInfo.factionId then
        local reqRank = ownerInfo.factionRank or 1
        if types.NPC.getFactionRank(actor, ownerInfo.factionId) < reqRank then
            victim.factionId = ownerInfo.factionId
        end
    end
    return next(victim) and victim or false
end

local function teleport(props)
    if not props.obj or not props.position then return end
    local obj = props.obj
    local count = props.count or obj.count
    if helpers.isGold(obj) then
        obj = props.source.type.inventory(props.source):find('gold_001') or obj
    end

    if count ~= obj.count then
        obj = obj:split(count)
    end
    local cellOrName = props.cell or ''
    local position = props.position
    local options = props.options

    if props.dropping then
        local mwscript = world.mwscript.getLocalScript(obj)
        if mwscript and mwscript.variables.onpcdrop then
            mwscript.variables.onpcdrop = 1
        end
    end

    obj:teleport(cellOrName, position, options)
    core.sendGlobalEvent('IE_PostTeleport', {
        obj = obj,
        position = position,
        cell = cellOrName,
        options = options,
        player = props.player,
        UIPlayer=props.UIPlayer-----------Côme
    })
end

local function postTeleport(props)
    local obj = props.obj
    local box = obj:getBoundingBox()
    local localCenter = box.center - obj.position
    local offset = util.vector3(0, 0, box.halfSize.z - localCenter.z)

    obj:teleport(obj.cell, obj.position + offset)
--print("1", props.player)
    props.UIPlayer:sendEvent('IE_Update')
end

local function isCompanion(npc)
    local script = world.mwscript.getLocalScript(npc)
    return script and script.variables and script.variables.companion ~= 0
end

local function getCompanionProfit(npc)
    local script = world.mwscript.getLocalScript(npc)
    if script then
        return script.variables.minimumprofit
    end
    return nil
end

local function commitPickpocket(props)
    if not props.player or not props.target then return end

    I.Crimes.commitCrime(props.player, {
        type = types.Player.OFFENSE_TYPE.Pickpocket,
        victim = props.target,
        victimAware = props.victimAware,
    })
end

local function scriptedDragStart(props)
    if props.obj.parentContainer == nil then return end

    props.player:sendEvent('IE_SetDraggingObject', {
        obj = props.resultingObj,
        target = props.destination,
        resetMode = true,
    })
--     print("2", props.player)
    props.player:sendEvent('IE_Update')
end

local authoritativeDragInFlight = {}

local function getAuthoritativeDragKey(source, obj)
    return source.id .. '\0' .. obj.id
end

local function snapshotDestinationRecord(destination, recordId)
    local counts = {}
    for _, item in ipairs(destination.type.inventory(destination):findAll(recordId)) do
        counts[item.id] = item.count
    end
    return counts
end

local function resolveCurrentCorpseItem(source, obj)
    if not source or not types.Actor.objectIsInstance(source) or not types.Actor.isDead(source) then
        return obj
    end
    local candidates = source.type.inventory(source):findAll(obj.recordId)
    if #candidates == 1 then
        return candidates[1]
    end
    return obj
end

local function resolveAuthoritativeDestinationStack(destination, recordId, previousCounts, expectedIncrease)
    local candidates = destination.type.inventory(destination):findAll(recordId)
    local changedCandidate
    local newCandidate
    local newCandidateCount = 0

    for _, item in ipairs(candidates) do
        local previousCount = previousCounts[item.id]
        if previousCount ~= nil then
            local increase = item.count - previousCount
            if expectedIncrease > 0 and increase >= expectedIncrease then
                return item
            end
            if increase > 0 and not changedCandidate then
                changedCandidate = item
            end
        else
            newCandidate = item
            newCandidateCount = newCandidateCount + 1
        end
    end

    if changedCandidate then
        return changedCandidate
    end
    if newCandidateCount == 1 then
        return newCandidate
    end
    if #candidates == 1 then
        return candidates[1]
    end
    return nil
end

local function moveInto(props)
    if not props.obj or not props.destination then return end
    local obj = props.obj
    local destination = props.destination
    local recordId = obj.recordId
    local moveCount = props.count or obj.count

    if mp.isConnected() and props.source == nil
        and types.Player.objectIsInstance(destination)
        and obj.parentContainer == nil and obj.cell ~= nil
        and mp.worldItemTake and mp.worldItemTake.isAvailable() then
        local dragKey = 'world\0' .. obj.id
        if authoritativeDragInFlight[dragKey] then
            return
        end
        authoritativeDragInFlight[dragKey] = true
        local previousCounts = snapshotDestinationRecord(destination, recordId)
        local dragStart = props.dragStart
        local player = props.player
        local ok, requestError = pcall(function()
            mp.worldItemTake.request(obj, function(result)
                authoritativeDragInFlight[dragKey] = nil
                if result.accepted and dragStart then
                    local authoritativeObj = resolveAuthoritativeDestinationStack(
                        destination,
                        result.itemRefId or recordId,
                        previousCounts,
                        result.itemCount or moveCount)
                    if authoritativeObj then
                        player:sendEvent('IE_SetDraggingObject', {
                            obj = authoritativeObj,
                            target = destination,
                        })
                    else
                        print(('[MP] InventoryExtender authoritative world pickup could not resolve destination stack recordId=%s request=%s')
                            :format(result.itemRefId or recordId, result.requestId or 'unknown'))
                    end
                end
                player:sendEvent('IE_Update')
            end)
        end)
        if not ok then
            authoritativeDragInFlight[dragKey] = nil
            error(requestError)
        end
        return
    end

    local resultingObj = obj
    local destinationInv = destination.type.inventory(destination)

    local isEquipped = false
    if props.source and types.Actor.objectIsInstance(props.source) then
        isEquipped = props.source.type.hasEquipped(props.source, obj)
    end

    if not helpers.isGold(obj) then
        for _, item in ipairs(destinationInv:findAll(recordId)) do
            if not destination.type.hasEquipped or not destination.type.hasEquipped(destination, item) and not isEquipped then
                if helpers.itemCanStack(item, obj) then
                    resultingObj = item
                    break
                end
            end
        end
    else
        if props.source and props.source ~= props.destination then
            local sourceInv = props.source.type.inventory(props.source)
            obj = sourceInv:find('gold_001') or obj
        end
        resultingObj = destinationInv:find('gold_001') or resultingObj
    end

    local sourceIsCompanion = props.source and isCompanion(props.source)
    local ownerInfo
    if props.pickpocket and props.pickpocket.target then
        ownerInfo = {
            recordId = props.pickpocket.target.recordId,
        }
    elseif sourceIsCompanion then
        ownerInfo = nil
    elseif props.source and types.Container.objectIsInstance(props.source) then
        ownerInfo = props.source.owner
    else
        ownerInfo = obj.owner
    end
    local victimInfo = isStealing(props.player, ownerInfo)
    local victimActor
    if victimInfo and victimInfo.recordId then
        for _, actor in ipairs(world.activeActors) do
            if actor.recordId == victimInfo.recordId then
                victimActor = actor
                break
            end
        end
    end

    if mp.isConnected() and props.source and props.source ~= props.destination
        and types.Player.objectIsInstance(props.destination)
        and (types.Container.objectIsInstance(props.source) or types.Actor.objectIsInstance(props.source))
        and mp.inventoryTake and mp.inventoryTake.isAvailable() then
        obj = resolveCurrentCorpseItem(props.source, obj)
        recordId = obj.recordId
        if props.count == nil then
            moveCount = obj.count
        end
        local stolenRecordId = obj.recordId
        local stolenCount = moveCount
        local stolenIsGold = helpers.isGold(obj)
        local pickpocket = props.pickpocket ~= nil and props.pickpocket ~= false
        local dragState

        if props.dragStart then
            local dragKey = getAuthoritativeDragKey(props.source, obj)
            if authoritativeDragInFlight[dragKey] then
                return
            end
            authoritativeDragInFlight[dragKey] = true
            dragState = {
                key = dragKey,
                previousCounts = snapshotDestinationRecord(destination, recordId),
            }
        end

        local ok, requestError = pcall(function()
            mp.inventoryTake.request(props.source, obj, moveCount, pickpocket, function(result)
                if dragState then
                    authoritativeDragInFlight[dragState.key] = nil
                end

                if result.accepted and result.theft and victimInfo and not stolenIsGold then
                    props.player:sendEvent('IE_StoleItem', {
                        recordId = stolenRecordId,
                        count = stolenCount,
                        victim = victimInfo,
                    })
                end

                if result.accepted and not result.detected and dragState then
                    local resultingObj = resolveAuthoritativeDestinationStack(
                        destination,
                        result.itemRefId or recordId,
                        dragState.previousCounts,
                        result.itemCount or moveCount)
                    if resultingObj then
                        props.player:sendEvent('IE_SetDraggingObject', {
                            obj = resultingObj,
                            target = destination,
                        })
                    else
                        print(('[MP] InventoryExtender authoritative drag result could not resolve destination stack recordId=%s request=%s')
                            :format(result.itemRefId or recordId, result.requestId or 'unknown'))
                    end
                end

                props.player:sendEvent('IE_Update')
            end)
        end)
        if not ok then
            if dragState then
                authoritativeDragInFlight[dragState.key] = nil
            end
            error(requestError)
        end
        return
    end

    local authoritativePutDestination = types.Container.objectIsInstance(props.destination)
        or (types.Actor.objectIsInstance(props.destination) and types.Actor.isDead(props.destination))
    if mp.isConnected() and props.source and props.source ~= props.destination
        and types.Player.objectIsInstance(props.source)
        and authoritativePutDestination
        and mp.inventoryPut and mp.inventoryPut.isAvailable() then
        local dragState
        if props.dragStart then
            local dragKey = getAuthoritativeDragKey(props.source, obj)
            if authoritativeDragInFlight[dragKey] then
                return
            end
            authoritativeDragInFlight[dragKey] = true
            dragState = {
                key = dragKey,
                previousCounts = snapshotDestinationRecord(destination, recordId),
            }
        end

        local ok, requestError = pcall(function()
            mp.inventoryPut.request(destination, obj, moveCount, function(result)
                if dragState then
                    authoritativeDragInFlight[dragState.key] = nil
                end
                if result.accepted and dragState then
                    local authoritativeObj = resolveAuthoritativeDestinationStack(
                        destination,
                        result.itemRefId or recordId,
                        dragState.previousCounts,
                        result.itemCount or moveCount)
                    if authoritativeObj then
                        props.player:sendEvent('IE_SetDraggingObject', {
                            obj = authoritativeObj,
                            target = destination,
                        })
                    else
                        print(('[MP] InventoryExtender authoritative put drag result could not resolve destination stack recordId=%s request=%s')
                            :format(result.itemRefId or recordId, result.requestId or 'unknown'))
                    end
                end
                props.player:sendEvent('IE_Update')
            end)
        end)
        if not ok then
            if dragState then
                authoritativeDragInFlight[dragState.key] = nil
            end
            error(requestError)
        end
        return
    end

    if victimInfo and not helpers.isGold(obj) then
        props.player:sendEvent('IE_StoleItem', { recordId = obj.recordId, count = moveCount, victim = victimInfo })
    end

    if types.Player.objectIsInstance(props.destination) and props.source ~= props.destination then
        local mwscript = world.mwscript.getLocalScript(obj)
        if mwscript and mwscript.variables.onpcadd then
            mwscript.variables.onpcadd = 1
        end
    end

    if props.dragStart and not props.source then
        local mwscript = obj.type.record(obj).mwscript
        if mwscript then
            local scriptRecord = core.mwscripts.records[mwscript]
            if scriptRecord and scriptRecord.text:lower():find('onactivate') then
                world._runStandardActivationAction(obj, props.player)
                core.sendGlobalEvent('IE_ScriptedDragStart', { player = props.player, obj = obj, resultingObj = resultingObj, target = destination })
                return
            end
        end
    end

    if victimInfo and not props.pickpocket then
        I.Crimes.commitCrime(props.player, {
            type = types.Player.OFFENSE_TYPE.Theft,
            arg = helpers.getItemValue(obj) * moveCount,
            faction = victimInfo.factionId,
            victim = victimActor,
        })
    end

    if props.source then
        local profit = getCompanionProfit(props.source)
        if profit then
            profit = profit - (helpers.getItemValue(obj) * moveCount)
            world.mwscript.getLocalScript(props.source).variables.minimumprofit = profit
            props.player:sendEvent('IE_CompanionProfit', {
                companion = props.source,
                profit = profit,
            })
        end
    end
    if props.destination ~= props.source then
        local profit = getCompanionProfit(props.destination)
        if profit then
            profit = profit + (helpers.getItemValue(obj) * moveCount)
            world.mwscript.getLocalScript(props.destination).variables.minimumprofit = profit
            props.player:sendEvent('IE_CompanionProfit', {
                companion = props.destination,
                profit = profit,
            })
        end
    end

    if moveCount ~= obj.count or helpers.isGold(obj) then
        obj = obj:split(moveCount)
        if props.source ~= props.destination then
            resultingObj = obj
        end
    end

    obj:moveInto(destination)

    if props.dragStart and helpers.isGold(obj) then
        local foundGold = destinationInv:find('gold_001')
        if foundGold then
            resultingObj = foundGold
        end
    end

    if props.dragStart then
        props.player:sendEvent('IE_SetDraggingObject', {
            obj = resultingObj,
            target = destination,
        })
    end

    if props.autoEquip then
        queueDelayedJob(function()
            props.autoEquip:addScript('scripts/InventoryExtender/autoEquip.lua', { player = props.player })
            props.autoEquip:removeScript('scripts/InventoryExtender/autoEquip.lua')
        end, 2)
    else
 --       print("3", props.player)
        props.player:sendEvent('IE_Update')
    end

    return obj
end

local function moveAll(props)
    if not props.source or not props.destination then return end

    local sourceInv = props.source.type.inventory(props.source)
    local items = props.items or sourceInv:getAll()

    local victimInfo = isStealing(props.player, props.source.owner)
    local victimActor
    if victimInfo then
        if victimInfo.recordId then
            for _, actor in ipairs(world.activeActors) do
                if actor.recordId == victimInfo.recordId then
                    victimActor = actor
                    break
                end
            end
        end
    end

    if mp.isConnected() and props.source ~= props.destination
        and types.Player.objectIsInstance(props.destination)
        and (types.Container.objectIsInstance(props.source) or types.Actor.objectIsInstance(props.source))
        and mp.inventoryTake and mp.inventoryTake.isAvailable() then
        for _, item in ipairs(items) do
            if types.Item.isCarriable(item) then
                local recordId = item.recordId
                local count = item.count
                local isGold = helpers.isGold(item)
                mp.inventoryTake.request(props.source, item, count, false, function(result)
                    if result.accepted and result.theft and victimInfo and not isGold then
                        props.player:sendEvent('IE_StoleItem', {
                            recordId = recordId,
                            count = count,
                            victim = victimInfo,
                        })
                    end
                    props.player:sendEvent('IE_Update')
                end)
            end
        end
        return
    end

    local totalCrimeValue = 0
    for _, item in ipairs(items) do
        if types.Item.isCarriable(item) then
            if victimInfo then
                if not helpers.isGold(item) then
                    props.player:sendEvent('IE_StoleItem', { recordId = item.recordId, count = item.count, victim = victimInfo })
                end
                totalCrimeValue = totalCrimeValue + (helpers.getItemValue(item) * item.count)
            end
            item:moveInto(props.destination)
        end
    end

    if victimInfo then
        I.Crimes.commitCrime(props.player, {
            type = types.Player.OFFENSE_TYPE.Theft,
            arg = totalCrimeValue,
            faction = victimInfo.factionId,
            victim = victimActor,
        })
    end
--print("4", props.player)
    props.player:sendEvent('IE_Update')
end

local checkStolenNextUpdate = {}
local checkStolenNextNextUpdate = {}
local checkStolenBooks = {}
local function onItemActivated(item, actor)
    if not types.Item.isCarriable(item) then return true end
    local victim = isStealing(actor, item.owner)
    if victim and not helpers.isGold(item) then
        if types.Book.objectIsInstance(item) then
            checkStolenBooks[actor.id] = checkStolenBooks[actor.id] or {}
            table.insert(checkStolenBooks[actor.id], { actor = actor, item = item, victim = victim, prevCount = actor.type.inventory(actor):countOf(item.recordId) })
        else
            checkStolenNextNextUpdate = checkStolenNextNextUpdate or {}
            checkStolenNextNextUpdate[actor.id] = checkStolenNextNextUpdate[actor.id] or {}
            table.insert(checkStolenNextNextUpdate[actor.id], { actor = actor, item = item, victim = victim, prevCount = actor.type.inventory(actor):countOf(item.recordId) })
        end
    end
end

local lastItemPickedUp = nil

local lastInteractedContainer = {}
local observedContainerRevisions = {}
local barterAuthorityRefresh = {}

local function refreshChangedOpenContainers()
    if not mp.isConnected() or not mp.containerUpdates then
        return
    end
    for actorId, entry in pairs(lastInteractedContainer) do
        local ok, revision = pcall(mp.containerUpdates.revision, entry.container)
        if ok then
            local previous = observedContainerRevisions[actorId]
            observedContainerRevisions[actorId] = revision
            if previous ~= nil and revision ~= previous then
                entry.actor:sendEvent('IE_Update')
            end
        end
    end

    for actorId, entry in pairs(barterAuthorityRefresh) do
        local allReady = true
        for index, source in ipairs(entry.sources) do
            local ok, revision = pcall(mp.containerUpdates.revision, source)
            if not ok then
                allReady = false
            else
                local initial = entry.initialRevisions[index]
                if initial == nil or revision == initial then
                    allReady = false
                end
            end
        end

        if allReady then
            entry.actor:sendEvent('IE_BarterAuthorityReady', { sources = entry.sources })
            barterAuthorityRefresh[actorId] = nil
        end
    end
end

local function onUpdate(dt)
    for i = #delayedJobs, 1, -1 do
        local job = delayedJobs[i]
        job.frameDelay = job.frameDelay - 1
        if job.frameDelay <= 0 then
            job.func()
            table.remove(delayedJobs, i)
        end
    end

    -- Multiplayer container reconciliation is applied outside InventoryExtender.
    -- Observe only the currently open container and coalesce any packet burst to
    -- one UI rebuild per frame.
    refreshChangedOpenContainers()

    if core.isWorldPaused() then
        return
    end

    if checkStolenNextUpdate and next(checkStolenNextUpdate) ~= nil then
        for actorId, entries in pairs(checkStolenNextUpdate) do
            for i = #entries, 1, -1 do
                local entry = entries[i]
                local actor = entry.actor
                local item = entry.item
                local victim = entry.victim
                local prevCount = entry.prevCount
                local currentCount = actor.type.inventory(actor):countOf(item.recordId)
                if currentCount > prevCount then
                    local stolenCount = currentCount - prevCount
                    actor:sendEvent('IE_StoleItem', { recordId = item.recordId, count = stolenCount, victim = victim })
                end
                table.remove(entries, i)
            end
            if #entries == 0 then
                checkStolenNextUpdate[actorId] = nil
            end
        end
    end

    checkStolenNextUpdate = checkStolenNextNextUpdate
    checkStolenNextNextUpdate = nil
end

local containerMemory = {}

local IE_LOCK_LEVEL = 49
local VANILLA_LOCK_LEVEL = 50

local function updateChestMemory(chest)
    if chest.cell.isExterior then return end
    if chest.recordId ~= 'stolen_goods' then return end
    local cellId = chest.cell.id:lower()
    containerMemory[cellId] = {}
    for _, item in ipairs(chest.type.inventory(chest):getAll()) do
        containerMemory[cellId][item.recordId] = (containerMemory[cellId][item.recordId] or 0) + item.count
    end
    if types.Lockable.getLockLevel(chest) == VANILLA_LOCK_LEVEL then
        types.Lockable.lock(chest, IE_LOCK_LEVEL)
    end
end

local function onUiModeChanged(data)
    if data.oldMode == 'Container' then
        local entry = lastInteractedContainer[data.actor.id]
        if entry then
            local container = entry.container
            if container.recordId == 'stolen_goods' then
                updateChestMemory(container)
            end
            lastInteractedContainer[data.actor.id] = nil
            observedContainerRevisions[data.actor.id] = nil
        end
    end
    if data.newMode == 'Container' then
        lastInteractedContainer[data.actor.id] = { actor = data.actor, container = data.arg }
        if mp.isConnected() and mp.containerUpdates then
            local ok, revision = pcall(mp.containerUpdates.revision, data.arg)
            observedContainerRevisions[data.actor.id] = ok and revision or nil
        end
    end

    if data.oldMode == 'Barter' then
        barterAuthorityRefresh[data.actor.id] = nil
    end
    if data.newMode == 'Barter' then
        if mp.isConnected() and mp.containerUpdates and mp.containerUpdates.requestBarterSources then
            local ok, sources = pcall(mp.containerUpdates.requestBarterSources, data.arg)
            if ok and type(sources) == 'table' and #sources > 0 then
                local entry = {
                    actor = data.actor,
                    sources = sources,
                    initialRevisions = {},
                }
                for index, source in ipairs(sources) do
                    local revisionOk, revision = pcall(mp.containerUpdates.revision, source)
                    entry.initialRevisions[index] = revisionOk and revision or nil
                end
                barterAuthorityRefresh[data.actor.id] = entry
            else
                -- Fail open only when no canonical source could be requested.
                -- The server still validates every transaction authoritatively.
                data.actor:sendEvent('IE_BarterAuthorityReady')
            end
        else
            data.actor:sendEvent('IE_BarterAuthorityReady')
        end
    end

    if data.oldMode == 'Book' or data.oldMode == 'Scroll' then
        if checkStolenBooks and checkStolenBooks[data.actor.id] then
            local entries = checkStolenBooks[data.actor.id]
            for i = #entries, 1, -1 do
                local entry = entries[i]
                local item = entry.item
                local victim = entry.victim
                local prevCount = entry.prevCount
                local currentCount = data.actor.type.inventory(data.actor):countOf(item.recordId)
                if currentCount > prevCount then
                    local stolenCount = currentCount - prevCount
                    data.actor:sendEvent('IE_StoleItem', { recordId = item.recordId, count = stolenCount, victim = victim })
                end
                table.remove(entries, i)
            end
            if #entries == 0 then
                checkStolenBooks[data.actor.id] = nil
            end
        end
    end
end

for _, type in pairs(types) do
    if type.baseType == types.Item then
        I.Activation.addHandlerForType(type, onItemActivated)
    end
end

local function initChestLocks()
    local prisonCells = CellUtils.getPrisonCells()
    for id in pairs(prisonCells) do
        local success, cell = pcall(world.getCellById, id)
        if success and cell then
            for _, container in ipairs(cell:getAll(types.Container)) do
                if container.recordId == 'stolen_goods' then
                    if types.Lockable.getLockLevel(container) == VANILLA_LOCK_LEVEL then
                        types.Lockable.lock(container, IE_LOCK_LEVEL)
                    end
                    break
                end
            end
        end
    end
end

local function confiscateStolenItems(data)
    local closestMarker = CellUtils.getClosestMarker(data.player)
    if not closestMarker then
        return
    end

    local success, cell = pcall(world.getCellById, closestMarker.interior)
    if not success or not cell then
        return
    end

    data.stolenMap = data.stolenMap or {}

    for _, container in ipairs(cell:getAll(types.Container)) do
        if container.recordId == 'stolen_goods' then
            if types.Lockable.getLockLevel(container) == VANILLA_LOCK_LEVEL then
                --print("Confiscating goods to", container)

                -- compare with memory
                local cellId = container.cell.id:lower()
                local memory = containerMemory[cellId] or {}
                local current = {}
                for _, item in ipairs(container.type.inventory(container):getAll()) do
                    current[item.recordId] = (current[item.recordId] or 0) + item.count
                end
                local delta = {}
                for recordId, count in pairs(current) do
                    local memCount = memory[recordId] or 0
                    if count > memCount then
                        delta[recordId] = count - memCount
                        --print("newly confiscated item:", recordId, "count:", delta[recordId])
                    end
                end

                local playerInv = data.player.type.inventory(data.player)
                for recordId, victimCounts in pairs(data.stolenMap) do
                    local totalToConfiscate = 0
                    for victimId, count in pairs(victimCounts) do
                        totalToConfiscate = totalToConfiscate + count
                    end
                    local totalConfiscated = delta[recordId] or 0
                    for _, item in ipairs(playerInv:findAll(recordId)) do
                        if totalConfiscated >= totalToConfiscate then
                            break
                        end
                        local itemCount = item.count
                        local toConfiscate = math.min(itemCount, totalToConfiscate - totalConfiscated)
                        local toMove = item
                        if toConfiscate < itemCount then
                            toMove = item:split(toConfiscate)
                        end
                        toMove:moveInto(container, toConfiscate)
                        current[recordId] = (current[recordId] or 0) + toConfiscate
                        totalConfiscated = totalConfiscated + toConfiscate
                    end
                    if (totalConfiscated - (delta[recordId] or 0)) > 0 then
                        --print("Script confiscated item:", recordId, "count:", (totalConfiscated - (delta[recordId] or 0)))
                    end
                    for victimId, count in pairs(victimCounts) do
                        if totalConfiscated >= count then
                            totalConfiscated = totalConfiscated - count
                            victimCounts[victimId] = nil
                        else
                            victimCounts[victimId] = count - totalConfiscated
                            break
                        end
                    end
                    if not next(victimCounts) then
                        data.stolenMap[recordId] = nil
                    end
                end

                containerMemory[cellId] = current

                types.Lockable.lock(container, IE_LOCK_LEVEL)
                data.player:sendEvent('IE_ItemsConfiscated', { stolenMap = data.stolenMap })
                return
            end
            break
        end
    end
end

local function confiscateToOwner(data)
    if not data.stolenMap[data.item.recordId] then
        return
    end

    local toRemove = math.min(data.count, data.stolenMap[data.item.recordId][data.victim.recordId] or 0)
    data.stolenMap[data.item.recordId][data.victim.recordId] = (data.stolenMap[data.item.recordId][data.victim.recordId] or 0) - toRemove
    if data.stolenMap[data.item.recordId][data.victim.recordId] <= 0 then
        data.stolenMap[data.item.recordId][data.victim.recordId] = nil
    end
    if not next(data.stolenMap[data.item.recordId]) then
        data.stolenMap[data.item.recordId] = nil
    end

    I.Crimes.commitCrime(data.player, {
        type = types.Player.OFFENSE_TYPE.Theft,
        arg = helpers.getItemValue(data.item) * toRemove,
        victim = data.victim,
        victimAware = true,
    })
    data.item:split(toRemove):moveInto(types.Actor.inventory(data.victim))
    data.player:sendEvent('IE_ItemsConfiscated', { stolenMap = data.stolenMap })
end

local function setItemData(item, data)
    local itemData = types.Item.itemData(item)
    if data.condition then
        itemData.condition = data.condition
    end
    if data.enchantmentCharge then
        itemData.enchantmentCharge = data.enchantmentCharge
    end
    if data.soul then
        itemData.soul = data.soul
    end
end

local function finalizeBarter(props)
    local function transferItems(entries, destination, restock)
        for _, entry in pairs(entries) do
            local remainingCount = entry.count

            if entry.count > 0 then
                if entry.stacks then
                    for _, stackItem in ipairs(entry.stacks) do
                        if remainingCount <= 0 then break end

                        local takeCount = math.min(stackItem.count, remainingCount)
                        local item = takeCount < stackItem.count and stackItem:split(takeCount) or stackItem

                        if restock and stackItem.parentContainer and types.Item.isRestocking(stackItem) then
                            local newItem = world.createObject(stackItem.recordId, takeCount)
                            setItemData(newItem, stackItem.type.itemData(stackItem))
                            newItem:moveInto(destination)
                        else
                            item:moveInto(destination)
                        end
                        remainingCount = remainingCount - takeCount
                    end
                else
                    local item = entry.count < entry.item.count and entry.item:split(entry.count) or entry.item

                    if restock and entry.item.parentContainer and types.Item.isRestocking(entry.item) then
                        local newItem = world.createObject(entry.item.recordId, entry.count)
                        setItemData(newItem, entry.item.type.itemData(entry.item))
                        newItem:moveInto(destination)
                    else
                        item:moveInto(destination)
                    end
                end
            end
        end
    end

    local function settleGold()
        local balance = props.barterState.currentBalance or 0
        if balance < 0 then
            local cost = -balance
            local inventory = types.Actor.inventory(props.player)
            local gold = inventory:find('gold_001')
            if not gold or gold.count < cost then
                return false, 'gold_changed'
            end
            gold:remove(cost)
            types.NPC.setBarterGold(props.merchant, types.NPC.getBarterGold(props.merchant) + cost)
        elseif balance > 0 then
            local gold = world.createObject('gold_001', balance)
            gold:moveInto(types.Actor.inventory(props.player))
            types.NPC.setBarterGold(props.merchant, types.NPC.getBarterGold(props.merchant) - balance)
        end
        return true
    end

    local function finishSuccess()
        if props.dispositionDelta and types.NPC.objectIsInstance(props.merchant) then
            types.NPC.modifyBaseDisposition(props.merchant, props.player, props.dispositionDelta)
        end
        props.player:sendEvent('IE_BarterFinalized', {
            skillGain = props.skillGain,
        })
    end

    local function failBarter(reason, rollbackFailed)
        print('[Fetcher InventoryExtender] authoritative barter aborted: ' .. tostring(reason)
            .. (rollbackFailed and ' (rollback incomplete)' or ''))
        props.player:sendEvent('IE_BarterAuthorityFailed', {
            reason = tostring(reason),
            rollbackFailed = rollbackFailed == true,
        })
        props.player:sendEvent('IE_Update')
    end

    local buying = props.barterState.buying or {}
    local selling = props.barterState.selling or {}
    local hasBuying = next(buying) ~= nil
    local hasSelling = next(selling) ~= nil

    -- Single-player and older/non-multiplayer runtimes retain InventoryExtender's
    -- original transaction path. Connected multiplayer sends the entire offer as
    -- one authoritative transaction: all buys, all sells, restocking semantics,
    -- player gold, merchant stock and the inventory revision commit together.
    if not mp.barter or not mp.barter.isAvailable() or not mp.barter.transaction then
        transferItems(selling, props.merchant, false)
        transferItems(buying, props.player, true)
        local settled, reason = settleGold()
        if not settled then
            failBarter(reason, false)
            return
        end
        finishSuccess()
        return
    end

    if not hasBuying and not hasSelling then
        finishSuccess()
        return
    end

    local lines = {}
    local preflightError = nil

    local function appendBuy(item, count)
        if not item or count <= 0 or preflightError then return end
        local source = item.parentContainer
        if not source then
            table.insert(lines, {
                kind = 4,
                source = item,
                item = item,
                count = count,
            })
            return
        end
        table.insert(lines, {
            kind = types.Item.isRestocking(item) and 2 or 1,
            source = source,
            item = item,
            count = count,
        })
    end

    local function appendSell(item, count)
        if not item or count <= 0 or preflightError then return end
        table.insert(lines, {
            kind = 3,
            item = item,
            count = count,
        })
    end

    local function appendEntries(entries, append)
        for _, entry in pairs(entries) do
            local remainingCount = entry.count
            if entry.stacks then
                for _, stackItem in ipairs(entry.stacks) do
                    if remainingCount <= 0 then break end
                    local moveCount = math.min(stackItem.count, remainingCount)
                    append(stackItem, moveCount)
                    remainingCount = remainingCount - moveCount
                end
                if remainingCount > 0 and not preflightError then
                    preflightError = 'stack_count_changed'
                end
            else
                append(entry.item, entry.count)
            end
        end
    end

    appendEntries(buying, appendBuy)
    appendEntries(selling, appendSell)
    if preflightError then
        failBarter(preflightError, false)
        return
    end

    local balance = props.barterState.currentBalance or 0
    local merchantGold = types.NPC.objectIsInstance(props.merchant)
        and types.NPC.getBarterGold(props.merchant) or 0
    local queued, queueError = pcall(function()
        mp.barter.transaction(props.merchant, lines, balance, merchantGold, function(result)
            if not result.accepted then
                failBarter('transaction_' .. tostring(result.error), false)
                return
            end
            if types.NPC.objectIsInstance(props.merchant) then
                types.NPC.setBarterGold(props.merchant, result.merchantGold)
            end
            props.player:sendEvent('IE_Update')
            finishSuccess()
        end)
    end)
    if not queued then
        failBarter('transaction_queue_failed:' .. tostring(queueError), false)
    end
end

local eventHandlers = {
    IE_TryConfiscate = confiscateStolenItems,
    IE_ConfiscateToOwner = confiscateToOwner,
    IE_CommitPickpocket = commitPickpocket,
    IE_Teleport = teleport,
    IE_PostTeleport = postTeleport,
    IE_ScriptedDragStart = scriptedDragStart,
    IE_MoveInto = moveInto,
    IE_MoveAll = moveAll,
    IE_FinalizeBarter = finalizeBarter,
    IE_UIModeChanged = onUiModeChanged,
    IE_StolenChestInactive = updateChestMemory,
    IE_DisposeOfCorpse = function(props)
        disposedBodies[props.target.recordId] = true
        props.target:remove()
    end,
    IE_ModDisposition = function(props)
        types.NPC.modifyBaseDisposition(props.target, props.player, props.amount)
    end,
    IE_ItemPickedUp = function(props)
        lastItemPickedUp = {
            recordId = props.recordId,
            count = props.count,
            player = props.player,
        }
        queueDelayedJob(function()
            lastItemPickedUp = nil
        end, 2)
    end,
    IE_OwnedItemInactive = function(props)
        if lastItemPickedUp then
            if props.recordId == lastItemPickedUp.recordId and props.owner then
                local victim = isStealing(lastItemPickedUp.player, props.owner)
                if victim and not helpers.isGold(props.recordId) then
                    lastItemPickedUp.player:sendEvent('IE_StoleItem', { recordId = props.recordId, count = lastItemPickedUp.count, victim = victim })
                 --   print("5", lastItemPickedUp.player)
                    lastItemPickedUp.player:sendEvent('IE_Update')
                end
            end
        end
    end,
    IE_UseItem = function(props)
        if props.object and props.actor and props.object.parentContainer ~= props.actor then
            local movedObject = moveInto({
                obj = props.object,
                count = 1,
                source = props.source or props.object.parentContainer,
                destination = props.actor,
                player = props.actor,
                pickpocket = props.pickpocket,
            })
            if movedObject then
                core.sendGlobalEvent('UseItem', {
                    object = movedObject,
                    actor = props.actor,
                })
                queueDelayedJob(function()
                --    print("6", props.UIPlayer)
                    props.UIPlayer:sendEvent('IE_Update')
                end, 2)
            end
            return
        end
        core.sendGlobalEvent('UseItem', {
            object = props.object,
            actor = props.actor,
        })
        queueDelayedJob(function()
            --print("7", props.UIPlayer)
            props.UIPlayer:sendEvent('IE_Update')
        end, 2)
    end,
}
-- deprecated event names
eventHandlers.MI_TryConfiscate = eventHandlers.IE_TryConfiscate
eventHandlers.MI_ConfiscateToOwner = eventHandlers.IE_ConfiscateToOwner
eventHandlers.MI_CommitPickpocket = eventHandlers.IE_CommitPickpocket
eventHandlers.MI_Teleport = eventHandlers.IE_Teleport
eventHandlers.MI_PostTeleport = eventHandlers.IE_PostTeleport
eventHandlers.MI_ScriptedDragStart = eventHandlers.IE_ScriptedDragStart
eventHandlers.MI_MoveInto = eventHandlers.IE_MoveInto
eventHandlers.MI_MoveAll = eventHandlers.IE_MoveAll
eventHandlers.MI_FinalizeBarter = eventHandlers.IE_FinalizeBarter
eventHandlers.MI_UIModeChanged = eventHandlers.IE_UIModeChanged
eventHandlers.MI_StolenChestInactive = eventHandlers.IE_StolenChestInactive
eventHandlers.MI_DisposeOfCorpse = eventHandlers.IE_DisposeOfCorpse
eventHandlers.MI_ModDisposition = eventHandlers.IE_ModDisposition

return {
    engineHandlers = {
        onInit = initChestLocks,
        onSave = function()
            return {
                containerMemory = containerMemory,
                disposedBodies = disposedBodies,
            }
        end,
        onLoad = function(data)
            initChestLocks()
            if data then
                containerMemory = data.containerMemory or {}
                disposedBodies = data.disposedBodies or {}
            end
        end,
        onUpdate = onUpdate,
    },
    eventHandlers = eventHandlers,
}
