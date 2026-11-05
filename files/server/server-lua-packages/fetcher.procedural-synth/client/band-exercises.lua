-- Original, source-free band exercises. No imported MIDI or third-party melody.
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=require(prefix..'shared.schema');local F=require(prefix..'shared.project')(schema)
local Sets=require(prefix..'client.soundsets');local M={}
M.entries={
    {id='heavy',name='Heavy rhythm',bpm=112,set='recorded-rock',description='Open power chords, palm-muted eighths, bass and a rock backbeat.'},
    {id='thrash',name='Tight thrash',bpm=184,set='recorded-thrash',description='Fast low-register chugs, open accents, double-kick patterns and tom fills.'},
    {id='funk',name='Funk groove',bpm=104,set='recorded-funk',description='Short chord stabs, dead strokes, syncopated bass and velocity-varied hats.'},
    {id='clean',name='Clean chords / lead',bpm=92,set='recorded-clean',description='Six-string strums and an overlapping lead phrase over restrained bass and drums.'},
    {id='acoustic',name='Acoustic / classical',bpm=96,set='recorded-clean',description='Steel-string fingerpicking, nylon melody and a quiet acoustic rhythm section.'},
    {id='bass-attacks',name='Recorded bass attacks',bpm=104,set='recorded-funk',description='Bars 1/2/3: regular, captured short, captured ghost bass. Bar 4 alternates the three; guitar and kit stay constant.'},
    {id='bridge-thrash',name='Bridge thrash / C',bpm=184,set='captured-thrash',description='The reviewed C tone: bridge DI, calibrated amp input and an editable channel low-mid cut. Same phrase and backing as Tight thrash.'},
    {id='string-unisons',name='Twin-string sustain',bpm=92,set='captured-clean',description='Two strings play the same pitch with different gate lengths. The shorter note releases while the other continues.'},
    {id='slap-pop',name='Slap / pop groove',bpm=104,set='band-funk',description='Real thumb slaps and octave-up pops, followed by short/dead strokes. Two editable bass lanes; one rhythm section.'},
    {id='doom-band',name='Doom band',bpm=112,set='band-doom',description='Neck-pickup fuzz, open power chords and a broad acoustic backbeat.'},
    {id='modern-band',name='Modern metal band',bpm=184,set='band-modern',description='Bridge rhythm, a tighter low end and an attack-forward kit. Same notes as Tight thrash.'},
    {id='alternative-band',name='Alternative metal band',bpm=112,set='band-alternative',description='Midrange crunch, picked bass and kit. Same notes as Heavy rhythm.'},
    {id='clean-band',name='Clean electric band',bpm=92,set='band-clean',description='Warm clean strums and melody with a balanced restrained rhythm section.'},
    {id='blues-band',name='Blues / jazz band',bpm=92,set='band-blues',description='Edge-of-breakup neck guitar and a darker kit. Compare with Clean electric band.'},
    {id='acoustic-band',name='Natural acoustic band',bpm=96,set='band-acoustic',description='Recorded steel fingerpicking and nylon melody with natural gated releases.'},
    {id='scooped-thrash',name='Scooped thrash band',bpm=184,set='band-thrash-ref',description='Staged bridge amp, interstage bass filtering and post-cab 750-Hz scoop. Same notes as Tight thrash.'},
    {id='industrial-band',name='Industrial low-B band',bpm=112,set='band-industrial',description='Original low-B chug pattern, parallel fold/noise texture, driven bass and full kit; not a transcription of a soundtrack.'},
    {id='harmonic-accents',name='Recorded harmonic accents',bpm=112,set='band-capture-metal',description='Bars 1/2/3: picked lead, clean natural harmonics, captured high-gain pinch harmonics. Bar 4 alternates accents; bass and kit stay constant.'},
    {id='connected-endings',name='Connected strings / natural endings',bpm=112,set='performance-thrash',description='Bars 1/2/3: picked notes, same-string recorded hammer-ons/modeled pull-offs, picked slides. Bar 4 exposes note endings. Guitar and finger bass use real release samples.'},
}
function M.find(id) for _,entry in ipairs(M.entries) do if entry.id==id then return entry end end end
function M.create(id,setId)
    local entry=M.find(id);if not entry or setId and not Sets.find(setId) then return nil end
    local bassComparison=id=='bass-attacks';local stringUnisons=id=='string-unisons';local slapComparison=id=='slap-pop';local harmonicComparison=id=='harmonic-accents'
    local articulationComparison=id=='connected-endings'
    if harmonicComparison or articulationComparison then id='heavy' end
    if slapComparison then id='funk' end
    id=({['doom-band']='heavy',['modern-band']='thrash',['alternative-band']='heavy',
        ['clean-band']='clean',['blues-band']='clean',['acoustic-band']='acoustic',
        ['scooped-thrash']='thrash',['industrial-band']='industrial'})[id] or id
    if bassComparison then id='funk' elseif stringUnisons then id='clean' elseif id=='bridge-thrash' then id='thrash' end
    local p=F.new();p.name='Sound check - '..entry.name;p.bpm=entry.bpm;p.length=64;p.soundSet=setId or entry.set
    p.timeline={beats=16,tempos={{beat=0,tempo=math.floor(60000000/entry.bpm+.5)}}}
    p.composition={bars=4,numerator=4,denominator=4,loopStart=1,loopEnd=4,loop=true}
    -- A dry common monitoring reference. Individual saved sounds and their amps stay intact.
    p.effects.delayEnabled=0;p.effects.reverbEnabled=0;p.master=.7
    local notes={}
    local function track(name,role,program,note,level)
        local t=F.newTrack('midi',name,64);local index=#p.tracks+1;p.tracks[index]=t;notes[index]={}
        t.sourceRole=role;t.sourceProgram=program;t.note=note or 60;t.level=level;t.followPrograms=false
        t.instrument=F.instrumentFor(role,program)
        Sets.apply(t,Sets.assignment(p.soundSet,role,true,t.note,t.instrument,program,name))
        return index
    end
    local guitar=track(entry.name..' guitar','guitar',id=='acoustic' and 25 or (id=='funk' and 28 or id=='clean' and 27 or 30),40,.6)
    local lead
    if id=='clean' or id=='acoustic' then lead=track(id=='acoustic' and 'Nylon melody' or 'Electric lead','guitar',id=='acoustic' and 24 or 31,64,.4) end
    local bass=track('Bass','bass',(id=='funk' or id=='acoustic') and 33 or 34,28,.72)
    -- Keep a complete kit split into independent MIDI parts, grouped by Auto NPC band.
    local kick=track('Kick','kick',nil,36,.85);local snare=track('Snare','snare',nil,38,.82)
    local hat=track('Hi-hat','hat',nil,42,.5);local cymbal=track('Cymbals','crash',nil,49,.45);local tom=track('Tom fill','tom',nil,45,.65)
    local function add(index,n,s,l,v,artic,direction)
        if s>=64 then return end
        local item={n=n,s=s,l=math.max(.05,math.min(l,64-s)),v=v}
        if artic then item.e={stringArticulation=artic,pickDirection=direction or 0} end
        notes[index][#notes[index]+1]=item
    end
    local chords={{40,47,52,55,59,64},{43,47,50,55,59,67},{45,52,57,60,64},{50,57,62,66}}
    for bar=0,3 do
        local start=bar*16;local root=bassComparison and 40 or (id=='industrial' and {35,35,38,36} or {40,43,45,42})[bar+1]
        if id=='heavy' or id=='thrash' or id=='industrial' then
            for step=0,15,id=='thrash' and 1 or 2 do
                local open=step==0 or step==12;local gate=open and 2.6 or (id=='thrash' and .58 or .85)
                local velocity=step%4==0 and .92 or step%2==0 and .78 or .66
                for _,interval in ipairs(open and {0,7,12} or {0,7}) do add(guitar,root+interval,start+step,gate,velocity,open and 1 or 2,step%2) end
                add(bass,root-12,start+step,open and 2.6 or gate+.1,.82,1)
            end
        elseif id=='funk' then
            for n,step in ipairs{0,3,6,8,11,14} do
                local dead=step==3 or step==11
                for _,pitch in ipairs{root+12,root+19,root+22} do add(guitar,pitch,start+step,.55,dead and .48 or .78,dead and 3 or 2,n%2) end
            end
            for n,step in ipairs{0,2.5,5,7,8,10.5,12,14.5} do add(bass,root-12+({0,0,7,12,0,7,10,7})[n],start+step,step==7 and .45 or 1,.7+(n%3)*.07,1) end
        else
            local chord=chords[bar+1]
            if id=='acoustic' then
                for n=0,15 do add(guitar,chord[({1,3,5,4,2,4,6,4})[n%8+1]] or chord[#chord],start+n,2.2,.58+(n%4==0 and .2 or 0),1,n%2) end
            else
                for stroke,step in ipairs{0,6,10} do
                    local up=stroke==2
                    for n=1,#chord do local at=up and #chord-n+1 or n
                        add(guitar,chord[at],start+step+(n-1)*.07,4.8-(n-1)*.07,up and .58 or .76,1,up and 1 or 0)
                    end
                end
            end
            for n,step in ipairs{0,6,8,14} do add(bass,root-12+(n%2==0 and 7 or 0),start+step,3.8,.66,1) end
            for n,step in ipairs{1,4,7,10,12} do add(lead,({64,67,66,62})[bar+1]+({0,2,5,2,0})[n],start+step,n==5 and 3.5 or 3.2,.65+(n%2)*.1,1,n%2) end
        end
        local soft=id=='acoustic' or id=='clean';local base=soft and .52 or .88
        for _,step in ipairs(id=='thrash' and {0,2,6,8,9,10,14,15} or id=='funk' and {0,3,6,8,10.5} or {0,6,8,10}) do add(kick,36,start+step,.35,base) end
        for _,step in ipairs{4,12} do add(snare,38,start+step,.4,base-.05) end
        if id=='funk' then for _,step in ipairs{7,15} do add(snare,38,start+step,.25,.28) end end
        for step=0,14,2 do add(hat,42,start+step,.4,(soft and .35 or .58)+(step%4==0 and .13 or 0)) end
        if bar==0 or bar==2 then add(cymbal,49,start,.5,soft and .35 or .72) end
        if bar==3 then for n,step in ipairs{13,14,15} do add(tom,45,start+step,.5,.55+n*.09) end end
    end
    if stringUnisons then
        notes[guitar]={};p.tracks[guitar].params.stringLayout=1;p.tracks[guitar].params.stringLegato=2
        for bar=0,3 do for _,step in ipairs{0,8} do
            local pitch=({64,66,67,69})[bar+1]
            for _,string in ipairs{4,5} do
                add(guitar,pitch,bar*16+step,string==4 and 2 or 6,.75,1)
                notes[guitar][#notes[guitar]].e.stringSelect=string
            end
        end end
    end
    for index,t in ipairs(p.tracks) do
        table.sort(notes[index],function(a,b) return a.s==b.s and a.n<b.n or a.s<b.s end)
        t.edited=assert(F.encodeNotes(notes[index],960))
    end
    if bassComparison then
        -- Separate, explicit recorded sounds; no claim that the engine switches
        -- samples from a Palm/Dead expression marker. Ordinary sound sets stay
        -- unchanged. The other instruments may still use the selected set.
        local source=assert(F.project(p)).tracks[bass]
        for variant,preset in ipairs{'b:sampled-bass-finger','b:sampled-bass-short','b:sampled-bass-ghost'} do
            local t=variant==1 and p.tracks[bass] or F.newTrack('midi','Bass',64)
            local patch;for _,candidate in ipairs(schema.builtins) do if candidate.id==preset then patch=candidate end end
            t.name=({'Regular finger bass','Captured short bass','Captured ghost bass'})[variant]
            t.sourceRole='bass';t.sourceProgram=33;t.instrument=source.instrument;t.note=source.note;t.level=source.level
            t.params=assert(schema.state(assert(patch).state));t.presetId=preset;t.followPrograms=false
            local selected={}
            for n,item in ipairs(notes[bass]) do
                local bar=math.floor(item.s/16)
                if bar==variant-1 or bar==3 and (n-1)%3==variant-1 then
                    -- Keep the recordings exposed: no added pick/resonance or
                    -- damping model, and the same velocity/gate for each phrase.
                    selected[#selected+1]={n=item.n,s=item.s,l=item.l,v=item.v}
                end
            end
            t.edited=assert(F.encodeNotes(selected,960))
            if variant>1 then p.tracks[#p.tracks+1]=t end
        end
    end
    if slapComparison then
        local source=p.tracks[bass];local original=notes[bass]
        for variant,preset in ipairs{'b:sampled-bass-slap','b:sampled-bass-pop'} do
            local t=variant==1 and source or F.newTrack('midi','Popped bass',64)
            local patch;for _,candidate in ipairs(schema.builtins) do if candidate.id==preset then patch=candidate end end
            t.name=variant==1 and 'Thumb slaps' or 'Octave pops';t.sourceRole='bass';t.sourceProgram=35+variant
            t.instrument='bass';t.note=40;t.level=.72;t.followPrograms=false
            Sets.apply(t,assert(schema.state(patch.state)),preset)
            local selected={}
            for n,item in ipairs(original) do if n%2==(variant==1 and 1 or 0) then
                local bar=math.floor(item.s/16)
                selected[#selected+1]={n=item.n+(variant==2 and 12 or 0),s=item.s,l=item.l,v=item.v,
                    e={stringArticulation=bar<2 and 1 or n%4<2 and 2 or 3}}
            end end
            t.edited=assert(F.encodeNotes(selected,960));if variant>1 then p.tracks[#p.tracks+1]=t end
        end
    end
    if harmonicComparison then
        for variant,preset in ipairs{'b:capture-heavy-lead','b:guitar-natural-harmonics','b:guitar-pinch-harmonics'} do
            local t=variant==1 and p.tracks[guitar] or F.newTrack('midi','Harmonics',64)
            local patch;for _,candidate in ipairs(schema.builtins) do if candidate.id==preset then patch=candidate end end
            t.name=({'Picked lead reference','Natural harmonics (sounding pitch)','Pinch harmonics (sounding pitch)'})[variant]
            t.sourceRole='guitar';t.sourceProgram=30;t.instrument='guitar';t.note=72;t.level=.6;t.followPrograms=false
            Sets.apply(t,assert(schema.state(patch.state)),preset)
            local selected={}
            for bar=0,3 do for step=0,14,2 do
                if bar==variant-1 or bar==3 and (step/2)%3==variant-1 then
                    selected[#selected+1]={n=72+({0,2,4,7,4,2,0,7})[step/2+1],s=bar*16+step,l=1.65,v=.8,
                        e={stringArticulation=1}}
                end
            end end
            t.edited=assert(F.encodeNotes(selected,960));if variant>1 then p.tracks[#p.tracks+1]=t end
        end
    end
    if articulationComparison then
        for _,t in ipairs(p.tracks) do if t.sourceRole~='guitar' then t.level=t.level*.65 end end
        local source=p.tracks[bass];source.sourceProgram=33
        local bassPatch;for _,candidate in ipairs(schema.builtins) do if candidate.id=='b:perf-finger-bass' then bassPatch=candidate end end
        Sets.apply(source,assert(schema.state(bassPatch.state)),bassPatch.id)
        for variant=1,3 do
            local t=variant==1 and p.tracks[guitar] or F.newTrack('midi','Connected guitar',64)
            local patch;for _,candidate in ipairs(schema.builtins) do if candidate.id=='b:perf-connected-guitar' then patch=candidate end end
            t.name=({'Picked reference','Recorded hammer / modeled pull','Picked slides'})[variant]
            t.sourceRole='guitar';t.sourceProgram=27;t.instrument='guitar';t.note=52;t.level=.6;t.followPrograms=false
            Sets.apply(t,assert(schema.state(patch.state)),patch.id)
            t.params.stringLegato=variant-1;t.params.stringLayout=1;t.params.stringSelect=1;t.params.stringSlideTime=variant==3 and .08 or .008
            local selected={}
            for n,note in ipairs{52,54,55,59,55,54,52,54} do
                local start=(variant-1)*16+(n-1)*2
                selected[#selected+1]={n=note,s=start,l=math.min(variant==1 and 1.8 or 2.6,variant*16-start),v=.75+(n%2)*.1,e={stringArticulation=1,stringSelect=1}}
            end
            selected[#selected+1]={n=51+variant,s=48+(variant-1)*6,l=variant==3 and 3 or 4,v=.8,e={stringArticulation=1,stringSelect=1}}
            t.edited=assert(F.encodeNotes(selected,960));if variant>1 then p.tracks[#p.tracks+1]=t end
        end
    end
    return assert(F.project(p))
end
return M
