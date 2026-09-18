-- Resolve only semantic keys against the independently installed mod/content.
local ok, shared = pcall(require, 'scripts.potionanim_shared')
if not ok then return { available = false } end
local types = require('openmw.types')
local M = { available = true }
M.groups = { potionl = 'drink', eatingr = 'eat', drinkbone = 'beverage',
    bugmusk2 = 'bugmusk', skoomapipe = 'skooma', smokepipe1 = 'smoke', smoke1r = 'cigar' }
local meshNames = { potion = 'STANDARD_POTION_3RD', goblet = 'GOBLET', bugmusk = 'BUGMUSK',
    skooma = 'SKOOMA_PIPE', pipe = 'SMOKE', cigar = 'CIGAR' }
local soundNames = { shatter = 'DRINK_SOUNDS', eat = 'EAT_SOUNDS', bong = 'BONG_SOUNDS', smoke = 'SMOKE_SOUNDS' }
local function normalized(path) return tostring(path or ''):lower():gsub('\\', '/') end
local function record(id) return types.Potion.records[id] or types.Ingredient.records[id] end
function M.mesh(key, id)
    if meshNames[key] then return shared.MESHES[meshNames[key]] end
    if key == 'ingredient' then return shared.INGRED_MESH[id] end
    if key == 'beverage' then return shared.DRINKBONE_MESH[id] end
    if key == 'item' then local r = record(id); return r and r.model end
end
function M.meshKey(path, id)
    local p = normalized(path)
    if p == normalized(shared.MESHES.STANDARD_POTION) then return 'potion' end
    for _, key in ipairs({'potion', 'goblet', 'bugmusk', 'skooma', 'pipe', 'cigar', 'ingredient', 'beverage', 'item'}) do
        local candidate = M.mesh(key, id)
        if candidate and p == normalized(candidate) then return key end
    end
end
function M.sound(key, index)
    if key == 'bugmusk' then return shared.BUGMUSK_SOUND end
    local list = shared[soundNames[key]]
    return list and list[index]
end
function M.soundKey(path)
    local p = normalized(path)
    if p == normalized(shared.BUGMUSK_SOUND) then return 'bugmusk', 1 end
    for key, name in pairs(soundNames) do
        for index, file in ipairs(shared[name] or {}) do
            if p == normalized(file) then return key, index end
        end
    end
end
return M
