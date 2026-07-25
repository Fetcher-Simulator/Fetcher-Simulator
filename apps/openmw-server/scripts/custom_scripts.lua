-- Central registry for Fetcher-specific server Lua modules.
--
-- Keep core.lua focused on common server lifecycle, authentication, event
-- dispatch, and shared infrastructure. Add or remove optional gameplay and
-- administration modules here, then access them through the returned table.

return {
    adminUiService = require("admin_ui_service"),
    bardcraftNetworkPolicy = require("bardcraft_network_policy"),
    destructibleSpawners = require("destructible_spawners"),
    extendedSpeechCommands = require("extended_speech_commands"),
    markRecallCommands = require("mark_recall"),
    recordDynamicTest = require("recorddynamic_test"),
    speechCommands = require("speech_commands"),
    surfCommands = require("surf_commands"),
    surfTimer = require("surf_timer"),
}
