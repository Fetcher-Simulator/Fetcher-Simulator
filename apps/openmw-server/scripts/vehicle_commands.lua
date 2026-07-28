local mp = require("mp")

local M = {}

local DEFAULT_PROFILE_ID = "fetcher.vehicles.pickup_85.v1"

local function trim(text)
    if not text then
        return ""
    end
    return (text:gsub("^%s+", ""):gsub("%s+$", ""))
end

function M.handleChat(player, data, env)
    local commandPrefix = env.commandPrefix or "/"
    local base = commandPrefix .. "vehicle"
    local message = trim(data.message)
    if message ~= base and message:sub(1, #base + 1) ~= base .. " " then
        return nil
    end

    local action = string.lower(trim(message:sub(#base + 1)))
    if action == "on" or action == "enter" then
        if mp.setPlayerVehicle(player.guid, DEFAULT_PROFILE_ID, 0) then
            player:sendMessage("Vehicle visual enabled: Lightbody Pickup '85")
        else
            player:sendMessage("Unable to enable the vehicle visual.")
        end
        return false
    end

    if action == "off" or action == "exit" then
        if mp.clearPlayerVehicle(player.guid) then
            player:sendMessage("Vehicle visual disabled.")
        else
            player:sendMessage("Unable to disable the vehicle visual.")
        end
        return false
    end

    player:sendMessage("Usage: " .. base .. " <on|off>")
    player:sendMessage("This experimental command tests synchronized root-attached vehicle presentation only.")
    return false
end

return M
