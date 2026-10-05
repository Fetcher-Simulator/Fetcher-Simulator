-- MWZ 2.26 Hardcore Mode.ESP assigns this script to its zombie actors.
-- Its original source toggles each actor at 4,000 units from the local player,
-- which lets observers hide a zombie another player is fighting. Keep the
-- actor enabled and let the shared actor roster control its lifetime.
local mp = require("mp")

return {
    eventHandlers = {
        OnServerInit = function(_)
            local accepted = mp.upsertDynamicRecord("script", "Z_Zombie_Rendering", {
                sourceText = "Begin Z_Zombie_Rendering\n\nEnd Z_Zombie_Rendering\n",
            }, {
                scope = "permanent",
                persistent = true,
                mode = "override",
            })
            mp.log("[morezombies] Z_Zombie_Rendering override " .. (accepted and "queued" or "rejected"))
        end,
    },
}
