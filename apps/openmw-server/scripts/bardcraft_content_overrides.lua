local mp = require("mp")

local M = {}

local OVERRIDES = {
    {
        recordType = "clothing",
        recordId = "_es_link_gtunic",
        data = {
            -- Clone the configured static tunic and clear only its enchantment.
            baseId = "_es_link_gtunic",
            enchant = "",
        },
    },
}

function M.initialize()
    local allApplied = true
    for _, override in ipairs(OVERRIDES) do
        local applied = mp.upsertDynamicRecord(
            override.recordType,
            override.recordId,
            override.data,
            {
                scope = "permanent",
                persistent = true,
                mode = "override",
            })
        if applied then
            mp.log(string.format(
                "[bardcraft] content override applied type=%s id=%s",
                override.recordType,
                override.recordId))
        else
            allApplied = false
            mp.log(string.format(
                "[bardcraft] content override rejected type=%s id=%s",
                override.recordType,
                override.recordId))
        end
    end
    return allApplied
end

return M
