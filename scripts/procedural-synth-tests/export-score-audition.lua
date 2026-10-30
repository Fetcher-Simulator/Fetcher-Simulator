-- Export a MIDI's compiled score and real preset snapshots for score-audition.
-- lua scripts/procedural-synth-tests/export-score-audition.lua MIDI SOUND_SET OUTPUT_PREFIX
assert(arg[1] and arg[2] and arg[3],'Provide MIDI path, sound-set ID and output prefix')
local root='files/server/server-lua-packages/fetcher.procedural-synth/';local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=dofile(root..'shared/schema.lua');local MIDI=dofile(root..'client/midi.lua');local S=dofile(root..'shared/sequence.lua')
package.loaded[prefix..'shared.schema']=schema;package.loaded[prefix..'client.soundsets']=dofile(root..'client/soundsets.lua');package.loaded[prefix..'shared.project']=dofile(root..'shared/project.lua');package.loaded[prefix..'client.midi']=MIDI
local sets=package.loaded[prefix..'client.soundsets'];local format=package.loaded[prefix..'shared.project'](schema)
local f=assert(io.open(arg[1],'rb'));local song=MIDI.parse(f:read('*a'));f:close()
local W=dofile(root..'client/workstation.lua');local w=W.new{panic=function()end,configure=function()end,message=print}
assert(w:import(song,'midi/audition.mid'));assert(w:applySoundSet(arg[2]))
local score=S.compile(w.project,song,MIDI,'song',format,sets.programPatch)
f=assert(io.open(arg[3]..'.bin','wb'));f:write(score.bytes);f:close();f=assert(io.open(arg[3]..'.tsv','wb'));f:write('length='..score.length..'\n')
for n,t in ipairs(w.project.tracks) do
 local sample=t.params.sample;local group=sample and sample.path:find('drum%-kit') and 'drums' or t.sourceRole
 if group~='guitar' and group~='bass' and group~='drums' then group='other' end
 f:write('@'..n..'\t'..t.level..'\t'..group..'\n')
 for _,key in ipairs(schema.order) do f:write(key..'='..string.format('%.9g',t.params[key])..'\n') end
 if sample then f:write('sample='..sample.path..'\n') end
 print(n..' '..t.name..' program='..tostring(t.sourceProgram)..' '..t.presetId)
end
f:close();print('score notes='..(#score.bytes/11)..' duration='..score.length/S.rate)

