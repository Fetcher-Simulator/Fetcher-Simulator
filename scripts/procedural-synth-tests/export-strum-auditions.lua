-- Original E minor / G / A minor / D progression, same notes before/after.
local root='files/server/server-lua-packages/fetcher.procedural-synth/'
local schema=dofile(root..'shared/schema.lua');local f=dofile(root..'shared/project.lua')(schema)
local Tools=dofile(root..'client/guitar-tools.lua');local Sequence=dofile(root..'shared/sequence.lua');local MIDI=dofile(root..'client/midi.lua')
local folder=assert(arg[1],'Provide an existing output directory')
local notes={};local chords={{40,47,52,55,59,64},{43,47,50,55,59,67},{45,52,57,60,64},{50,57,62,66}}
for bar,pitches in ipairs(chords) do for hit=0,3 do for _,pitch in ipairs(pitches) do
    notes[#notes+1]={n=pitch,s=(bar-1)*16+hit*4,l=3.5,v=hit%2==0 and .85 or .65}
end end end
local clock={quantum=1/960,seconds=function(s) return s/8 end,shift=function(s,delay) return s+delay*8 end}
local plan=assert(Tools.plan(notes,{direction=2,spacing=8},clock))
for _,id in ipairs{'guitar-steel','sampled-guitar-steel'} do
    local preset;for _,p in ipairs(schema.builtins) do if p.id=='b:'..id then preset=p end end;assert(preset)
    for _,mode in ipairs{'reference','strum'} do
        local p=f.new();p.length=64;p.loop=false;p.tracks[1]=f.newTrack('piano',preset.name,64)
        local t=p.tracks[1];t.params=schema.state(preset.state);t.presetId=preset.id;t.notes=mode=='strum' and plan.notes or notes
        local score=Sequence.compile(p,nil,MIDI,'clips',f);local stem=folder..'/'..id..'-'..mode
        local out=assert(io.open(stem..'.bin','wb'));out:write(score.bytes);out:close()
        out=assert(io.open(stem..'.tsv','wb'));out:write('length='..score.length..'\n@1\t0.8\tguitar\n')
        for _,key in ipairs(schema.order) do out:write(key..'='..string.format('%.9g',t.params[key])..'\n') end
        if t.params.sample then out:write('sample='..t.params.sample.path..'\n') end;out:close()
    end
end
print(string.format('%d notes, %d gestures, %d delayed picks, %d shortened gates',#notes,plan.groups,plan.shifted,plan.choked))
