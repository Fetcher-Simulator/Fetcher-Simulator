-- Original four-bar riff with bass and drums; no imported/copyrighted song.
-- Render the resulting score/config pairs with score-audition (0 9 all).
local root='files/server/server-lua-packages/fetcher.procedural-synth/'
local schema=dofile(root..'shared/schema.lua');local format=dofile(root..'shared/project.lua')(schema)
local Sequence=dofile(root..'shared/sequence.lua');local MIDI=dofile(root..'client/midi.lua')
local folder=assert(arg[1],'Provide an existing output directory')
local presets={};for _,preset in ipairs(schema.builtins) do presets[preset.id]=preset end
for _,mode in ipairs{'reference','chug','saw','bass'} do
    local p=format.new();p.length=64;p.loop=false
    local ids={'b:doom-parallel-chug',mode=='bass' and 'b:doom-parallel-bass' or 'b:bass-picked','b:kit-kick','b:kit-snare','b:kit-pedalhat'}
    if mode=='saw' then ids[1]='b:doom-parallel-saw' end
    local groups={'guitar','bass','drums','drums','drums'}
    for n,id in ipairs(ids) do
        local preset=assert(presets[id],id)
        local t=format.newTrack('piano',preset.name,64);p.tracks[n]=t;t.params=schema.state(preset.state);t.presetId=id
        if mode=='reference' then t.params.parallelMix=0 end
    end
    local function note(track,n,s,l,v) local notes=p.tracks[track].notes;notes[#notes+1]={n=n,s=s,l=l,v=v} end
    for bar=0,3 do
        local start=bar*16;local rootNote=({40,40,43,42})[bar+1]
        for step=0,14,2 do
            for _,offset in ipairs{0,7,12} do note(1,rootNote+offset,start+step,step==14 and 1.6 or .8,step%4==0 and .85 or .65) end
            note(2,rootNote,start+step,1.5,.8)
            note(5,42,start+step,.5,step%4==0 and .7 or .45)
        end
        for _,step in ipairs{0,6,8,10} do note(3,36,start+step,.5,.9) end
        for _,step in ipairs{4,12} do note(4,38,start+step,.5,.85) end
    end
    local score=Sequence.compile(p,nil,MIDI,'clips',format)
    local f=assert(io.open(folder..'/'..mode..'.bin','wb'));f:write(score.bytes);f:close()
    f=assert(io.open(folder..'/'..mode..'.tsv','wb'));f:write('length='..score.length..'\n')
    for n,t in ipairs(p.tracks) do
        f:write('@'..n..'\t0.8\t'..groups[n]..'\n')
        for _,key in ipairs(schema.order) do f:write(key..'='..string.format('%.9g',t.params[key])..'\n') end
    end
    f:close()
end
