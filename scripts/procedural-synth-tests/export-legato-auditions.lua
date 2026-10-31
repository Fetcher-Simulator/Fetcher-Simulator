-- Original held bass/lead phrase: overlapping notes, held-note returns, then a gap.
local root='files/server/server-lua-packages/fetcher.procedural-synth/'
local schema=dofile(root..'shared/schema.lua');local f=dofile(root..'shared/project.lua')(schema)
local S=dofile(root..'shared/sequence.lua');local MIDI=dofile(root..'client/midi.lua')
local folder=assert(arg[1],'Provide an existing output directory')
for _,id in ipairs{'guitar-lead','sampled-electric-lead'} do
    local preset;for _,p in ipairs(schema.builtins) do if p.id=='b:'..id then preset=p end end;assert(preset)
    for mode=0,2 do
        local p=f.new();p.length=32;p.loop=false;p.tracks[1]=f.newTrack('piano',preset.name,32)
        local t=p.tracks[1];t.params=schema.state(preset.state);t.params.stringLegato=mode;t.params.stringSlideTime=.1
        t.notes={{n=40,s=0,l=16,v=.8},{n=43,s=4,l=3,v=.6},{n=47,s=8,l=4,v=.7},
            {n=45,s=12,l=8,v=.65},{n=50,s=14,l=2,v=.9},{n=52,s=24,l=4,v=.75}}
        local score=S.compile(p,nil,MIDI,'clips',f);local stem=folder..'/'..id..'-'..({'poly','legato','picked'})[mode+1]
        local out=assert(io.open(stem..'.bin','wb'));out:write(score.bytes);out:close()
        out=assert(io.open(stem..'.tsv','wb'));out:write('length='..score.length..'\n@1\t0.8\tguitar\n')
        for _,key in ipairs(schema.order) do out:write(key..'='..string.format('%.9g',t.params[key])..'\n') end
        if t.params.sample then out:write('sample='..t.params.sample.path..'\n') end;out:close()
    end
end
