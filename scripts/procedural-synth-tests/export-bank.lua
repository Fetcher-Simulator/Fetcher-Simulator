-- lua scripts/procedural-synth-tests/export-bank.lua <parameters.tsv> [Guitar]
local schema=dofile('files/server/server-lua-packages/fetcher.procedural-synth/shared/schema.lua')
local output=assert(io.open(assert(arg[1]),'wb'))
for _,preset in ipairs(schema.builtins) do
    if not arg[2] or schema.inBank(preset,arg[2]) then
        output:write('@'..preset.name..'\t'..preset.role..'\n')
        for _,name in ipairs(schema.order) do output:write(name..'='..string.format('%.9g',preset.state[name])..'\n') end
    end
end
output:close()
