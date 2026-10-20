-- Bounded, portable score format: LE u32 onset, LE u32 gate, lane, pitch, velocity.
-- The server validates the same bytes that native clients consume.
local M={rate=48000,maxNotes=60000,maxFrames=86400000,recordSize=11,chunkSize=2816}
local function integer(n,a,b) return type(n)=='number' and n==n and n>=a and n<=b and n%1==0 end
local function u32(bytes,at)
    local a,b,c,d=bytes:byte(at,at+3);return a+b*256+c*65536+d*16777216
end
local function pack(n)
    local a=n%256;n=math.floor(n/256);local b=n%256;n=math.floor(n/256)
    return string.char(a,b,n%256,math.floor(n/256))
end
function M.validate(bytes,length,loop,channels)
    if type(bytes)~='string' or #bytes==0 or #bytes%11~=0 or #bytes>11*M.maxNotes
        or not integer(length,1,M.maxFrames) or type(loop)~='boolean' then return nil,'Invalid score size or duration.' end
    local tokens,previous,base,intervals=80,0,0,{}
    for at=1,#bytes,11 do
        local frame,duration=u32(bytes,at),u32(bytes,at+4)
        local lane,note,velocity=bytes:byte(at+8,at+10)
        if frame<previous or frame>=length or duration<1 or duration>M.maxFrames or lane<1 or lane>16 or note>127 or velocity<1 or velocity>127
            or channels and not channels[lane] or not loop and frame+duration>length then return nil,'Invalid score note.' end
        tokens=math.min(80,tokens+(frame-previous)*40/M.rate)
        if tokens<1-1e-9 then return nil,'Score exceeds the shared 40 notes/sec, burst-80 limit.' end
        tokens=tokens-1;previous=frame
        local gate=duration
        if loop then
            base=base+math.floor(duration/length);gate=duration%length
            if gate>0 and frame+gate>=length then base=base+1;intervals[#intervals+1]={frame+gate-length,-1}
            elseif gate>0 then intervals[#intervals+1]={frame+gate,-1} end
        else intervals[#intervals+1]={frame+gate,-1} end
        if gate>0 then intervals[#intervals+1]={frame,1} end
    end
    if loop and #bytes/11*M.rate>40*length then return nil,'Loop exceeds 40 notes/sec.' end
    if loop then
        for at=1,#bytes,11 do
            local frame=u32(bytes,at)+length;tokens=math.min(80,tokens+(frame-previous)*40/M.rate)
            if tokens<1-1e-9 then return nil,'Loop boundary exceeds burst-80 limit.' end
            tokens=tokens-1;previous=frame
        end
    end
    table.sort(intervals,function(a,b) return a[1]==b[1] and a[2]<b[2] or a[1]<b[1] end)
    if base>4096 then return nil,'Too many overlapping gates.' end
    for _,event in ipairs(intervals) do base=base+event[2];if base>4096 then return nil,'Too many overlapping gates.' end end
    return true
end
function M.compile(project,song,MIDI,mode,format)
    local notes={};local function add(lane,pitch,velocity,time,gate)
        local frame=math.max(0,math.floor(time*M.rate+0.5))
        notes[#notes+1]={frame=frame,duration=math.max(1,math.floor(gate*M.rate+0.5)),lane=lane,note=pitch,
            velocity=math.max(1,math.min(127,math.floor(velocity*127+0.5))),order=#notes+1}
        assert(#notes<=M.maxNotes,'Compiled score exceeds 60,000 notes.')
        return notes[#notes]
    end
    local songMode=mode=='song';local timeline=project.timeline or song
    assert(not songMode or timeline,'Import a MIDI file first.')
    if songMode then for _,t in ipairs(project.tracks) do assert(t.kind~='midi' or t.edited or song,'The original MIDI file is required for unedited parts.') end end
    local function seconds(beat)
        if not songMode then return beat*60/project.bpm end
        if project.timeline then return format.seconds(timeline,beat) end
        local t={time=0,beat=0,tempo=500000}
        for _,tempo in ipairs(timeline.tempos or {}) do if tempo.beat>beat then break end;t=tempo end
        return t.time+(beat-t.beat)*t.tempo/1000000
    end
    local beats=songMode and timeline.beats or project.length/4
    if songMode then for _,t in ipairs(project.tracks) do if t.edited then
        for _,n in ipairs(assert(format.decodeNotes(t.edited))) do beats=math.max(beats,(n.s+n.l)/4) end
    end end end
    local total=songMode and math.max(project.timeline and 0 or song.duration,seconds(beats)) or seconds(beats)
    if songMode and song then
        local routes,pending,at={},{},0
        local sourceEnd=project.timeline and seconds(timeline.beats) or song.duration
        for lane,t in ipairs(project.tracks) do if t.kind=='midi' and not t.edited then routes[t.part]=lane end end
        local player=MIDI.player(song,{on=function(note,velocity,_,part)
            local lane=routes[part];if not lane then return end
            local key=lane*128+note;pending[key]=pending[key] or {}
            pending[key][#pending[key]+1]=add(lane,note,velocity,at,math.max(1/M.rate,sourceEnd-at))
        end,off=function(note,_,part)
            local lane=routes[part];if not lane then return end
            local key=lane*128+note
            for _,n in ipairs(pending[key] or {}) do n.duration=math.max(1,math.floor(at*M.rate+0.5)-n.frame) end
            pending[key]=nil
        end,panic=function() end},{multitimbral=true,percussion=true})
        for _,e in ipairs(song.events) do at=project.timeline and seconds(e.tick/song.division) or e.time;player:event(e) end
    end
    if songMode then for lane,t in ipairs(project.tracks) do if t.edited then
        for _,n in ipairs(assert(format.decodeNotes(t.edited))) do
            local time=seconds(n.s/4);add(lane,n.n,n.v,time,seconds((n.s+n.l)/4)-time)
        end
    end end end
    local period=project.length/4
    local cycles=songMode and (project.loop and math.ceil(beats/period) or 1) or 1
    for cycle=0,cycles-1 do
        for lane,t in ipairs(project.tracks) do
            local function clip(note,velocity,step,length,gateSeconds)
                local beat=cycle*period+step/4;beat=beat+(step%2==1 and project.swing/4 or 0)
                if step>=project.length or beat>=beats then return end
                local time=seconds(beat);local gate=gateSeconds or seconds(beat+length/4)-time
                if songMode or not project.loop then gate=math.min(gate,total-time) end
                if gate>0 then add(lane,note,velocity,time,gate) end
            end
            if t.kind=='drum' then
                for step=0,project.length-1 do local velocity=tonumber(t.steps:sub(step+1,step+1)) or 0
                    if velocity>0 then clip(t.note,velocity/9,step,1,t.gate) end
                end
            elseif t.kind=='piano' then for _,n in ipairs(t.notes) do clip(n.n,n.v,n.s,n.l) end end
        end
    end
    table.sort(notes,function(a,b) return a.frame==b.frame and a.order<b.order or a.frame<b.frame end)
    local length=math.max(1,math.floor(total*M.rate+0.5));local bytes={}
    for _,n in ipairs(notes) do
        if n.frame<length then
            if songMode or not project.loop then n.duration=math.min(n.duration,length-n.frame) end
            bytes[#bytes+1]=pack(n.frame)..pack(n.duration)..string.char(n.lane,n.note,n.velocity)
        end
    end
    bytes=table.concat(bytes);assert(#bytes>0,'No notes to play. Add notes or choose another playback view.');local loop=not songMode and project.loop
    local ok,err=M.validate(bytes,length,loop);assert(ok,err)
    return {bytes=bytes,length=length,loop=loop,quantum=math.floor(4*60/project.bpm*M.rate+0.5)}
end
return M
