local root='files/server/server-lua-packages/fetcher.procedural-synth/'
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=dofile(root..'shared/schema.lua');local format=dofile(root..'shared/project.lua')(schema)
local MIDI=dofile(root..'client/midi.lua');local Sequence=dofile(root..'shared/sequence.lua')
package.loaded[prefix..'shared.schema']=schema;package.loaded[prefix..'shared.project']=dofile(root..'shared/project.lua')
package.loaded[prefix..'client.midi']=MIDI;package.loaded[prefix..'shared.sequence']=Sequence
package.loaded[prefix..'client.soundsets']=dofile(root..'client/soundsets.lua')
local Export=dofile(root..'client/midi-export.lua');package.loaded[prefix..'client.midi-export']=Export
local W=dofile(root..'client/workstation.lua');local checks=0
local function check(b,m) checks=checks+1;assert(b,m) end
local p=format.new();p.name='Export QA';p.timeline={beats=16,tempos={{beat=0,tempo=500000},{beat=4,tempo=1000000}}}
local t=format.newTrack('midi',string.rep('N',48),16);p.tracks[1]=t;t.part='2:0';t.sourceProgram=24;t.edited=format.encodeNotes({{n=60,s=0,l=4,v=.8}},960)
t.arrangement={clips={{name='Theme',length=16,notes=format.encodeNotes({{n=67,s=0,l=2,v=.5}},960)}},regions={{clip=1,s=16,l=16,transpose=12,mute=false},{clip=1,s=32,l=16,transpose=0,mute=true}}}
p.tracks[2]=format.newTrack('drum','Snare',16);p.tracks[2].note=38;p.tracks[2].steps='0900000000000000'..string.rep('0',240)
local bytes=Export.encode(p,nil,MIDI,format);local song=MIDI.parse(bytes)
check(song.division==960 and song.tracks==3 and song.names[2]==t.name and song.names[3]=='Snare','format 1, conductor and full 48-byte track names')
check(#song.tempos==2 and song.tempos[2].beat==4 and song.tempos[2].tempo==1000000,'tempo map round trip')
local spans=MIDI.noteSpans(song);local notes=spans['2:0']
check(#notes==2 and notes[1].n==60 and notes[1].s==0 and notes[1].l==4,'direct PPQ notes retain pitch/onset/gate')
check(notes[2].n==79 and notes[2].s==16 and notes[2].l==2,'linked placement flattened with transpose and mute')
local percussion=false;for _,e in ipairs(song.events) do if e.kind=='on' and e.note==38 then percussion=e.channel==9 end end
check(percussion,'drum lane maps to MIDI percussion channel')
check(song.programs['2:0']==24,'source-free edited guitar retains its original program in exported MIDI')
local cymbal=format.new();cymbal.timeline={beats=4,tempos={{beat=0,tempo=500000}}}
cymbal.tracks[1]=format.newTrack('midi','Ride',16);cymbal.tracks[1].sourceRole='ride';cymbal.tracks[1].edited=format.encodeNotes({{n=51,s=0,l=1,v=.8}},960)
local round=MIDI.parse(Export.encode(cymbal,nil,MIDI,format))
check(round.events[1].channel==9 and round.events[1].note==51,'authored source-free cymbal exports on the percussion channel')
local w=W.new{message=function() end,configure=function() end,panic=function() end,exportMidi=function(name,data) check(name=='New song' and MIDI.parse(data),'export callback receives validated MIDI');return 'file.mid' end}
assert(w:newSong(120,6,8,4));assert(w:addNote(1,60,0));w:setComposition{loop=true,loopStart=2,loopEnd=3}
local authored=Export.encode(w.project,nil,MIDI,format);local result=MIDI.parse(authored)
check(authored:find(string.char(255,88,4,6,3,24,8),1,true) and result.events[1].tick==0,'authored meter metadata and full song rather than just active loop')
check(w.project.composition.loopStart==2 and w.project.composition.loop,'export leaves project loop settings untouched')
local old=w.project.tracks[1].name;local generation=w.generation
local sampled=format.new();sampled.timeline={beats=4,tempos={{beat=0,tempo=500000}}}
sampled.tracks[1]=format.newTrack('piano','Recorded guitar',16);sampled.tracks[1].notes={{n=60,s=0,l=1,v=.8}}
for _,p in ipairs(schema.builtins) do if p.id=='b:sampled-electric-clean' then sampled.tracks[1].params=p.state;sampled.tracks[1].presetId=p.id end end
check(MIDI.parse(Export.encode(sampled,nil,MIDI,format)).programs['2:0']==27,'authored sampled guitar exports the clean-electric GM program')
for id,program in pairs{['b:sampled-bass-finger']=33,['b:sampled-bass-picked']=34,['b:sampled-guitar-nylon']=24,['b:sampled-guitar-steel']=25} do
    for _,p in ipairs(schema.builtins) do if p.id==id then sampled.tracks[1].params=p.state;sampled.tracks[1].presetId=id end end
    check(MIDI.parse(Export.encode(sampled,nil,MIDI,format)).programs['2:0']==program,'authored recorded technique retains its GM export identity')
end
check(w:renameTrack(1,'Voice chop') and w.generation==generation+1,'explicit undoable track rename')
check(w:undo() and w.project.tracks[1].name==old and w:redo() and w.project.tracks[1].name=='Voice chop','rename undo/redo')
check(format.project(w.project).tracks[1].name=='Voice chop' and MIDI.parse(Export.encode(w.project,nil,MIDI,format)).names[2]=='Voice chop','name persists in project and MIDI')
check(not w:renameTrack(1,'   ') and not w:renameTrack(1,'bad\nname') and not w:renameTrack(1,string.rep('x',49)),'invalid track names preserve current name')
check(w:exportMidi(),'model invokes native writer callback')
local sampleTrack=w.project.tracks[1]
sampleTrack.params.sample={path='sound/vo/d/m/hlo_dm001.mp3',offset=.2,duration=1,root=60,oneShot=true,hash=string.rep('a',32)}
sampleTrack.mixer.bass=3;local sound,route,notes=sampleTrack.params,sampleTrack.output,sampleTrack.edited
check(w:renameTrack(1,'Sample phrase') and sampleTrack.params==sound and sampleTrack.output==route and sampleTrack.edited==notes and sampleTrack.mixer.bass==3,'renaming sample part retains sound, mix, notes and route')
local saved=format.project(w.project);local midi=Export.encode(saved,nil,MIDI,format)
check(saved.tracks[1].params.sample.offset==.2 and MIDI.parse(midi).names[2]=='Sample phrase' and not midi:find('hlo_dm001',1,true),'sample name round trip without embedding sample data')
local many=format.new();for n=1,16 do many.tracks[n]=format.newTrack('piano','Part '..n,16);many.tracks[n].notes={{n=60+n,s=0,l=1,v=1}} end
local all=MIDI.parse(Export.encode(many,nil,MIDI,format));check(#MIDI.parts(all)==16,'all sixteen melodic parts round trip')
check(not pcall(Export.encode,format.new(),nil,MIDI,format),'empty export fails without creating a file')
if arg[1] then local file=assert(io.open(arg[1],'rb'));local original=MIDI.parse(file:read('*a'));file:close()
    local imported=W.new{configure=function() end,panic=function() end,message=function() end}
    assert(imported:import(original,'midi/test.mid'));local data=Export.encode(imported.project,original,MIDI,format)
    local round=MIDI.parse(data);check(#MIDI.parts(round)==#MIDI.parts(original),'real Bardcraft MIDI parts survive export/reimport')
end
print('PASS MIDI export names, PPQ/gates, tempo/meter, percussion, linked clips, sixteen parts, rename history and callback; '..checks..' assertions')
