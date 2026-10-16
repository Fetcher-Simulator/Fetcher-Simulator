-- Versioned wire/persistence format. Kept identical to the package shared/schema.lua.
local M = { version = 1, specs = {}, order = {} }
local function p(name, low, high, default, discrete)
    M.specs[name] = { min = low, max = high, default = default, discrete = discrete }
    M.order[#M.order + 1] = name
end
p('wave1',0,3,0,true); p('volume1',0,1,0.8); p('tune1',-24,24,0); p('fine1',-100,100,0)
p('enabled2',0,1,0,true); p('wave2',0,3,2,true); p('volume2',0,1,0.4)
p('tune2',-24,24,0); p('fine2',-100,100,7)
p('attack',0.002,5,0.01); p('decay',0.002,5,0.2); p('sustain',0,1,0.7); p('release',0.005,8,0.3)
p('cutoff',20,18000,8000); p('resonance',0,0.95,0.1); p('filterAmount',-8,8,0)
p('filterAttack',0.002,5,0.01); p('filterDecay',0.002,5,0.3); p('filterSustain',0,1,0); p('filterRelease',0.005,8,0.3)
p('lfoRate',0.05,20,4); p('lfoDepth',0,1,0); p('lfoTarget',0,2,0,true)
p('volume',0,1,0.7); p('gain',0,2,1); p('polyphony',1,32,16,true)
function M.valid(name, value)
    local s = M.specs[name]
    return s and type(value) == 'number' and value == value and value >= s.min and value <= s.max
        and (not s.discrete or value % 1 == 0)
end
function M.state(input)
    if type(input) ~= 'table' or (input.version ~= nil and input.version ~= 1) then return nil end
    local count, result = 0, { version = 1 }
    for key, value in pairs(input) do
        count = count + 1
        if count > 27 or (key ~= 'version' and not M.valid(key, value)) then return nil end
    end
    -- Missing fields in old version-1 saves receive stable defaults.
    for name, spec in pairs(M.specs) do result[name] = input[name] or spec.default end
    return result
end
M.builtins = {
    {id='b:pad', name='Soft Pad', state={wave1=2, enabled2=1, wave2=2, fine2=12, attack=0.9, release=2, cutoff=1800}},
    {id='b:pluck', name='Plucked String', state={wave1=1, attack=0.002, decay=0.4, sustain=0, release=0.2, cutoff=3000, filterAmount=2}},
    {id='b:lead', name='Retro Lead', state={wave1=2, enabled2=1, wave2=3, volume2=0.2, cutoff=6500, lfoDepth=0.08}},
    {id='b:bass', name='Bass', state={wave1=3, tune1=-12, cutoff=600, resonance=0.3, filterAmount=3, decay=0.18, sustain=0.5}},
    {id='b:flute', name='Flute-like', state={wave1=0, enabled2=1, wave2=0, tune2=12, volume2=0.1, attack=0.08, cutoff=3500, lfoDepth=0.08, lfoRate=5}},
}
for _, preset in ipairs(M.builtins) do preset.state = M.state(preset.state) end
return M
