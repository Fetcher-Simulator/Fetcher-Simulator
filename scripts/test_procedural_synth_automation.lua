local root='files/server/server-lua-packages/fetcher.procedural-synth/';local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=dofile(root..'shared/schema.lua');local format=dofile(root..'shared/project.lua')(schema)
local MIDI=dofile(root..'client/midi.lua');local Sequence=dofile(root..'shared/sequence.lua')
package.loaded[prefix..'shared.schema']=schema;package.loaded[prefix..'shared.project']=dofile(root..'shared/project.lua')
package.loaded[prefix..'client.midi']=MIDI;package.loaded[prefix..'shared.sequence']=Sequence
local SoundSets=dofile(root..'client/soundsets.lua');package.loaded[prefix..'client.soundsets']=SoundSets
local W=dofile(root..'client/workstation.lua');local Export=dofile(root..'client/midi-export.lua')
local checks=0;local function check(ok,msg) checks=checks+1;assert(ok,msg) end
local function copy(t) if type(t)~='table' then return t end;local out={};for k,v in pairs(t) do out[k]=copy(v) end;return out end
local function le(bytes,at) local a,b,c,d=bytes:byte(at,at+3);return a+b*256+c*65536+d*16777216 end
local function records(plan,pitch) local out={};for at=1,#plan.bytes,11 do if plan.bytes:byte(at+9)==pitch then out[#out+1]={f=le(plan.bytes,at),v=le(plan.bytes,at+4),lane=plan.bytes:byte(at+8),mode=plan.bytes:byte(at+10)} end end;return out end
local queued=0;local playing,time=true,0;local messages={}
local cb={configure=function() end,panic=function() end,stopSequence=function() end,message=function(m) messages[#messages+1]=m end,
    schedule=function() queued=queued+1;return true end,progress=function() return playing,time,0 end}
local w=W.new(cb);assert(w:newSong(120,4,4,8));assert(w:addNote(1,69,0));w.noteLength=16;assert(w:addNote(1,72,16))
do
    local phrase=W.new(cb);assert(phrase:newSong(120,4,4,4));assert(phrase:addNote(1,48,0));assert(phrase:addNote(1,55,16))
    check(phrase:setAutomation(1,'stringArticulation',0,1,false) and phrase:setAutomation(1,'stringArticulation',16,2,false),'authored open and palm-muted phrases are editable automation')
    check(not phrase:setAutomation(1,'stringArticulation',8,1.5,false),'articulation rejects fractional modes')
    local document=assert(format.project(phrase.project));local plan=Sequence.compile(document,nil,MIDI,'song',format,SoundSets.programPatch)
    local modes=records(plan,191);check(#modes==2 and modes[1].v==21845 and modes[2].v==43690 and modes[2].mode==0,'articulation has a stable stepped wire slot')
    check(format.automationSpecs.pitchBend.code==183 and format.automationSpecs.pickDirection.code==197,'new expression controls preserve existing MIDI codes')
end
check(format.automationSpecs.pitchBend.code==183 and format.automationSpecs.pedal.code==187 and format.automationSpecs.preHighpass.code==188,'new filter controls preserve existing MIDI wire slots')
do
    local probe=assert(format.project(w.project));probe.tracks[1].params.cabinet=6;probe.tracks[1].params.preHighpass=120
    probe.tracks[1].automation={preHighpass={{s=0,v=80,linear=true},{s=8,v=180,linear=false}}}
    local persisted=assert(format.project(probe));local score=Sequence.compile(persisted,nil,MIDI,'song',format,SoundSets.programPatch)
    check(#records(score,188)==2 and persisted.tracks[1].params.cabinet==6,'IR choice and new filter automation survive project validation and compilation')
end
do
    local probe=assert(format.project(w.project));local t=probe.tracks[1]
    t.params.parallelMix=.7;t.params.parallelShape=2;t.params.parallelLift=8
    t.automation={parallelMix={{s=0,v=.2,linear=true},{s=8,v=.8,linear=false}},parallelShape={{s=0,v=2,linear=false},{s=8,v=3,linear=false}}}
    local saved=assert(format.project(probe));local plan=Sequence.compile(saved,nil,MIDI,'song',format,SoundSets.programPatch)
    check(#records(plan,198)==2 and records(plan,198)[1].mode==128 and #records(plan,203)==2,'parallel continuous and stepped automation use extension slots')
    check(saved.tracks[1].params.parallelLift==8 and saved.tracks[1].params.parallelShape==2,'parallel snapshots survive strict project validation')
    check(format.automationSpecs.parallelLift.code==209 and format.automationSpecs.pitchBend.code==183,'parallel extension preserves MIDI performance codes')
end
do
    local probe=assert(format.project(w.project));local t=probe.tracks[1]
    t.params.stringLegato=1;t.params.stringSlideTime=.1
    t.automation={stringLegato={{s=0,v=1,linear=false},{s=8,v=2,linear=false}},stringSlideTime={{s=0,v=.1,linear=true},{s=8,v=.2,linear=false}}}
    local saved=assert(format.project(probe));local plan=Sequence.compile(saved,nil,MIDI,'song',format,SoundSets.programPatch)
    check(records(plan,210)[1].v==32768 and records(plan,210)[2].v==65535 and records(plan,210)[1].mode==0,'legato/picked modes compile to a stepped extension slot')
    check(records(plan,211)[1].mode==128 and saved.tracks[1].params.stringSlideTime==.1,'slide timing and connection settings survive save/compile')
    check(format.automationSpecs.pitchBend.code==183 and schema.state({}).stringLegato==0,'new fields preserve MIDI codes and default old sounds to polyphonic')
end
check(w:setAutomation(1,'cutoff',0,100,true) and w:setAutomation(1,'cutoff',16,18000,false),'editable continuous curve')
check(w:automationValue(1,'cutoff',8)==9050,'linear musical interpolation')
check(w:setAutomation(1,'pedal',0,1,true) and not w.project.tracks[1].automation.pedal[1].linear,'discrete controls always step')
local old=copy(w.project)
check(not w:setAutomation(1,'cutoff',4,0/0,true) and not w:setAutomation(1,'cutoff',-1,200,true),'invalid curve edits reject atomically')
check(not w:setAutomation(1,'program',4,1.2,false) and not w:setAutomation(1,'noSuchParameter',0,1,false),'unknown/discrete values rejected')
check(w:setAutomation(1,'cutoff',20,4000,false,false,16) and #w.project.tracks[1].automation.cutoff==2,'drag move replaces old point in one edit')
check(w:undo() and w.project.tracks[1].automation.cutoff[2].s==16 and w:redo(),'curve Undo/Redo')
check(w:setAutomation(1,'cutoff',20,nil,false,true) and #w.project.tracks[1].automation.cutoff==1,'point deletion')
check(w:undo(),'delete undo')
local saved=assert(format.project(w.project));local loaded=W.new(cb);assert(loaded:load(saved))
check(loaded.project.tracks[1].automation.cutoff[2].v==4000,'project validation/save/load preserves curves')
local bad=copy(saved);bad.tracks[1].automation.cutoff[2].s=0;check(not format.project(bad),'duplicate curve times rejected')
bad=copy(saved);bad.tracks[1].automation.pedal[1].linear=true;check(not format.project(bad),'server rejects interpolated switches')
local plan=Sequence.compile(saved,nil,MIDI,'song',format,SoundSets.programPatch)
check(Sequence.validate(plan.bytes,plan.length,plan.loop,{[1]=true}) and #records(plan,141)==2,'native controls compile and validate on configured lane')
check(not Sequence.validate(plan.bytes,plan.length,plan.loop,{[2]=true}),'controls preserve lane ownership')
local malformed=plan.bytes:sub(1,9)..string.char(188)..plan.bytes:sub(11)
check(not Sequence.validate(malformed,plan.length,plan.loop),'unsupported control tags rejected')
w.project.composition.loopStart=2;w.project.composition.loopEnd=2;w.project.composition.loop=true
w.project.tracks[1].automation.cutoff={{s=0,v=100,linear=true},{s=32,v=18000,linear=false}}
local crop=Sequence.compile(w.project,nil,MIDI,'song',format,SoundSets.programPatch);local controls=records(crop,141)
check(controls[1].f==0 and math.abs(controls[1].v/65535*17980+20-9050)<1,'loop crop chases interpolated value')
check(controls[#controls].f==crop.length-1,'cropped ramp retains its slope to the final frame')
w.project.composition.loop=false;w.project.composition.loopStart=1;w.project.composition.loopEnd=8
check(w:setAutomation(1,'program',0,24,false) and w:setAutomation(1,'program',16,32,false),'program lane editable')
local manual=Sequence.compile(w.project,nil,MIDI,'song',format,SoundSets.programPatch)
check(#records(manual,128)==0,'program lane is opt-in')
check(w:followPrograms(1,true),'program follow enabled')
local follow=Sequence.compile(w.project,nil,MIDI,'song',format,SoundSets.programPatch)
check(#follow.bytes==#manual.bytes+2*#schema.order*11 and records(follow,128)[1].mode==129,'program changes expand to bounded sound-set snapshots')
local before=#w.undoStack;check(w:play('song'),'automation song schedules natively');w.awaiting=false;w:frame(.1)
check(w:beginAutomationWrite(),'write captures native playhead')
for n=1,12 do w.beat=n/4;assert(w:recordAutomation(1,'expression',n/12)) end
local previous=queued;w:frame(.3)
check(queued==previous,'active write avoids score-upload churn')
w:endAutomationWrite();check(#w.undoStack==before+1 and #w.project.tracks[1].automation.expression==12,'one undo action per captured curve take')
w:frame(.3);check(queued==previous+1,'end write queues one native bar replacement')
w:stop(true);check(w:undo() and not w.project.tracks[1].automation.expression,'write Undo restores prior lane')
check(w:play('song'),'program-bank replacement setup');w.awaiting=false;w:frame(.3)
local previous=queued;assert(w:cycleSoundSet(1));w:frame(.3)
check(queued==previous+1,'sound-set change rebuilds followed program snapshots at the next bar')
w:stop(true)
-- MIDI controller import is explicit, preserves patch automation, and follows the channel across setup tracks.
local song={events={{kind='on',channel=0,part='2:0',note=60,velocity=1,time=0,tick=0,track=2},
    {kind='control',channel=0,part='1:0',note=64,velocity=127,time=.1,tick=192,track=1},
    {kind='off',channel=0,part='2:0',note=60,time=.5,tick=960,track=2},
    {kind='control',channel=0,part='1:0',note=64,velocity=0,time=1,tick=1920,track=1}},
    controllers={{kind='bend',channel=0,note=12288,tick=480},{kind='program',channel=0,note=24,tick=0},
    {kind='control',channel=0,note=64,velocity=127,tick=192},{kind='control',channel=0,note=64,velocity=0,tick=1920},
    {kind='control',channel=0,note=11,velocity=64,tick=240}},
    duration=2,beats=4,division=960,tempos={{beat=0,time=0,tempo=500000}},programs={['2:0']=0},names={}}
local imported=W.new(cb);assert(imported:import(song,'midi/automation.mid'))
check(not imported.project.tracks[1].automation,'existing MIDI playback defaults unchanged')
assert(imported:setAutomation(1,'cutoff',0,4000,true));assert(imported:importControls(1))
local t=imported.project.tracks[1]
check(t.automation.cutoff[1].v==4000 and t.automation.pitchBend[1].v==1 and t.automation.expression[1].v==64/127,'explicit import retains patch curves and setup-track channel controls')
local raw=Sequence.compile(imported.project,song,MIDI,'song',format,SoundSets.programPatch)
check(records(raw,60)[1].v==24000,'explicit sustain keeps raw gates for editable pedal playback')
check(imported:convertPart(1) and imported:noteData(1)[1].l==4,'conversion after control import preserves raw PPQ gates')
local converted=Sequence.compile(imported.project,song,MIDI,'song',format,SoundSets.programPatch)
check(raw.bytes==converted.bytes,'native sustain conversion retains score identity')
local midi=Export.encode(imported.project,nil,MIDI,format);local parsed=MIDI.parse(midi)
local cc=MIDI.automation(parsed,MIDI.parts(parsed)[1]);check(cc.pitchBend[1].v==1 and cc.program[1].v==24 and cc.pedal[1].v==1,'SMF bend/program/CC round trip')
local budget=copy(saved);budget.tracks[1].automation={expression={}}
for n=1,1025 do budget.tracks[1].automation.expression[n]={s=n,v=.5,linear=false} end
check(not format.project(budget),'per-lane point budget enforced')
local muted=W.new(cb);assert(muted:newSong(120,4,4,4));assert(muted:addNote(1,48,0));assert(muted:preset(1,'b:sampled-electric-muted'))
check(muted:setAutomation(1,'stringMute',0,0,true) and muted:setAutomation(1,'stringMute',4,.8,false),'recorded note muting is an editable automation lane')
local muteDoc=assert(format.project(muted.project));check(muteDoc.tracks[1].params.stringMute==.7 and muteDoc.tracks[1].automation.stringMute[2].v==.8,'patch mute and its independent automation survive project validation')
local mutePlan=Sequence.compile(muteDoc,nil,MIDI,'song',format,SoundSets.programPatch)
local muteRecords=records(mutePlan,format.automationSpecs.stringMute.code)
check(#muteRecords>=2 and muteRecords[1].v==0 and muteRecords[#muteRecords].v==52428,'recorded mute curve reaches the existing native control code without changing the wire format')
check(muted:undo() and #muted.project.tracks[1].automation.stringMute==1 and muted:redo(),'recorded mute automation supports Undo/Redo')
print('PASS automation document/editor/history, native compilation/crop/programs, captured curves, MIDI controller import/export; '..checks..' assertions')
