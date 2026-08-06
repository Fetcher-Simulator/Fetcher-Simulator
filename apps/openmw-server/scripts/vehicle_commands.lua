local mp = require("mp")

local M = {}

local DEFAULT_PROFILE_ID = "fetcher.vehicles.pickup_85.v1"
local DEFAULT_PARKED_REF_ID = "fv_pickup_85"
local DEFAULT_SPAWN_DISTANCE = 256
local DEFAULT_SPAWN_DIRECTION = 0

local function trim(text)
    if not text then
        return ""
    end
    return (text:gsub("^%s+", ""):gsub("%s+$", ""))
end

local function splitWords(text)
    local words = {}
    for word in tostring(text or ""):gmatch("%S+") do
        words[#words + 1] = word
    end
    return words
end

local function sendUsage(player, base)
    player:sendMessage("Usage: " .. base .. " spawn [distance] [direction]")
    player:sendMessage("       " .. base .. " <on|off>")
    player:sendMessage("Spawn direction: 0=forward, 1=back, 2=left, 3=right.")
end

local function spawnParkedVehicle(player, args, env, base)
    local distance = DEFAULT_SPAWN_DISTANCE
    if args[2] and args[2] ~= "" then
        distance = tonumber(args[2])
        if not distance or distance < 0 then
            player:sendMessage("distance must be a non-negative number.")
            return false
        end
    end

    local direction = DEFAULT_SPAWN_DIRECTION
    if args[3] and args[3] ~= "" then
        direction = tonumber(args[3])
        if not direction or direction % 1 ~= 0 or direction < 0 or direction > 3 then
            player:sendMessage("direction must be 0, 1, 2, or 3.")
            return false
        end
    end

    if args[4] then
        sendUsage(player, base)
        return false
    end

    if type(env.normalizeCellId) ~= "function" or type(env.placeAtPosition) ~= "function" then
        player:sendMessage("Vehicle spawn helpers are unavailable.")
        mp.log("[vehicle] spawn failed: core helpers unavailable")
        return false
    end

    local cellId = env.normalizeCellId(player.cell)
    if not cellId or cellId == "" then
        player:sendMessage("Unable to determine your current cell.")
        return false
    end

    local position = env.placeAtPosition(player, distance, direction)
    if not mp.placeObject(DEFAULT_PARKED_REF_ID, 1, cellId, position) then
        player:sendMessage("Failed to queue parked vehicle placement.")
        return false
    end

    player:sendMessage(string.format(
        "Placed Lightbody Pickup '85 in %s at distance %.1f direction %d.",
        cellId, distance, direction
    ))
    mp.log(string.format(
        "[vehicle] /vehicle spawn by %s guid=%s refId=%s cell=%s distance=%.1f direction=%d",
        tostring(player.name), tostring(player.guid), DEFAULT_PARKED_REF_ID, cellId, distance, direction
    ))
    return false
end

function M.handleChat(player, data, env)
    env = env or {}
    local commandPrefix = env.commandPrefix or "/"
    local base = commandPrefix .. "vehicle"
    local message = trim(data and data.message)
    if message ~= base and message:sub(1, #base + 1) ~= base .. " " then
        return nil
    end

    local args = splitWords(trim(message:sub(#base + 1)))
    local action = string.lower(args[1] or "")

    if action == "spawn" then
        return spawnParkedVehicle(player, args, env, base)
    end

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

    sendUsage(player, base)
    return false
end

return M
