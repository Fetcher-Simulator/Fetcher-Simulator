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
}
function M.find(id) for _,entry in ipairs(M.entries) do if entry.id==id then return entry end end end
function M.create(id,setId)
    local entry=M.find(id);if not entry or setId and not Sets.find(setId) then return nil end
    local bassComparison=id=='bass-attacks';if bassComparison then id='funk' end
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
        t.params,t.presetId=Sets.assignment(p.soundSet,role,true,t.note,t.instrument,program,name)
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
        local start=bar*16;local root=bassComparison and 40 or ({40,43,45,42})[bar+1]
        if id=='heavy' or id=='thrash' then
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
    return assert(F.project(p))
end
return M
