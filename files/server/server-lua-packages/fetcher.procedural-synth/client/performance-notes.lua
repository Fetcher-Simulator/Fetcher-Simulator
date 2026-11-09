-- Bounded visual interpretation of native attacks. Never changes audio/notes.
local M={}
local tunings={{40,45,50,55,59,64},{38,45,50,55,59,64},{36,43,48,53,57,62},
    {35,40,45,50,55,59,64},{28,33,38,43},{23,28,33,38,43}}
local function clamp(n,a,b) return math.max(a,math.min(b,n)) end
local function pulse(age,seconds) return clamp(1-age/seconds,0,1) end
-- Middle-register B-flat trumpet fingering, indexed by written pitch class.
local trumpetMasks={0,7,5,6,3,1,2,0,6,3,1,2}
function M.trumpet(snapshot,fallback,dt,pressure)
    local latest,pitch=math.huge,fallback
    for _,note in ipairs(snapshot or {}) do
        if note.held then
            local sounding=note.note+(note.transpose or 0)
            if note.age<latest or note.age==latest and sounding>pitch then latest,pitch=note.age,sounding end
        end
    end
    local mask=pitch and trumpetMasks[(math.floor(pitch)+2)%12+1] or 0
    pressure=pressure or {0,0,0}
    local mix=math.min(1,dt/.045)
    for valve=1,3 do
        local down=math.floor(mask/2^(valve-1))%2
        pressure[valve]=pressure[valve]+(down-pressure[valve])*mix
    end
    return pressure
end
function M.bow(snapshot,held,lastStrike,now,dt,state)
    state=state or {travel=0,direction=1}
    local attack
    if held then
        for _,note in ipairs(snapshot or {}) do
            if note.held then
                local at=now-note.age
                attack=math.max(attack or at,at)
            end
        end
        attack=attack or lastStrike
        -- A single chord uses one stroke. Old notes left ringing under a new
        -- attack cannot reverse the bow again as their neighbors release.
        if not state.attack or attack>state.attack+.045 then
            if state.attack and math.abs(state.travel-state.start)>.2 then state.direction=-state.direction end
            state.start=state.travel
            state.attack=attack
        end
        local endpoint=5*state.direction
        state.travel=state.travel+(endpoint-state.travel)*(1-math.exp(-math.min(dt,.2)*1.35))
        state.lastHeld=now
    elseif not state.lastHeld or now-state.lastHeld>.16 then
        state.travel=state.travel*math.exp(-math.min(dt,.2)*5)
    end
    return state.travel,state
end

local function cymbal(note)
    if note==42 or note==46 then return 'hat' end
    if note==49 or note==55 or note==57 then return 'crash' end
    if note==51 or note==53 or note==59 then return 'ride' end
end

local function strikePulse(age)
    return clamp(age/.045,0,1)*pulse(math.max(0,age-.095),.105)
end
local function rows(snapshot)
    local out={};for _,n in ipairs(snapshot or {}) do out[#out+1]=n end
    table.sort(out,function(a,b)
        if a.age~=b.age then return a.age>b.age end
        if a.track~=b.track then return a.track<b.track end
        if a.note~=b.note then return a.note<b.note end
        return a.stringSelect<b.stringSelect
    end)
    return out
end
function M.strings(snapshot,kind,state)
    state=state or {};local active,latest={},nil;local heldLayout;local group={};local explicit,automatic={},{}
    for _,n in ipairs(rows(snapshot)) do
        if n.age<.18 and n.picked~=false then latest=n;group[#group+1]=n end
        if n.held then
            heldLayout=n.layout
            if n.stringSelect>0 then explicit[#explicit+1]=n else automatic[#automatic+1]=n end
        end
    end
    local layout=latest and latest.layout or heldLayout or state.layout or 0
    if layout==0 then layout=kind=='bass' and 5 or 1 end
    state.layout=layout;local tuning=tunings[layout] or tunings[1]
    local function assign(n,fixed)
        local own=tunings[n.layout] or tuning;local pitch=n.note+(n.transpose or 0)
        local chosen,cost
        for string,open in ipairs(own) do
            local fret=pitch-open
            if fret>=0 and fret<=24 and (not fixed or string==n.stringSelect) then
                local c=fret+math.abs(fret-(state.position or 5))*.25+(active[string] and 100 or 0)
                if not cost or c<cost then chosen,cost=string,c end
            end
        end
        if chosen then
            -- Explicit saved assignments take precedence over visual Auto guesses.
            if not active[chosen] or fixed or not active[chosen].explicit then
                active[chosen]={string=chosen,fret=pitch-own[chosen],pitch=pitch,explicit=fixed,age=n.age}
            end
        end
    end
    for _,n in ipairs(explicit) do assign(n,true) end
    for _,n in ipairs(automatic) do assign(n,false) end
    local base=math.huge;local frets={}
    for _,n in pairs(active) do if n.fret>0 then base=math.min(base,n.fret) end end
    if base<math.huge then state.position=base end
    state.position=state.position or 5
    for string,n in pairs(active) do if n.fret>0 then
        local finger=clamp(n.fret-state.position+1,1,4)
        frets[finger]=frets[finger] or {};frets[finger][#frets[finger]+1]=string
    end end
    for _,strings in pairs(frets) do table.sort(strings) end
    local pick=0;local direction=0;local selected=0;local count=0
    if latest then
        pick=pulse(latest.age,.14);direction=latest.direction;selected=latest.stringSelect
        for _,n in ipairs(group) do if n.age-latest.age<.035 then count=count+1 end end
        local id=latest.track..':'..latest.note..':'..latest.stamp
        if state.lastPick~=id then state.lastPick=id;state.finger=state.finger==1 and 2 or 1 end
    end
    return {position=state.position,frets=frets,strings=active,stringCount=#tuning,
        pulse=pick,direction=direction,pickedString=selected,strum=count>1,pickFinger=state.finger or 1},state
end
function M.drums(snapshot,now,state)
    state=state or {seen={},hands={}};state.seen=state.seen or {};state.hands=state.hands or {}
    if snapshot and snapshot.playing==false and #snapshot==0 then
        state.hands={};state.seen={};state.nextHand=nil;state.kick=nil;state.hat=nil;state.closed=nil;state.cymbals=nil
    end
    local hits={}
    for _,n in ipairs(rows(snapshot)) do if n.age<=.25 then
        local key=n.track*128+n.note
        if state.seen[key]~=n.stamp then
            state.seen[key]=n.stamp
            local hit={note=n.note,at=now-n.age,stamp=n.stamp}
            if n.note==35 or n.note==36 then state.kick=hit
            elseif n.note==44 then state.hat=hit;state.closed=true
            else
                hit.hat=n.note==42 or n.note==46
                if hit.hat then state.closed=n.note==42 end
                local piece=cymbal(n.note)
                if piece then
                    state.cymbals=state.cymbals or {}
                    state.cymbals[piece]=hit
                end
                hit.snare=n.note>=37 and n.note<=40
                hit.side=hit.hat and 'R' or (n.note==49 or n.note==55 or n.note==57) and 'L'
                    or (n.note==41 or n.note==43 or n.note==51 or n.note==53 or n.note==59) and 'R'
                hit.priority=hit.hat and 3 or hit.side and 2 or hit.snare and 1 or 0
                hits[#hits+1]=hit
            end
        end
    end end
    -- Resolve each chord before the next one. Reserve the right stick for hats
    -- and the reachable stick for outer cymbals, then fit snare/toms around them.
    -- Track order must not turn a normal hat+snare backbeat into one stick hit.
    local first=1
    while first<=#hits do
        local last=first
        while last<#hits and math.abs(hits[last+1].at-hits[first].at)<.002 do last=last+1 end
        local chord={};for i=first,last do chord[#chord+1]=hits[i] end
        table.sort(chord,function(a,b) if a.priority~=b.priority then return a.priority>b.priority end;return a.note<b.note end)
        for _,hit in ipairs(chord) do
            local function busy(side,window)
                local old=state.hands[side];return old and math.abs(hit.at-old.at)<window
            end
            local side=hit.side or (hit.snare and 'L' or state.nextHand or 'R')
            local other=side=='L' and 'R' or 'L'
            -- Fast snare rolls and tom fills can use either hand; ordinary
            -- backbeats remain left-handed even when the right stick is idle.
            if not hit.side and busy(side,.09) and not busy(other,.09) then side,other=other,side end
            if busy(side,.002) then
                local old=state.hands[side]
                if not busy(other,.002) then
                    if hit.priority>(old.priority or 0) then state.hands[other]=old
                    else side=other end
                else side=nil end -- More than two simultaneous notes remain audio-only.
            end
            if side then
                local old=state.hands[side]
                hit.previous=old and old.note or (side=='L' and 38 or 42)
                state.hands[side]=hit
                state.nextHand=side=='L' and 'R' or 'L'
            end
        end
        first=last+1
    end
    local out={hands={},feet={L=state.closed==nil and 0 or state.closed and .08 or -.10,R=0}}
    for _,side in ipairs{'L','R'} do local hit=state.hands[side]
        local age=hit and now-hit.at or math.huge
        out.hands[side]={note=hit and hit.note or (side=='L' and 38 or 42),
            pulse=pulse(age,.14),impact=strikePulse(age),reach=pulse(math.max(0,age-.15),.22),
            previous=hit and hit.previous,approach=clamp(age/.045,0,1)}
    end
    out.feet.R=state.kick and .16*pulse(now-state.kick.at,.15) or 0
    if state.hat then out.feet.L=out.feet.L+.10*pulse(now-state.hat.at,.15) end
    out.kick=state.kick and pulse(now-state.kick.at,.15) or 0
    out.hatClosure=state.closed and 1 or 0
    out.cymbals={}
    for name,hit in pairs(state.cymbals or {}) do
        local age=now-hit.at-.055
        if age>=0 and age<.55 then out.cymbals[name]=math.sin(age*47)*math.exp(-age*10) end
    end
    return out,state
end
return M
