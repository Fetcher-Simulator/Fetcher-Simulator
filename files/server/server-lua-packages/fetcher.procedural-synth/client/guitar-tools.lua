-- Explicit, offline note authoring. No audio/UI/network calls or random timing.
local M={profiles={
    {id='guitar',name='Guitar / standard E',opens={40,45,50,55,59,64}},
    {id='drop-d',name='Guitar / Drop D',opens={38,45,50,55,59,64}},
    {id='drop-c',name='Guitar / Drop C',opens={36,43,48,53,57,62}},
    {id='seven',name='Guitar / seven-string B',opens={35,40,45,50,55,59,64}},
    {id='bass',name='Bass / four-string E',opens={28,33,38,43}},
    {id='bass-five',name='Bass / five-string B',opens={23,28,33,38,43}},
}}
local function number(v,a,b) return type(v)=='number' and v==v and v>=a and v<=b end
function M.options(input)
    input=input or {};local out={profile=input.profile or 'guitar',spacing=input.spacing or 8,
        direction=input.direction or 0,frets=input.frets or 24,span=input.span or 5,scope=input.scope or 'all'}
    local profile;for _,p in ipairs(M.profiles) do if p.id==out.profile then profile=p end end
    if not profile or not number(out.spacing,0,25) or not number(out.direction,0,2) or out.direction%1~=0
        or not number(out.frets,0,24) or out.frets%1~=0 or not number(out.span,1,12) or out.span%1~=0
        or out.scope~='all' and out.scope~='selected' then return nil end
    return out,profile
end
function M.key(o) return table.concat({o.profile,o.spacing,o.direction,o.frets,o.span,o.scope},'|') end
function M.plan(notes,input,clock,selection,transpose)
    local o,profile=M.options(input);if not o then return nil,'Invalid fingering settings.' end
    transpose=transpose or 0
    if not number(transpose,-24,24) or transpose%1~=0 then return nil,'Set Tune to whole semitones before planning.' end
    if type(notes)~='table' or #notes==0 or #notes>4096 then return nil,'Choose a nonempty melodic source with at most 4,096 notes.' end
    local order,out,chosenIds={},{},{};local chosen=0
    for id,n in ipairs(notes) do
        if not number(n.n,0,127) or n.n%1~=0 or not number(n.s,0,1000000) or not number(n.l,clock.quantum,1000000)
            or not number(n.v,.001,1) then return nil,'Invalid source note.' end
        out[id]={n=n.n,s=n.s,l=n.l,v=n.v};order[#order+1]=id
        if o.scope=='all' or selection and selection[id] then chosenIds[id]=true;chosen=chosen+1 end
    end
    if chosen==0 then return nil,'Select notes in Piano first, or choose All notes.' end
    table.sort(order,function(a,b) return notes[a].s==notes[b].s and (notes[a].n==notes[b].n and a<b or notes[a].n<notes[b].n) or notes[a].s<notes[b].s end)
    local groups={}
    for _,id in ipairs(order) do
        local g=groups[#groups]
        if not g or math.abs(g.step-notes[id].s)>clock.quantum*.1 then g={step=notes[id].s,ids={}};groups[#groups+1]=g end
        g.ids[#g.ids+1]=id
    end
    local active,last={},{};local report={notes=out,rows={},groups=0,shifted=0,choked=0,selected=chosen,key=M.key(o),search=0}
    local strings=#profile.opens;local minimum=clock.quantum
    local function fail(g,message) return nil,string.format('At step %.4g: %s',g.step,message) end
    for groupIndex,g in ipairs(groups) do
        if #g.ids>strings then return fail(g,'more simultaneous notes than strings; split the part first.') end
        for s,a in pairs(active) do if out[a.id].s+out[a.id].l<=g.step+minimum*.1 then active[s]=nil end end
        local candidates,seen={},{ }
        for index,id in ipairs(g.ids) do
            local pitch=notes[id].n+transpose
            if seen[pitch] then return fail(g,'duplicate unison notes; remove duplicates before fingering.') end
            seen[pitch]=true;candidates[index]={}
            for s,open in ipairs(profile.opens) do local fret=pitch-open
                if fret>=0 and fret<=o.frets then
                    candidates[index][#candidates[index]+1]={string=s,fret=fret,
                        cost=fret*.1+(fret>0 and .5 or 0)+math.abs(fret-(last[s] or fret))*.08+(active[s] and 4 or 0)}
                end
            end
            if #candidates[index]==0 then return fail(g,'a pitch is outside this tuning/fret range; pitches are never changed or dropped.') end
            table.sort(candidates[index],function(a,b) return a.cost==b.cost and a.string<b.string or a.cost<b.cost end)
        end
        local best,bestCost,picks,used=nil,math.huge,{},{}
        local up=o.direction==1 or o.direction==2 and groupIndex%2==0
        local touched=false;for _,id in ipairs(g.ids) do touched=touched or chosenIds[id] or false end
        -- Alternate strokes count edited gestures only, independent of untouched context.
        if o.direction==2 then up=report.groups%2==1 end
        local function evaluate(cost)
            local lo,hi=math.huge,0
            for _,p in ipairs(picks) do if p.fret>0 then lo=math.min(lo,p.fret);hi=math.max(hi,p.fret) end end
            for s,a in pairs(active) do if not used[s] and a.fret>0 then lo=math.min(lo,a.fret);hi=math.max(hi,a.fret) end end
            if hi-lo>o.span then return end
            local edge=up and 0 or strings+1
            for i,id in ipairs(g.ids) do if chosenIds[id] then edge=up and math.max(edge,picks[i].string) or math.min(edge,picks[i].string) end end
            local starts={}
            for i,id in ipairs(g.ids) do
                local n=notes[id];local p=picks[i];local start=n.s
                if chosenIds[id] then
                    local delay=math.abs(p.string-edge)*o.spacing/1000
                    if delay>0 then
                        if clock.seconds(n.s+n.l)-clock.seconds(n.s)<=delay then return end
                        start=math.max(n.s,math.floor(clock.shift(n.s,delay,n.s+n.l)/minimum+.5)*minimum)
                    end
                end
                if n.s+n.l-start<minimum*.999 then return end
                if groups[groupIndex+1] and start>=groups[groupIndex+1].step-minimum*.1 then return end
                local prior=active[p.string]
                if prior and out[prior.id].s+out[prior.id].l>start+minimum*.1 then
                    if not chosenIds[prior.id] or start-out[prior.id].s<minimum*.999 then return end
                end
                -- Native voices retrigger by pitch; keep a held unison on its string.
                for s,a in pairs(active) do if out[a.id].n==n.n and s~=p.string and out[a.id].s+out[a.id].l>start then return end end
                starts[i]=start
            end
            bestCost=cost;best={}
            for i,p in ipairs(picks) do best[i]={string=p.string,fret=p.fret,start=starts[i]} end
        end
        local exhausted=false
        local function search(index,cost,lo,hi)
            report.search=report.search+1;if report.search>500000 then exhausted=true;return end
            if cost>=bestCost or exhausted then return end
            if index>#g.ids then evaluate(cost);return end
            for _,p in ipairs(candidates[index]) do if not used[p.string] then
                local low,high=lo,hi;if p.fret>0 then low=math.min(low,p.fret);high=math.max(high,p.fret) end
                if high-low<=o.span then
                    used[p.string]=true;picks[index]=p;search(index+1,cost+p.cost,low,high);used[p.string]=nil
                end
            end end
        end
        search(1,0,math.huge,0)
        if exhausted then return fail(g,'fingering search limit reached; use a smaller clip or lower the fret limit.') end
        if not best then return fail(g,'could not fit the next gesture without shortening protected notes, stretching too far or crossing the next attack. Reduce spacing, widen span or split the phrase.') end
        if touched then report.groups=report.groups+1 end
        for i,id in ipairs(g.ids) do
            local p=best[i];local prior=active[p.string]
            if prior and out[prior.id].s+out[prior.id].l>p.start+minimum*.1 then
                out[prior.id].l=p.start-out[prior.id].s;report.choked=report.choked+1
            end
            if chosenIds[id] then
                local n=out[id];local finish=n.s+n.l
                if p.start~=n.s then
                    if math.abs(p.start-n.s)>minimum*.1 then report.shifted=report.shifted+1 end
                    n.s=p.start;n.l=finish-p.start
                end
                report.rows[#report.rows+1]={id=id,n=n.n,step=n.s,string=strings-p.string+1,fret=p.fret,
                    delay=(clock.seconds(n.s)-clock.seconds(notes[id].s))*1000,up=up}
            end
            active[p.string]={id=id,fret=p.fret};last[p.string]=p.fret
        end
    end
    return report
end
return M
