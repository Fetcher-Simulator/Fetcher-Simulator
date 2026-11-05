local root='files/server/server-lua-packages/fetcher.procedural-synth/';local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=dofile(root..'shared/schema.lua');local factory=dofile(root..'shared/project.lua');local F=factory(schema)
local MIDI=dofile(root..'client/midi.lua');local Sequence=dofile(root..'shared/sequence.lua')
package.loaded[prefix..'shared.schema']=schema;package.loaded[prefix..'shared.project']=factory
local Sets=dofile(root..'client/soundsets.lua')
package.loaded[prefix..'client.midi']=MIDI;package.loaded[prefix..'client.soundsets']=Sets
package.loaded[prefix..'shared.sequence']=Sequence
local Exercises=dofile(root..'client/band-exercises.lua');package.loaded[prefix..'client.band-exercises']=Exercises
local W=dofile(root..'client/workstation.lua');local Export=dofile(root..'client/midi-export.lua')
local checks=0;local function check(ok,msg) checks=checks+1;assert(ok,msg) end
check(not Exercises.create('bad') and not Exercises.create('heavy','bad'),'unknown exercises and sound sets leave documents alone')
for _,entry in ipairs(Exercises.entries) do
    local p=assert(Exercises.create(entry.id));check(p.midi=='' and p.length==64 and p.timeline.beats==16 and p.composition.bars==4,'each exercise is four editable source-free bars')
    check(p.effects.reverbEnabled==0 and p.effects.delayEnabled==0,'sound check does not hide attacks behind authored return effects')
    local count,expressive,drums,bass=0,0,0,0
    for _,t in ipairs(p.tracks) do
        local notes=assert(F.decodeNotes(t.edited));check(#notes>0 and not t.followPrograms,'every part is explicit notes with stable sounds')
        for _,n in ipairs(notes) do count=count+1;check(n.s>=0 and n.s+n.l<=64.001 and n.n>=0 and n.n<=127,'all gates fit one cycle and stay in MIDI range');if n.e then expressive=expressive+1 end end
        if t.sourceRole=='bass' then bass=bass+1;check(({[33]=true,[34]=true,[36]=true,[37]=true})[t.sourceProgram],'bass has an explicit numeric GM technique') end
        if t.instrument=='drumkit' then drums=drums+1;check(t.note==36 or t.note==38 or t.note==42 or t.note==45 or t.note==49,'kit parts use deliberate GM notes') end
    end
    check(count>100 and expressive>40 and drums==5 and bass==(entry.id=='bass-attacks' and 3 or entry.id=='slap-pop' and 2 or 1),'exercise includes meaningful string performance and a complete split rhythm section')
    local plan=Sequence.compile(p,nil,MIDI,'song',F,Sets.programPatch)
    check(Sequence.validate(plan.bytes,plan.length,true),'native looping score validates with saved attack expression')
    local saved=assert(F.project(p));local loaded=Sequence.compile(saved,nil,MIDI,'song',F,Sets.programPatch)
    check(plan.bytes==loaded.bytes,'private source-free project reload keeps exact score bytes')
    local midi=MIDI.parse(Export.encode(p,nil,MIDI,F));check(#MIDI.parts(midi)==#p.tracks,'standard MIDI export retains all original parts')
    -- Every advertised sound set can reinterpret the same source identities.
    for _,set in ipairs(Sets.entries) do local alt=assert(Exercises.create(entry.id,set.id))
        check(F.project(alt) and Sequence.compile(alt,nil,MIDI,'song',F,Sets.programPatch),'sound-set comparisons compile without external MIDI')
    end
end
do
    local p=assert(Exercises.create('harmonic-accents'));local voices={};local phrases={}
    for _,t in ipairs(p.tracks) do if t.sourceRole=='guitar' then voices[#voices+1]=t end end
    check(#voices==3,'harmonic check retains three independent recorded guitar choices')
    for variant,t in ipairs(voices) do
        phrases[variant]={}
        check(t.presetId==({'b:capture-heavy-lead','b:guitar-natural-harmonics','b:guitar-pinch-harmonics'})[variant],'comparison pins explicit recorded harmonic sounds')
        for _,n in ipairs(assert(F.decodeNotes(t.edited))) do
            local bar=math.floor(n.s/16)
            check(bar==variant-1 or bar==3,'harmonics occupy their comparison bar and the final accent exchange')
            check(n.n>=(variant==2 and 52 or variant==3 and 60 or 35) and n.n<=(variant==2 and 83 or variant==3 and 96 or 86),'demo stays in the actual recorded bank register')
            if bar<3 then phrases[variant][#phrases[variant]+1]=n.n end
        end
    end
    for n,note in ipairs(phrases[1]) do check(note==phrases[2][n] and note==phrases[3][n],'all three comparison passages retain the same sounding-pitch phrase') end
    for _,patch in ipairs(schema.builtins) do if patch.id=='b:guitar-natural-harmonics' or patch.id=='b:guitar-pinch-harmonics' then
        check(not patch.midiProgram and patch.bank=='Band / Recorded Harmonics','new harmonic sounds are opt-in and do not change imported GM assignments')
    end end
end
do
    local p=assert(Exercises.create('bass-attacks'));local parts={}
    for _,t in ipairs(p.tracks) do if t.sourceRole=='bass' then parts[#parts+1]=t end end
    check(#parts==3,'comparison has three independently selectable recorded bass sounds')
    for variant,t in ipairs(parts) do
        check(t.presetId==({'b:sampled-bass-finger','b:sampled-bass-short','b:sampled-bass-ghost'})[variant],'comparison pins the actual recording family')
        local phrase={}
        for _,n in ipairs(assert(F.decodeNotes(t.edited))) do
            local bar=math.floor(n.s/16)
            check(not n.e and (bar==variant-1 or bar==3),'bass recordings remain exposed and only play in their comparison slots')
            if bar<3 then phrase[#phrase+1]={n=n.n,s=n.s%16,l=n.l,v=n.v} end
        end
        check(#phrase==8,'each recording gets the complete same eight-note phrase')
        parts[variant]=phrase
    end
    for n,item in ipairs(parts[1]) do for variant=2,3 do
        local other=parts[variant][n]
        check(item.n==other.n and item.s==other.s and item.l==other.l and item.v==other.v,'comparison retains identical pitch, timing, velocity and gates')
    end end
end
local messages,changed,released,detached={};local w=W.new{configure=function() end,panic=function() end,
    message=function(s) messages[#messages+1]=s end,documentChanged=function() changed=true end}
assert(w:newSong(120,4,4,8));assert(w:addNote(1,61,0));w.project.name='Keep my music';w.project.stageLayout='seated';w.project.leaderPlays=false;w.project.autoBand=true
w.project.band[1].name='My bassist';w:touch();local prior=assert(F.project(w.project));local history=#w.undoStack
w.library={detach=function() detached=true end};w.recorder={releaseInput=function() released=true end}
w.rollStart=200;w.arrangeBar=25;w.regionIndex=9;w.regionSelection={9}
check(w:loadExercise('thrash') and changed and released and detached,'opening a sound check clears old input and detaches from the named save')
check(w.project.band[1].name=='My bassist' and w.project.stageLayout=='seated' and w.project.leaderPlays==false and w.project.autoBand,'sound checks retain physical band and leader preferences')
check(w.rollStart==0 and w.arrangeBar==0 and not w.regionIndex and #w.regionSelection==0,'new project clears stale note/arranger scroll and placement selection')
check(w.page=='mixer' and w.dirty and not w:needsMidi() and #w.undoStack==history+1,'new exercise is an unsaved undoable project, ready for native playback')
assert(w:undo());check(w.project.name==prior.name and w:noteData(1)[1].n==61,'Undo restores the previous musical document')
assert(w:redo());check(w.project.name=='Sound check - Tight thrash','Redo restores the exercise')
w.failedTake={};local project=w.project;check(not w:loadExercise('heavy') and w.project==project,'pending take recovery cannot be replaced by an exercise');w.failedTake=nil
check(not w:loadExercise('unknown') and w.project==project,'invalid exercise does not detach or replace a document')
print('PASS original band exercises, per-note performance, full-kit routing, all sound sets, source-free persistence, MIDI export, safe project replacement, band preferences and Undo/Redo; '..checks..' assertions')
