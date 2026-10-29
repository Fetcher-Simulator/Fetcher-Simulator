-- Render with synth-tests --bank-preview-matched <file.tsv> <file.wav>.
-- Each sound gets 1 second held/struck + 0.75 seconds release at 48 kHz mono.
local schema=dofile('files/server/server-lua-packages/fetcher.procedural-synth/shared/schema.lua')
local presets={};for _,p in ipairs(schema.builtins) do presets[p.id]=p end
local folder=assert(arg[1],'Provide an existing output directory')
local groups={
 electric={'guitar-clean','guitar-heavy','guitar-thrash','guitar-altmetal','guitar-funk'},
 acoustic={'guitar-nylon','guitar-steel'},
 bass={'bass-acoustic','bass-finger','bass-picked','bass-slap','bass-pop'},
}
local function write(output,preset,name,frequency)
 output:write('@'..(name or preset.name)..'\t'..preset.role..'\n')
 for _,key in ipairs(schema.order) do output:write(key..'='..string.format('%.9g',key=='bodyFrequency' and frequency or preset.state[key])..'\n') end
end
for name,ids in pairs(groups) do
 local output=assert(io.open(folder..'/'..name..'.tsv','wb'))
 for _,id in ipairs(ids) do write(output,assert(presets['b:'..id])) end;output:close()
end
local output=assert(io.open(folder..'/kit.tsv','wb'))
local toms={[50]=220,[47]=155,[43]=105}
for _,lane in ipairs(schema.kits[6].lanes) do write(output,assert(presets[lane.id]),lane.name,toms[lane.note]) end
output:close()
