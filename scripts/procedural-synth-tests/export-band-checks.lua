-- Original editable projects, rendered with the same compiler, patches and mix as Studio.
local folder=assert(arg[1],'Provide an existing output directory')
local root='files/server/server-lua-packages/fetcher.procedural-synth/';local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=dofile(root..'shared/schema.lua');local factory=dofile(root..'shared/project.lua');local F=factory(schema)
package.loaded[prefix..'shared.schema']=schema;package.loaded[prefix..'shared.project']=factory
package.loaded[prefix..'client.soundsets']=dofile(root..'client/soundsets.lua')
local Exercises=dofile(root..'client/band-exercises.lua');local Sequence=dofile(root..'shared/sequence.lua');local MIDI=dofile(root..'client/midi.lua')
local list=assert(io.open(folder..'/exercises.tsv','wb'));list:write('id\tname\tbpm\tsoundSet\tseconds\tnotes\n')
for _,entry in ipairs(Exercises.entries) do
    local project=assert(Exercises.create(entry.id,arg[2]));local count=0;for _,t in ipairs(project.tracks) do count=count+#assert(F.decodeNotes(t.edited)) end
    list:write(entry.id..'\t'..entry.name..'\t'..entry.bpm..'\t'..project.soundSet..'\t'..(16*60/entry.bpm+1)..'\t'..count..'\n')
    for _,mode in ipairs{'patch','authored','source'} do
        local p=assert(F.project(project))
        for _,t in ipairs(p.tracks) do
            if mode=='patch' then local notes=assert(F.decodeNotes(t.edited));for _,n in ipairs(notes) do n.e=nil end;t.edited=assert(F.encodeNotes(notes,960)) end
            if mode=='source' and t.sourceRole=='guitar' then
                t.params.fxMix=0;t.params.parallelMix=0;t.params.driveType=0;t.params.cabinet=0
            end
        end
        local score=Sequence.compile(p,nil,MIDI,'song',F)
        local stem=folder..'/'..entry.id..'-'..mode
        local f=assert(io.open(stem..'.bin','wb'));f:write(score.bytes);f:close()
        f=assert(io.open(stem..'.tsv','wb'));f:write('length='..score.length..'\nmaster='..p.master..'\n')
        for _,key in ipairs(F.effectsOrder) do f:write('effects.'..key..'='..p.effects[key]..'\n') end
        for n,t in ipairs(p.tracks) do
            local group=t.sourceRole=='guitar' and 'guitar' or t.sourceRole=='bass' and 'bass' or 'drums'
            f:write('@'..n..'\t'..t.level..'\t'..group..'\t1\n')
            for _,key in ipairs(schema.order) do f:write(key..'='..string.format('%.9g',t.params[key])..'\n') end
            for _,key in ipairs(F.mixerOrder) do f:write('mix.'..key..'='..string.format('%.9g',t.mixer[key])..'\n') end
            if t.params.sample then
                f:write('sample='..t.params.sample.path..'\n')
                f:write('sampleGain='..(t.params.sample.gain or 1)..'\n')
                for _,attack in ipairs(schema.attackOrder) do local ref=t.params.sample.attacks and t.params.sample.attacks[attack]
                    if ref then f:write('sample.'..attack..'='..ref.path..'\nsampleGain.'..attack..'='..(ref.gain or 1)..'\n') end
                end
            end
        end
        f:close()
    end
end
list:close()
