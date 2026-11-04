-- Bounded visual interpretation of native attacks. Never changes audio/notes.
local M={}
local tunings={{40,45,50,55,59,64},{38,45,50,55,59,64},{36,43,48,53,57,62},
    {35,40,45,50,55,59,64},{28,33,38,43},{23,28,33,38,43}}
local function clamp(n,a,b) return math.max(a,math.min(b,n)) end
local function pulse(age,seconds) return clamp(1-age/seconds,0,1) end
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
        if n.age<.18 then latest=n;group[#group+1]=n end
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
        state.hands={};state.kick=nil;state.hat=nil;state.closed=nil
    end
    for _,n in ipairs(rows(snapshot)) do if n.age<=.25 then
        local key=n.track*128+n.note
        if state.seen[key]~=n.stamp then
            state.seen[key]=n.stamp
            local hit={note=n.note,at=now-n.age,stamp=n.stamp}
            if n.note==35 or n.note==36 then state.kick=hit
            elseif n.note==44 then state.hat=hit;state.closed=true
            elseif n.note==42 or n.note==46 then
                state.closed=n.note==42;state.hands.L=hit
            else
                local leftPiece=n.note==49 or n.note==55 or n.note==57
                local rightPiece=n.note==41 or n.note==43 or n.note==51 or n.note==53 or n.note==59
                local leftBusy=state.hands.L and hit.at-state.hands.L.at<.09
                local rightBusy=state.hands.R and hit.at-state.hands.R.at<.09
                local side=leftPiece and 'L' or rightPiece and 'R' or leftBusy and 'R' or rightBusy and 'L' or state.nextHand or 'R'
                -- Keep outer cymbals on their reachable side. If two written
                -- hits need one hand at once, the outer piece takes precedence;
                -- move a simultaneous central hit to the other free stick.
                local old=state.hands[side];local other=side=='L' and 'R' or 'L'
                if old and math.abs(old.at-hit.at)<.002 and old.note>=37 and old.note<=40
                    and (not state.hands[other] or hit.at-state.hands[other].at>.002) then state.hands[other]=old end
                state.hands[side]=hit;state.nextHand=side=='L' and 'R' or 'L'
            end
        end
    end end
    local out={hands={},feet={L=state.closed==nil and 0 or state.closed and .08 or -.10,R=0}}
    for _,side in ipairs{'L','R'} do local hit=state.hands[side]
        out.hands[side]={note=hit and hit.note or (side=='L' and 42 or 38),pulse=hit and pulse(now-hit.at,.14) or 0}
    end
    out.feet.R=state.kick and .16*pulse(now-state.kick.at,.15) or 0
    if state.hat then out.feet.L=out.feet.L+.10*pulse(now-state.hat.at,.15) end
    return out,state
end
return M
