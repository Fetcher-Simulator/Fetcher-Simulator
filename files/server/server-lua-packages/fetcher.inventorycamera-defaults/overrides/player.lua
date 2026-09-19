---@omw-context player

local I = require('openmw.interfaces')
local storage = require('openmw.storage')

I.Settings.registerGroup {
    key = 'SettingsInventoryCamera_fetcher',
    page = 'InventoryCamera',
    name = 'Fetcher Defaults',
    description = 'Fetcher multiplayer defaults for Inventory Camera.',
    permanentStorage = true,
    settings = {
        {
            key = 'enabled',
            name = 'Enable Inventory Camera',
            description = 'Enable the Inventory Camera mod for this character.',
            renderer = 'checkbox',
            default = false,
        },
    },
}

local fetcherSettings = storage.playerSection('SettingsInventoryCamera_fetcher')
local settings = require("scripts.InventoryCamera.settings")
local pan = require("scripts.InventoryCamera.camera.pan")
local view = require("scripts.InventoryCamera.camera.view")
local preview = require("scripts.InventoryCamera.camera.preview")
local save = require("scripts.InventoryCamera.camera.save")

local function isEnabled()
    return fetcherSettings:get('enabled') == true
end

-- Wire settings changes to the outside-of-inventory preview. Done here
-- (rather than settings.lua requiring preview.lua directly) to avoid a
-- circular require between settings <-> preview <-> view.
settings.onPreviewCallbacks(
    function()
        if isEnabled() then preview.start('start') end
    end,
    function()
        if isEnabled() then preview.start('finish') end
    end
)

local function onUpdate(dt)
    if not isEnabled() then
        preview.endPreview()
        if view.active then view.exit() end
        return
    end

    preview.update(dt)
    pan.update(dt, settings.cam.yawPanDirection)
end

local function onUiModeChanged(data)
    if not isEnabled() then
        preview.endPreview()
        if view.active then view.exit() end
        return
    end

    local enteringInventory = data.newMode == 'Interface' and data.oldMode ~= 'Interface'
    local leavingInventory = data.oldMode == 'Interface' and data.newMode ~= 'Interface'

    if enteringInventory then
        preview.endPreview()
        view.enter()
    elseif leavingInventory then
        view.exit()
    elseif view.active then
        view.exit()
    end
end

return {
    eventHandlers = {
        UiModeChanged = onUiModeChanged,
    },
    engineHandlers = {
        onUpdate = onUpdate,
        onInit = save.onLoad,
        onSave = save.onSave,
        onLoad = save.onLoad,
    },
}