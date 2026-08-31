local input = require('openmw.input')
local ui = require('openmw.ui')
local self = require('openmw.self')
local types = require('openmw.types')
local I = require('openmw.interfaces')
local storage = require('openmw.storage')
local async = require('openmw.async')


local knownActions = require("scripts.MoveObjects.input.knownActions")

local keyBindings = storage.playerSection("AA_KeyBindings")

local function migrateBardcraftBuildModeConflict()
    local buildModeKey = knownActions.toggleBuildMode .. "_key"
    local currentBuildModeKey = keyBindings:get(buildModeKey)
    if currentBuildModeKey == input.KEY.B or currentBuildModeKey == input.KEY.F10 then
        keyBindings:set(buildModeKey, input.KEY.L)
        print("Ashlander Architect compatibility: moved Toggle Build Mode to L to avoid Bardcraft/debug conflicts")
    end

    local resetRotationKey = knownActions.resetRotation .. "_key"
    if keyBindings:get(resetRotationKey) == input.KEY.L then
        keyBindings:set(resetRotationKey, input.KEY.H)
        print("Ashlander Architect compatibility: moved Reset Rotation from L to H")
    end
end


local function getKeyCodeKB(bindingName)
    return keyBindings:get(bindingName .. "_key")
end
local function getKeyCodeCTRL(bindingName)
    return keyBindings:get(bindingName .. "_ctrl")
end
local function onKeyPress()

end
local function onControllerButtonPress()

end

return {
    engineHandlers = {
        onKeyPress = function(key)
            if not types.Player.isCharGenFinished(self) then return end
            migrateBardcraftBuildModeConflict()

            for index, value in pairs(knownActions) do
                if key.code == getKeyCodeKB(value) then
                    I.MoveObjects.handleInput(key, nil, value)
                end
            end
        end,
        onControllerButtonPress = function(ctrl)
            if not types.Player.isCharGenFinished(self) then return end
            for index, value in pairs(knownActions) do
                if ctrl == getKeyCodeCTRL(value) then
                    I.MoveObjects.handleInput(nil, ctrl, value)
                end
            end
        end,
        onMouseButtonPress = function(btn)
            for index, value in pairs(knownActions) do
                if btn == 1 and "leftMb" == getKeyCodeKB(value) then
                    I.MoveObjects.handleInput("leftMb", nil, value)
                elseif btn == 3 and "rightMb" == getKeyCodeKB(value) then
                    I.MoveObjects.handleInput("leftMb", nil, value)
                end
            end
        end
    }
}
