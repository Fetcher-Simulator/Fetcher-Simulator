-- Original two-string phrase and a drone, with the same bass/drum accompaniment.
-- Render exported files with score-audition; no MIDI or new sample assets needed.
local root='files/server/server-lua-packages/fetcher.procedural-synth/'
local schema=dofile(root..'shared/schema.lua');local f=dofile(root..'shared/project.lua')(schema)
local S=dofile(root..'shared/sequence.lua');local MIDI=dofile(root..'client/midi.lua')
local folder=assert(arg[1],'Provide an existing output directory')
local presets={};for _,p in ipairs(schema.builtins) do presets[p.id]=p end
for _,id in ipairs{'guitar-clean','sampled-electric-clean'} do
    for _,variant in ipairs{{name='poly',mode=0,layout=0},{name='mono',mode=1,layout=0},{name='strings',mode=1,layout=1},{name='picked-strings',mode=2,layout=1}} do
        local p=f.new();p.length=32;p.loop=false
        p.tracks[1]=f.newTrack('piano','Independent guitar strings',32)
        local t=p.tracks[1];t.params=schema.state(assert(presets['b:'..id]).state)
        t.params.stringLegato=variant.mode;t.params.stringLayout=variant.layout;t.params.stringSlideTime=.09
        t.level=.65;t.notes={{n=40,s=0,l=24,v=.75},{n=45,s=1,l=22,v=.7},{n=50,s=2,l=22,v=.7},
            {n=43,s=6,l=5,v=.6},{n=48,s=8,l=6,v=.6},{n=42,s=16,l=4,v=.8},{n=47,s=18,l=3,v=.65}}
        t.automation={stringSelect={{s=0,v=1,linear=false},{s=1,v=2,linear=false},{s=2,v=3,linear=false},
            {s=6,v=1,linear=false},{s=8,v=2,linear=false},{s=16,v=1,linear=false},{s=18,v=2,linear=false}}}
        p.tracks[2]=f.newTrack('piano','Bass',32);local bass=p.tracks[2]
        bass.params=schema.state(presets['b:bass-finger'].state);bass.level=.55
        bass.notes={{n=28,s=0,l=7,v=.7},{n=31,s=8,l=7,v=.75},{n=28,s=16,l=7,v=.8},{n=35,s=24,l=6,v=.7}}
        for n,entry in ipairs{{'kick',36},{'snare',38},{'hat',42}} do
            local lane=f.newTrack('drum',entry[1],32);p.tracks[n+2]=lane
            lane.params=schema.state(assert(presets['b:'..entry[1]]).state);lane.note=entry[2];lane.level=.65
            local steps={};for step=0,31 do steps[#steps+1]=(n==1 and step%8==0 or n==2 and step%8==4 or n==3 and step%2==0) and '8' or '0' end
            lane.steps=table.concat(steps)
        end
        p=assert(f.project(p));local score=S.compile(p,nil,MIDI,'clips',f)
        local stem=folder..'/'..id..'-'..variant.name
        local out=assert(io.open(stem..'.bin','wb'));out:write(score.bytes);out:close()
        out=assert(io.open(stem..'.tsv','wb'));out:write('length='..score.length..'\n')
        for lane,track in ipairs(p.tracks) do
            out:write('@'..lane..'\t'..track.level..'\t'..(lane==1 and 'guitar' or lane==2 and 'bass' or 'drums')..'\n')
            for _,key in ipairs(schema.order) do out:write(key..'='..string.format('%.9g',track.params[key])..'\n') end
            if track.params.sample then out:write('sample='..track.params.sample.path..'\n') end
        end
        out:close()
    end
end
