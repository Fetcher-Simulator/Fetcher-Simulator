-- Run from repository root: lua scripts/test_inventoryextender_inventory.lua
local root = 'files/server/server-lua-packages/fetcher.inventoryextender-fix/overrides/'
local resolve = dofile(root .. 'droptarget.lua')
local player, corpse = { id = 'player' }, { id = 'corpse' }
local function window(target, x)
    return { target = target, element = { layout = { props = {
        visible = true, position = { x = x, y = 0 }, size = { x = 100, y = 100 },
    } } } }
end
local windows = { Inventory = window(player, 0), Container = window(corpse, 150) }
local invPos, corpsePos = { x = 50, y = 50 }, { x = 200, y = 50 }
assert(resolve('Container', false, windows, 'Inventory', player, corpsePos) == corpse)
assert(resolve('Container', false, windows, 'Inventory', player, invPos) == player)
assert(resolve('Container', false, windows, 'Container', corpse, invPos) == player)
assert(resolve('Container', false, windows, 'Container', corpse, corpsePos) == corpse)
for _, mode in ipairs({ 'Interface', 'Barter', 'Companion' }) do
    assert(resolve(mode, false, windows, 'Inventory', player, corpsePos) == player)
end
assert(resolve('Container', true, windows, 'Inventory', player, corpsePos) == player)
assert(resolve('Container', false, windows, 'Inventory', player, nil) == player)
assert(resolve('Container', false, windows, 'Inventory', player, { x = 500, y = 500 }) == nil)
windows.Container.element.layout.props.visible = false
assert(resolve('Container', false, windows, 'Inventory', player, corpsePos) == nil)
windows.Container.element.layout.props.visible = true

local events, puts = {}, {}
local inv = { findAll = function() return {} end, find = function() return nil end }
local actorType = { inventory = function() return inv end, hasEquipped = function() return false end }
player.type, corpse.type = actorType, actorType
function player:sendEvent(name, props) events[name] = props end
local types = {
    Player = { objectIsInstance = function(obj) return obj == player end },
    Actor = { objectIsInstance = function() return true end, isDead = function(obj) return obj == corpse end },
    Container = { objectIsInstance = function() return false end },
}
package.loaded['openmw.types'] = types
package.loaded['openmw.interfaces'] = { Activation = { addHandlerForType = function() end } }
package.loaded['openmw.world'] = { activeActors = {}, mwscript = { getLocalScript = function() return nil end } }
package.loaded['openmw.core'] = {}
package.loaded['openmw.util'] = {}
package.loaded['openmw.mp'] = {
    isConnected = function() return true end,
    inventoryPut = {
        isAvailable = function() return true end,
        request = function(destination, obj, count) puts[#puts + 1] = { destination, obj, count } end,
    },
}
package.loaded['scripts.InventoryExtender.util.cell'] = {}
package.loaded['scripts.InventoryExtender.util.helpers'] = {
    isGold = function(obj) return obj.recordId == 'gold_001' end,
    itemCanStack = function() return false end,
}
local global = dofile(root .. 'global.lua')
for _, recordId in ipairs({ 'gold_001', 'mandalore_pauldronr' }) do
    for _, count in ipairs({ 1, 10 }) do
        local obj = { id = recordId, recordId = recordId, count = 10, parentContainer = player }
        function obj:split() error('Cursor drag must retain the authoritative player row') end
        function obj:moveInto() error('Cursor drag must not mutate the live player store') end
        global.eventHandlers.IE_MoveInto({ obj = obj, source = player, destination = player,
            player = player, count = count, dragStart = true })
        assert(events.IE_SetDraggingObject.obj == obj)
        assert(events.IE_SetDraggingObject.preserveObject)
        assert(obj.count == 10 and obj.parentContainer == player)
        global.eventHandlers.IE_MoveInto({ obj = obj, source = player, destination = corpse,
            player = player, count = count })
        assert(puts[#puts][1] == corpse and puts[#puts][2] == obj and puts[#puts][3] == count)
    end
end
print('Inventory Extender drop routing and authoritative partial/full cursor tests passed')
