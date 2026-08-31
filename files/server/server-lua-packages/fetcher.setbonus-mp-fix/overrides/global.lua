-- Set Bonus -- OpenMW global script.
-- Builds the tier ability spells through the authoritative runtime-record service and applies the
-- correct tier to each actor based on equipped set pieces. Targets OpenMW 0.51+.
local world   = require('openmw.world')
local core    = require('openmw.core')
local types   = require('openmw.types')
local storage = require('openmw.storage')
local async   = require('openmw.async')
local mp      = require('openmw.mp')
local I       = require('openmw.interfaces')
local data    = require('scripts.SetBonus.data')
local C       = require('scripts.SetBonus.conditions')

local SPELL = {}      -- [setIndex][tier] = generated spell record (object)
local SPELLCOND = {} -- [setIndex][tier] = { { record=, condition= }, ... }
local itemLink = {}   -- itemId(lower) -> { setIndex, ... }
local iconLink = {}   -- iconPath(lower) -> { setIndex, ... }  (matches enchanted/copied items)
local stateOf = {}    -- [actorId] = { [setIndex] = tier }
local cleaned = {}    -- [actorId] = true once stale spells purged
local ready = false
local initializing = false
local buildGeneration = 0

-- Exact names of every spell WE generate, built from `data` (see buildSpellsForSet:
-- unconditional tier spells are "<Set> Set Bonus", conditional per-effect sub-spells
-- are "<Set> Bonus"). Used to purge leftover copies from a previous save without
-- catching unrelated abilities that merely share the " Bonus" suffix. Cached and
-- invalidated (set back to nil) at every `cleaned = {}` reset site, since a set
-- redefinition can rename/add/drop sets.
local knownSpellNames = nil
local function buildKnownSpellNames()
    local names = {}
    for _, s in ipairs(data) do
        names[s.name .. ' Set Bonus'] = true
        names[s.name .. ' Bonus'] = true
    end
    return names
end

local cfg  -- global settings section

local function npcBonusesEnabled()
    local v = cfg and cfg:get('npcBonuses')
    if v == nil then return true end
    return v
end

local function matchByIconEnabled()
    local v = cfg and cfg:get('matchByIcon')
    if v == nil then return true end
    return v
end

local function conditionalEnabled()
    local v = cfg and cfg:get('conditionalBonuses')
    if v == nil then return true end
    return v
end

local NOSCALE = { waterbreathing = true, waterwalking = true, levitate = true, jump = true, telekinesis = true }
local function isWeakness(id) return type(id) == 'string' and id:sub(1, 8) == 'weakness' end
local function benefitScaleVal()
    local v = cfg and cfg:get('scale')
    if type(v) == 'number' and v > 0 then return v end
    return 1.0
end
local function drawbackScaleVal()
    local v = cfg and cfg:get('weaknessScale')
    if type(v) == 'number' and v >= 0 then return v end
    return 1.0
end
-- Whole-number scale (round half up); a real effect (base >= 1) never rounds to 0
-- so over-time ticks (restore*) and small bonuses can't silently vanish. Scale 0
-- does zero the effect (used to switch drawbacks off).
local function roundScale(base, scale)
    if base == 0 or scale <= 0 then return 0 end
    local v = math.floor(base * scale + 0.5)
    if v < 1 and base >= 1 then v = 1 end
    return v
end

-- Icon+mesh matching: a player-enchanted or copied set piece gets a new record id
-- but keeps the base item's inventory icon AND model, so we index sets by that
-- combined signature. Icon alone is not safe: icon-replacer/compilation mods
-- (e.g. NOD) routinely point unrelated armor records at the same shared icon to
-- save texture slots, which would otherwise false-match e.g. an unrelated House
-- Hlaalu helm onto an Indoril set purely because they share an icon file. Two
-- different armor pieces essentially never also share a mesh, so requiring both
-- keeps the "enchanted/copied item" fallback narrow to actual copies.
local ICON_TYPES = { types.Armor, types.Clothing, types.Weapon }
local function iconMeshForRecordId(id)
    for _, t in ipairs(ICON_TYPES) do
        local ok, rec = pcall(t.record, id)
        if ok and rec and rec.icon and rec.icon ~= '' then return rec.icon, rec.model end
    end
    return nil, nil
end
local function iconMeshForObject(obj)
    for _, t in ipairs(ICON_TYPES) do
        if t.objectIsInstance(obj) then
            local rec = t.record(obj)
            if rec and rec.icon and rec.icon ~= '' then return rec.icon, rec.model end
            return nil, nil
        end
    end
    return nil, nil
end
-- Combined lookup key: icon path is the primary signal, mesh path disambiguates
-- unrelated items that happen to share an icon. Returns nil if there's no icon
-- to key on at all (nothing to fall back to).
local function iconMeshSig(icon, mesh)
    -- Require BOTH icon and mesh: the fallback must never degrade to icon-only
    -- (the cross-match bug it exists to prevent). Meshless records use id only.
    if not (icon and icon ~= '' and mesh and mesh ~= '') then return nil end
    return icon:lower() .. '|' .. mesh:lower()
end
local function listHas(list, si)
    if not list then return false end
    for _, x in ipairs(list) do if x == si then return true end end
    return false
end
-- Weight-class umbrella sets are defined strictly by an item's engine weight
-- class (Light/Medium/Heavy Armor), so they must NEVER be matched by icon/mesh:
-- a Light piece can share a Medium piece's look and would be filed in the wrong
-- class set. They're kept out of the icon index (id/weight roster only). Cloth
-- is not weight-based, so it stays eligible.
local ICON_EXCLUDED = { ['Heavy Armor'] = true, ['Medium Armor'] = true, ['Light Armor'] = true }

-- Weight-class umbrella sets are read straight off each armor piece's own
-- weight and slot, using the same formula the engine uses to decide Light vs
-- Medium vs Heavy (GMSTs fLightMaxMod/fMedMaxMod against the per-slot base
-- weight GMST), instead of the pre-baked item-id roster. A vanilla-enchanted
-- item is a new record with a new id that the roster (and, deliberately, the
-- icon/mesh fallback above) can't catch -- but it keeps the source item's
-- weight and slot, so recomputing the class here is immune to that new id.
-- No roster, icon/mesh fallback, or Consistent-Enchanting-style base-id
-- tracking is needed for these three sets specifically.
local ARMOR_SLOT_WEIGHT_GMST = {
    [types.Armor.TYPE.Helmet] = 'iHelmWeight',
    [types.Armor.TYPE.LPauldron] = 'iPauldronWeight',
    [types.Armor.TYPE.RPauldron] = 'iPauldronWeight',
    [types.Armor.TYPE.Cuirass] = 'iCuirassWeight',
    [types.Armor.TYPE.LGauntlet] = 'iGauntletWeight',
    [types.Armor.TYPE.RGauntlet] = 'iGauntletWeight',
    [types.Armor.TYPE.LBracer] = 'iGauntletWeight',
    [types.Armor.TYPE.RBracer] = 'iGauntletWeight',
    [types.Armor.TYPE.Greaves] = 'iGreavesWeight',
    [types.Armor.TYPE.Boots] = 'iBootsWeight',
    [types.Armor.TYPE.Shield] = 'iShieldWeight',
}
local function armorWeightClassName(obj)
    if not types.Armor.objectIsInstance(obj) then return nil end
    local rec = types.Armor.record(obj)
    local gmstName = rec and ARMOR_SLOT_WEIGHT_GMST[rec.type]
    if not gmstName then return nil end
    local baseWeight = core.getGMST(gmstName)
    if type(baseWeight) ~= 'number' or baseWeight <= 0 then return nil end
    local lightMod = core.getGMST('fLightMaxMod')
    local medMod = core.getGMST('fMedMaxMod')
    if type(lightMod) ~= 'number' then lightMod = 0.6 end
    if type(medMod) ~= 'number' then medMod = 0.9 end
    local w = rec.weight or 0
    if w <= baseWeight * lightMod then return 'Light Armor'
    elseif w <= baseWeight * medMod then return 'Medium Armor'
    else return 'Heavy Armor' end
end

-- Map one set's item icon+mesh signatures to its index (deduped).
local function linkIconsForSet(si)
    if ICON_EXCLUDED[data[si].name] then return end
    for _, it in ipairs(data[si].items) do
        local icon, mesh = iconMeshForRecordId(it)
        local sig = iconMeshSig(icon, mesh)
        if sig then
            local list = iconLink[sig]
            if not list then list = {}; iconLink[sig] = list end
            if not listHas(list, si) then list[#list + 1] = si end
        end
    end
end

local function dbg(...)
    if cfg and cfg:get('debug') then print('[SetBonus]', ...) end
end

local function registerSettings()
    I.Settings.registerGroup{
        key = 'SettingsGlobalSetBonus',
        page = 'SetBonus',
        l10n = 'SetBonus',
        name = 'General',
        description = 'Set Bonus options (shared/global).',
        permanentStorage = true,
        settings = {
            {
                key = 'npcBonuses',
                renderer = 'checkbox',
                name = 'Apply bonuses to NPCs',
                description = 'If on, NPCs wearing full sets also receive set bonuses. Turn off for player-only bonuses.',
                default = true,
            },
            {
                key = 'matchByIcon',
                renderer = 'checkbox',
                name = 'Match enchanted/copied items by icon',
                description = 'Also match set pieces by their inventory icon and model, so a player-enchanted or copied set item (new internal ID, same icon+model) still counts toward the set. Turn off for strict ID-only matching.',
                default = true,
            },
            {
                key = 'conditionalBonuses',
                renderer = 'checkbox',
                name = 'Conditional bonuses',
                description = 'Enable condition-gated bonus effects (e.g. below 50% health, at night, or above a skill level). When off, such effects are not applied. Only affects sets that define conditions.',
                default = true,
            },
            {
                key = 'debug',
                renderer = 'checkbox',
                name = 'Debug logging',
                description = 'Print Set Bonus diagnostics (spell creation, tier changes) to the log/console.',
                default = false,
            },
            {
                key = 'scale',
                renderer = 'number',
                argument = { min = 0.25, max = 3.0, integer = false },
                name = 'Benefit magnitude scale',
                description = 'Multiplies the helpful set-bonus effects. 1.0 = default, 0.5 = half, 2.0 = double. Magnitudes are always whole numbers and never drop to 0. Applies immediately.',
                default = 1.0,
            },
            {
                key = 'weaknessScale',
                renderer = 'number',
                argument = { min = 0.0, max = 3.0, integer = false },
                name = 'Weakness (drawback) scale',
                description = 'Multiplies the thematic Weakness drawbacks independently of the benefits. 1.0 = default, 0.5 = milder, 2.0 = harsher, 0 = no drawbacks. Applies immediately.',
                default = 1.0,
            },
        },
    }
    cfg = storage.globalSection('SettingsGlobalSetBonus')
end

local byName = {}     -- setName(lower) -> setIndex (into `data`)

-- Link every item of one set to its index (deduped so a repeat can't inflate counts).
local function linkItems(si)
    local s = data[si]
    for _, it in ipairs(s.items) do
        local k = it:lower()
        local list = itemLink[k]
        if not list then list = {}; itemLink[k] = list end
        local dup = false
        for _, x in ipairs(list) do if x == si then dup = true; break end end
        if not dup then list[#list + 1] = si end
    end
end

local function buildItemLinks()
    for si in ipairs(data) do linkItems(si); linkIconsForSet(si) end
end

-- Drop every item/icon link that points at a set index (used when a set is replaced).
local function unlinkSet(si)
    for _, index in ipairs({ itemLink, iconLink }) do
        for k, list in pairs(index) do
            for i = #list, 1, -1 do if list[i] == si then table.remove(list, i) end end
            if #list == 0 then index[k] = nil end
        end
    end
end

local function indexByName()
    for si, s in ipairs(data) do byName[s.name:lower()] = si end
end

-- ---------------------------------------------------------------------------
-- Player-side sync. The Inventory Extender tooltip runs in the PLAYER VM with
-- its own require'd copy of data.lua, so interop changes made here (register/
-- replace/amend/addItems -- e.g. the Conditional Rebalance) would be invisible
-- on tooltips. We track which sets the interop has touched and push those
-- definitions to player scripts (SetBonus_syncSets), on change and whenever a
-- player is added. Untouched sets are identical to the file, so only the
-- dirty ones are ever sent.
-- ---------------------------------------------------------------------------
local dirty = {}  -- [setIndex] = true once touched via the interop (never cleared)
local function markDirty(si)
    if si then dirty[si] = true end
end
local function dirtyPayload()
    local sets = {}
    for si in pairs(dirty) do
        local s = data[si]
        if s then
            sets[#sets + 1] = {
                name = s.name,
                thresholds = s.thresholds,
                bonuses = s.bonuses,
                items = s.items,
            }
        end
    end
    return { sets = sets }
end
local function syncDirtyTo(player)
    if next(dirty) then
        pcall(function() player:sendEvent('SetBonus_syncSets', dirtyPayload()) end)
    end
end
local function syncDirtyAll()
    if not next(dirty) then return end
    local payload = dirtyPayload()
    for _, pl in ipairs(world.players) do
        pcall(function() pl:sendEvent('SetBonus_syncSets', payload) end)
    end
end

local builtCount, failCount = 0, 0
local MAX_RECORDS_PER_REQUEST = 16
local SCALE_REBUILD_DEBOUNCE = 0.35
local needsRebuild = false
local scaleRebuildTicket = 0

local function magFor(e)
    if NOSCALE[e.effect] then return e.mag
    elseif isWeakness(e.effect) then return roundScale(e.mag, drawbackScaleVal())
    else return roundScale(e.mag, benefitScaleVal()) end
end

local function makeDraft(name, effs)
    local draft = { name = name, type = core.magic.SPELL_TYPE.Ability, cost = 0, effects = {} }
    for _, e in ipairs(effs) do
        local m = magFor(e)
        draft.effects[#draft.effects + 1] = {
            id = e.effect,
            affectedSkill = e.skill,
            affectedAttribute = e.attribute,
            range = core.magic.RANGE.Self,
            area = 0,
            duration = e.dur or 0,
            magnitudeMin = m,
            magnitudeMax = m,
        }
    end
    return core.magic.spells.createRecordDraft(draft)
end

-- Collect one set into staging tables. The live SPELL/SPELLCOND tables are not
-- replaced until every authoritative callback for this build completes, so
-- actors keep their previous bonuses while a rebuild is in flight.
local function collectSpellsForSet(si, descriptors, stagedSpell, stagedCond)
    local s = data[si]
    local stagedTiers = {}
    local stagedConditional = {}
    stagedSpell[si] = stagedTiers
    stagedCond[si] = stagedConditional

    local function queue(name, effs, assign)
        local ok, draft = pcall(makeDraft, name, effs)
        if not ok or not draft then
            failCount = failCount + 1
            print('[SetBonus] could not draft spell "' .. tostring(name) .. '": ' .. tostring(draft))
            return
        end
        descriptors[#descriptors + 1] = {
            key = 'spell_' .. tostring(#descriptors + 1),
            draft = draft,
            name = name,
            assign = assign,
        }
    end

    for _, tier in ipairs({ 'min', 'mid', 'max' }) do
        local effs = s.bonuses[tier]
        if effs and #effs > 0 then
            local uncond, cond = {}, {}
            for _, e in ipairs(effs) do
                if e.condition then cond[#cond + 1] = e else uncond[#uncond + 1] = e end
            end
            local slotTier = tier
            if #uncond > 0 then
                queue(s.name .. ' Set Bonus', uncond, function(record)
                    stagedTiers[slotTier] = record
                end)
            end
            local list = {}
            stagedConditional[slotTier] = list
            for _, e in ipairs(cond) do
                local condition = e.condition
                queue(s.name .. ' Bonus', { e }, function(record)
                    list[#list + 1] = { record = record, condition = condition }
                end)
            end
        end
    end
end

local function buildSpellRecords(indices, onComplete)
    buildGeneration = buildGeneration + 1
    local generation = buildGeneration
    builtCount, failCount = 0, 0

    local descriptors = {}
    local stagedSpell, stagedCond = {}, {}
    for _, si in ipairs(indices) do
        collectSpellsForSet(si, descriptors, stagedSpell, stagedCond)
    end

    local function commit()
        if generation ~= buildGeneration then return end
        for _, si in ipairs(indices) do
            SPELL[si] = stagedSpell[si] or {}
            SPELLCOND[si] = stagedCond[si] or {}
        end
        if onComplete then onComplete() end
    end

    if #descriptors == 0 then
        commit()
        return
    end

    -- Never fall back to world.createRecord in multiplayer. Those records are
    -- session-local, so actors can retain $custom_spell_* ids that do not exist
    -- on the next client/session. Wait until the authoritative record service is
    -- available and retry the same build instead.
    if not (mp.records and mp.records.isAvailable and mp.records.isAvailable()) then
        async:newUnsavableSimulationTimer(0.25, function()
            buildSpellRecords(indices, onComplete)
        end)
        return
    end

    local pending = math.ceil(#descriptors / MAX_RECORDS_PER_REQUEST)
    local function batchDone()
        if generation ~= buildGeneration then return end
        pending = pending - 1
        if pending == 0 then commit() end
    end

    for first = 1, #descriptors, MAX_RECORDS_PER_REQUEST do
        local batch = {}
        local records = {}
        local last = math.min(first + MAX_RECORDS_PER_REQUEST - 1, #descriptors)
        for i = first, last do
            local descriptor = descriptors[i]
            batch[#batch + 1] = descriptor
            records[#records + 1] = {
                key = descriptor.key,
                type = 'spell',
                definition = descriptor.draft,
            }
        end

        local callbackBatch = batch
        local ok, requestError = pcall(function()
            mp.records.request({ records = records }, function(result)
                if generation ~= buildGeneration then return end
                if result.accepted then
                    for _, descriptor in ipairs(callbackBatch) do
                        local mapping = result.records and result.records[descriptor.key]
                        local record = mapping and core.magic.spells.records[mapping.id]
                        if record then
                            builtCount = builtCount + 1
                            descriptor.assign(record)
                        else
                            failCount = failCount + 1
                            print('[SetBonus] authoritative spell result missing local record for "'
                                .. tostring(descriptor.name) .. '"')
                        end
                    end
                else
                    failCount = failCount + #callbackBatch
                    print(('[SetBonus] authoritative spell batch rejected: %s (%d records)')
                        :format(tostring(result.error), #callbackBatch))
                end
                batchDone()
            end)
        end)
        if not ok then
            failCount = failCount + #callbackBatch
            print(('[SetBonus] could not submit authoritative spell batch: %s (%d records)')
                :format(tostring(requestError), #callbackBatch))
            batchDone()
        end
    end
end

local function allSetIndices()
    local indices = {}
    for si in ipairs(data) do indices[#indices + 1] = si end
    return indices
end

local function buildSpellsForSet(si, onComplete)
    buildSpellRecords({ si }, onComplete)
end

local function buildSpells(onComplete)
    buildSpellRecords(allSetIndices(), onComplete)
end

local function recomputeAll()
    for _, a in ipairs(world.activeActors) do
        core.sendGlobalEvent('SetBonus_recompute', { actor = a })
    end
end

-- ---------------------------------------------------------------------------
-- Framework API (exposed as interface I.SetBonus and via global events).
-- Registration is data-only, so it works over events too. Sets registered
-- before init() are picked up by init()'s build pass; sets registered after
-- are built and applied immediately.
-- ---------------------------------------------------------------------------

local function normalizeSet(sd)
    assert(type(sd) == 'table' and type(sd.name) == 'string' and sd.name ~= '',
        '[SetBonus] registerSet: a non-empty string `name` is required')
    assert(type(sd.items) == 'table', '[SetBonus] registerSet: `items` table is required')
    assert(type(sd.bonuses) == 'table', '[SetBonus] registerSet: `bonuses` table is required')
    local t = sd.thresholds or {}
    sd.thresholds = { min = t.min or 2, mid = t.mid or 4, max = t.max or 6 }
    local seen, uniq = {}, {}
    for _, it in ipairs(sd.items) do
        if type(it) == 'string' then
            local k = it:lower()
            if not seen[k] then seen[k] = true; uniq[#uniq + 1] = k end
        end
    end
    sd.items = uniq
    return sd
end

-- Normalise, index, and link one set WITHOUT rebuilding spells or recomputing
-- actors; single and batch registration share this. Returns (setIndex, replaced).
local function registerSetInner(sd)
    normalizeSet(sd)
    local key = sd.name:lower()
    local si = byName[key]
    local replaced = false
    if si then
        unlinkSet(si)            -- drop the previous definition's item links first
        data[si] = sd            -- full replace (an empty set effectively disables it)
        replaced = true
    else
        data[#data + 1] = sd
        si = #data
        byName[key] = si
    end
    linkItems(si)
    linkIconsForSet(si)
    markDirty(si)
    dbg(('registerSet: %s (%d items)%s'):format(sd.name, #sd.items, replaced and ' [replace]' or ''))
    return si, replaced
end

local function registerSet(sd)
    local si, replaced = registerSetInner(sd)
    if ready then
        buildSpellsForSet(si, function()
            if replaced then
                -- a redefinition can change tiers/effects; purge stale records by name
                cleaned = {}
                knownSpellNames = nil
                stateOf = {}
            end
            recomputeAll()
        end)
    elseif initializing then
        needsRebuild = true
    end
    syncDirtyAll()
    return sd
end

-- Batch registration (interface v2): register/replace MANY sets with one
-- spell-build pass and ONE actor recompute at the end. A per-set registerSet
-- loop triggers a full recompute per call, which is noticeable when a mod
-- re-registers the whole roster (e.g. the Conditional Rebalance's 136 sets).
-- Payload: an array of the same tables registerSet takes. Invalid entries are
-- skipped with a log line; the valid ones still apply. Returns the count.
local function registerSets(list)
    if type(list) ~= 'table' then return 0 end
    local indices, anyReplaced, n = {}, false, 0
    for _, sd in ipairs(list) do
        local ok, si, replaced = pcall(registerSetInner, sd)
        if ok then
            n = n + 1
            indices[#indices + 1] = si
            anyReplaced = anyReplaced or replaced
        else
            print('[SetBonus] registerSets: skipped entry: ' .. tostring(si))
        end
    end
    if ready and n > 0 then
        buildSpellRecords(indices, function()
            if anyReplaced then
                -- redefinitions can change tiers/effects; purge stale records by name
                cleaned = {}
                knownSpellNames = nil
                stateOf = {}
            end
            recomputeAll()
        end)
    elseif initializing and n > 0 then
        needsRebuild = true
    end
    syncDirtyAll()
    dbg(('registerSets: %d set(s) in one batch'):format(n))
    return n
end

local TIERS = { 'min', 'mid', 'max' }
local MAXEFF = 8
-- Append `src` effects onto `dst` per tier (capped at 8 effects/tier).
local function mergeBonuses(dst, src)
    dst = dst or {}
    for _, tier in ipairs(TIERS) do
        if src[tier] then
            local list = dst[tier] or {}
            for _, e in ipairs(src[tier]) do
                if #list >= MAXEFF then
                    print('[SetBonus] amendSet: ' .. tier .. ' tier capped at 8 effects; extra ignored')
                    break
                end
                list[#list + 1] = e
            end
            dst[tier] = list
        end
    end
    return dst
end

-- Non-destructively amend an existing set: append items and/or bonus effects, and
-- override individual thresholds. Unlike registerSet, it keeps what's already there.
-- patch = { items = {...}, bonuses = { min/mid/max = {...} }, thresholds = { min=,mid=,max= } }
local function amendSet(name, patch)
    if type(name) ~= 'string' or type(patch) ~= 'table' then return end
    local si = byName[name:lower()]
    if not si then dbg('amendSet: no set named ' .. tostring(name)); return end
    local s = data[si]
    if type(patch.items) == 'table' then
        local have = {}
        for _, it in ipairs(s.items) do have[it:lower()] = true end
        for _, it in ipairs(patch.items) do
            if type(it) == 'string' then
                local k = it:lower()
                if not have[k] then have[k] = true; s.items[#s.items + 1] = k end
            end
        end
    end
    if type(patch.bonuses) == 'table' then s.bonuses = mergeBonuses(s.bonuses, patch.bonuses) end
    if type(patch.thresholds) == 'table' then
        s.thresholds = s.thresholds or { min = 2, mid = 4, max = 6 }
        for _, tier in ipairs(TIERS) do
            if patch.thresholds[tier] then s.thresholds[tier] = patch.thresholds[tier] end
        end
    end
    linkItems(si)
    linkIconsForSet(si)
    markDirty(si)
    if ready then
        buildSpellsForSet(si, function()
            cleaned = {}
            knownSpellNames = nil
            stateOf = {}
            recomputeAll()
        end)
    elseif initializing then
        needsRebuild = true
    end
    syncDirtyAll()
    dbg(('amendSet: %s'):format(s.name))
end

local function addItems(name, items)
    if type(name) ~= 'string' or type(items) ~= 'table' then return end
    local si = byName[name:lower()]
    if not si then dbg('addItems: no set named ' .. tostring(name)); return end
    local s = data[si]
    local have = {}
    for _, it in ipairs(s.items) do have[it:lower()] = true end
    for _, it in ipairs(items) do
        if type(it) == 'string' then
            local k = it:lower()
            if not have[k] then have[k] = true; s.items[#s.items + 1] = k end
        end
    end
    linkItems(si)
    linkIconsForSet(si)
    markDirty(si)
    if ready then recomputeAll() end
    syncDirtyAll()
end

local function registerSetLink(t)
    if type(t) ~= 'table' or type(t.item) ~= 'string' or type(t.set) ~= 'string' then return end
    local si = byName[t.set:lower()]
    if not si then dbg('registerSetLink: no set named ' .. tostring(t.set)); return end
    local s = data[si]
    local k = t.item:lower()
    local have = false
    for _, it in ipairs(s.items) do if it:lower() == k then have = true; break end end
    if not have then s.items[#s.items + 1] = k end
    linkItems(si)
    linkIconsForSet(si)
    markDirty(si)
    if ready then recomputeAll() end
    syncDirtyAll()
end

local function getSetsForItem(itemId)
    if type(itemId) ~= 'string' then return nil end
    local links = itemLink[itemId:lower()]
    if not links then return nil end
    local names = {}
    for _, si in ipairs(links) do names[#names + 1] = data[si].name end
    return names
end

local function isItemInSet(itemId, name)
    if type(itemId) ~= 'string' or type(name) ~= 'string' then return false end
    local links = itemLink[itemId:lower()]
    local target = byName[name:lower()]
    if not links or not target then return false end
    for _, si in ipairs(links) do if si == target then return true end end
    return false
end

local function scheduleScaleRebuild()
    scaleRebuildTicket = scaleRebuildTicket + 1
    local ticket = scaleRebuildTicket
    async:newUnsavableSimulationTimer(SCALE_REBUILD_DEBOUNCE, function()
        if ticket ~= scaleRebuildTicket then return end
        if not ready then
            needsRebuild = true
            return
        end
        buildSpells(function()
            if ticket ~= scaleRebuildTicket then return end
            cleaned = {}
            knownSpellNames = nil
            stateOf = {}
            recomputeAll()
        end)
    end)
end

local function init()
    if ready or initializing then return end
    initializing = true
    registerSettings()

    local function finishInitialBuild()
        if needsRebuild then
            needsRebuild = false
            buildSpells(finishInitialBuild)
            return
        end
        initializing = false
        ready = true
        dbg(('initialised: %d spells built, %d failed, %d sets'):format(builtCount, failCount, #data))
        recomputeAll()
    end

    -- Re-apply to everyone whenever runtime settings change. Scale changes
    -- require a new authoritative spell generation; other toggles only require
    -- actor recomputation against the already-installed records.
    cfg:subscribe(async:callback(function(_, key)
        if key == 'scale' or key == 'weaknessScale' then
            scheduleScaleRebuild()
        elseif ready then
            recomputeAll()
        end
    end))
    indexByName()
    buildItemLinks()
    buildSpells(finishInitialBuild)
end

local function tierFor(s, count)
    local t = s.thresholds
    if count >= t.max then return 'max'
    elseif count >= t.mid then return 'mid'
    elseif count >= t.min then return 'min' end
    return nil
end

local function equippedList(actor)
    local list = {}
    for _, slot in pairs(types.Actor.EQUIPMENT_SLOT) do
        local obj = types.Actor.getEquipment(actor, slot)
        if obj then
            local id = obj.recordId and obj.recordId:lower() or nil
            local icon, mesh = iconMeshForObject(obj)
            list[#list + 1] = {
                id = id,
                icon = iconMeshSig(icon, mesh),
                weightClass = armorWeightClassName(obj),
            }
        end
    end
    return list
end

local function recompute(actor)
    init()
    if not actor or not actor:isValid() then return end
    local aid = actor.id
    local isPlayer = types.Player.objectIsInstance(actor)
    local spells = types.Actor.spells(actor)

    if not cleaned[aid] then
        -- Spell record ids are regenerated every session, so match our abilities by
        -- name to purge any left on the actor from a previous save (prevents stacking).
        -- Match against the EXACT names we generate (whitelist), not just a shared
        -- " Bonus" suffix: a suffix-only check also deletes any unrelated ability
        -- whose display name happens to end in " Bonus" -- e.g. tamriel_data's
        -- Dagi-Raht/Reachman magicka-multiplier racial ("magicka mult bonus_5"),
        -- which collided with the old suffix check and got stripped as if it were
        -- one of ours. The whitelist still covers both of our own naming
        -- conventions (tier spells AND conditional per-effect sub-spells), so
        -- leftover copies from a previous save are still purged correctly.
        if not knownSpellNames then knownSpellNames = buildKnownSpellNames() end
        local stale = {}
        for _, sp in pairs(spells) do
            local nm = sp.name
            if nm and knownSpellNames[nm] then stale[#stale + 1] = sp end
        end
        for _, sp in ipairs(stale) do spells:remove(sp) end
        stateOf[aid] = {}
        cleaned[aid] = true
    end

    local new = {}
    if isPlayer or npcBonusesEnabled() then
        local eq = equippedList(actor)
        local useIcon = matchByIconEnabled()
        local candidates = {}
        for _, it in ipairs(eq) do
            -- Icon is a FALLBACK ONLY: if the item is recognised by its id, trust
            -- that and ignore its icon, so mod helms that reuse a vanilla icon (e.g.
            -- the Indoril helmet icon) can't pull an item into unrelated sets. Icon
            -- still catches copies/enchants that have a new id and no id-link.
            local byId = it.id and itemLink[it.id]
            if byId then
                for _, si in ipairs(byId) do candidates[si] = true end
            elseif useIcon and it.icon then
                local byIcon = iconLink[it.icon]
                if byIcon then for _, si in ipairs(byIcon) do candidates[si] = true end end
            end
            -- The item's own computed weight class always counts, even for an
            -- enchanted/unlisted piece with no id-link and no icon-link (weight
            -- sets are deliberately excluded from the icon fallback -- see
            -- ICON_EXCLUDED). This is on top of, not instead of, any themed set
            -- matched above (a Daedric Cuirass is both "Daedric" AND "Heavy Armor").
            if it.weightClass then
                local wsi = byName[it.weightClass:lower()]
                if wsi then candidates[wsi] = true end
            end
        end
        for si in pairs(candidates) do
            local s = data[si]
            local wcName = ICON_EXCLUDED[s.name] and s.name or nil
            local count = 0
            for _, it in ipairs(eq) do
                if wcName then
                    if it.weightClass == wcName then count = count + 1 end
                else
                    local byId = it.id and itemLink[it.id]
                    if listHas(byId, si)
                        or (not byId and useIcon and listHas(it.icon and iconLink[it.icon], si)) then
                        count = count + 1
                    end
                end
            end
            local tier = tierFor(s, count)
            if tier and SPELL[si] and SPELL[si][tier] then new[si] = tier end
        end
    end

    local old = stateOf[aid] or {}
    local union = {}
    for si in pairs(old) do union[si] = true end
    for si in pairs(new) do union[si] = true end
    for si in pairs(union) do
        local o, n = old[si], new[si]
        if o ~= n then
            if o and SPELL[si][o] then spells:remove(SPELL[si][o]) end
            if n and SPELL[si][n] then spells:add(SPELL[si][n]) end
            dbg(('%s: %s -> %s [%s]'):format(tostring(aid), data[si].name, tostring(n), tostring(o)))
            if isPlayer then
                actor:sendEvent('SetBonus_notify', { name = data[si].name, tier = n })
            end
        end
    end
    -- Toggle conditional sub-spells for the actor's active tiers.
    local desired = {}
    if conditionalEnabled() then
        for si, tier in pairs(new) do
            local cl = SPELLCOND[si] and SPELLCOND[si][tier]
            if cl then for _, e in ipairs(cl) do desired[#desired + 1] = e end end
        end
    end
    if #desired > 0 or C.hasApplied(aid) then C.reconcileActor(actor, desired) end
    stateOf[aid] = new
end

-- Re-toggle conditional sub-spells for state that changes without an equip
-- event (health, time, ...). The old loop walked EVERY active actor in one
-- frame each second -- a once-a-second spike that grew with city size (same
-- family as the actor.lua polling report). It is now sliced: each frame
-- handles a few actors, sized so the whole roster is still covered about
-- once per second, and capped so no single frame can spike regardless of
-- how crowded the cell is. Per-actor refresh latency stays ~1s.
local REEVAL_PERIOD = 1.0        -- target seconds to cover every active actor
local REEVAL_MAX_PER_FRAME = 8   -- hard cap on actors reconciled per frame
local condCursor = 1

local function reevalActor(actor)
    if not (actor and actor:isValid()) then return end
    local st = stateOf[actor.id]
    local desired = {}
    if st and conditionalEnabled() then
        for si, tier in pairs(st) do
            local cl = SPELLCOND[si] and SPELLCOND[si][tier]
            if cl then for _, e in ipairs(cl) do desired[#desired + 1] = e end end
        end
    end
    if #desired > 0 or C.hasApplied(actor.id) then
        C.reconcileActor(actor, desired)
    end
end

-- Full pass in one go -- kept for explicit triggers (external flag pushes via
-- SetBonus_setFlag) where an immediate, complete refresh is wanted.
local function reevalConditions()
    if not ready then return end
    for _, actor in ipairs(world.activeActors) do
        reevalActor(actor)
    end
end

local function reevalSlice(dt)
    if not ready then return end
    local actors = world.activeActors
    local n = #actors
    if n == 0 then return end
    local step = math.ceil(n * (dt or 0) / REEVAL_PERIOD)
    if step < 1 then step = 1 end
    if step > REEVAL_MAX_PER_FRAME then step = REEVAL_MAX_PER_FRAME end
    for _ = 1, step do
        if condCursor > n then condCursor = 1 end
        reevalActor(actors[condCursor])
        condCursor = condCursor + 1
    end
end

return {
    interfaceName = 'SetBonus',
    interface = {
        version = 2,
        registerSet = function(sd) return registerSet(sd) end,
        registerSets = function(list) return registerSets(list) end,
        amendSet = function(name, patch) return amendSet(name, patch) end,
        addItems = function(name, items) return addItems(name, items) end,
        registerSetLink = function(t) return registerSetLink(t) end,
        getSet = function(name)
            local si = type(name) == 'string' and byName[name:lower()]
            return si and data[si] or nil
        end,
        getSets = function() return data end,
        getSetsForItem = function(itemId) return getSetsForItem(itemId) end,
        isItemInSet = function(itemId, name) return isItemInSet(itemId, name) end,
        benefitScale = function() return benefitScaleVal() end,
        drawbackScale = function() return drawbackScaleVal() end,
    },
    engineHandlers = {
        -- Push any interop-modified set definitions to a (re)joining player so
        -- the player-side tooltip shows current data, not the shipped file.
        onPlayerAdded = function(player)
            syncDirtyTo(player)
        end,
        onUpdate = function(dt)
            init()
            reevalSlice(dt)
        end,
    },
    eventHandlers = {
        SetBonus_recompute = function(e) recompute(e.actor) end,
        SetBonus_registerSet = function(e) registerSet(e) end,
        -- Batch event: { sets = { {...}, {...} } } or a plain array of sets.
        SetBonus_registerSets = function(e) registerSets(e and e.sets or e) end,
        SetBonus_amendSet = function(e) amendSet(e.name, e.patch) end,
        SetBonus_addItems = function(e) addItems(e.name, e.items) end,
        SetBonus_registerSetLink = function(e) registerSetLink(e) end,
        -- External state hook: any script can push per-actor flags (combat, weather,
        -- custom) that `flag`/`combat`/`weather` conditions read. { actor=, id=, value= }.
        SetBonus_setFlag = function(e)
            if e.actor then C.setFlag(e.actor.id, e.id, e.value) end
            reevalConditions()
        end,
    },
}
