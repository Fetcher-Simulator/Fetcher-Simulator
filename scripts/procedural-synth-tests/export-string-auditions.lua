-- Original four-bar phrase: Open / Palm / Dead / Open, generated and recorded.
-- Output directory must exist. Render .bin/.tsv with score-audition.
local root='files/server/server-lua-packages/fetcher.procedural-synth/'
local schema=dofile(root..'shared/schema.lua');local format=dofile(root..'shared/project.lua')(schema)
local Sequence=dofile(root..'shared/sequence.lua');local MIDI=dofile(root..'client/midi.lua')
local folder=assert(arg[1],'Provide an output directory')
for _,entry in ipairs{{'generated','b:guitar-thrash'},{'recorded','b:sampled-thrash-rhythm'}} do
    local preset;for _,p in ipairs(schema.builtins) do if p.id==entry[2] then preset=p end end;assert(preset)
    local p=format.new();p.length=64;p.loop=false;p.tracks[1]=format.newTrack('piano','Expression exercise',64)
    local t=p.tracks[1];t.params=schema.state(preset.state);t.presetId=preset.id
    for bar=0,3 do for step=0,12,4 do
        for _,pitch in ipairs{40,47,52} do t.notes[#t.notes+1]={n=pitch,s=bar*16+step,l=2,v=step%8==0 and .9 or .65} end
    end end
    t.automation={stringArticulation={{s=0,v=1,linear=false},{s=16,v=2,linear=false},{s=32,v=3,linear=false},{s=48,v=1,linear=false}}}
    local score=Sequence.compile(p,nil,MIDI,'clips',format)
    local f=assert(io.open(folder..'/'..entry[1]..'.bin','wb'));f:write(score.bytes);f:close()
    f=assert(io.open(folder..'/'..entry[1]..'.tsv','wb'));f:write('length='..score.length..'\n@1\t0.8\tguitar\n')
    for _,key in ipairs(schema.order) do f:write(key..'='..string.format('%.9g',t.params[key])..'\n') end
    if t.params.sample then f:write('sample='..t.params.sample.path..'\n') end;f:close()
end
