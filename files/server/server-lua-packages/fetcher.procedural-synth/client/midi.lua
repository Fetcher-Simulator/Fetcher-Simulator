-- Standard MIDI File reader, independent of Bardcraft's ESP and sample player.
-- Format 0/1, PPQ, tempo map, running status, notes and channel controls.
local M={}
function M.parse(bytes, checkpoint)
    assert(type(bytes)=='string' and #bytes<=512*1024,'MIDI must be at most 512 KiB')
    local pos,limit,operations=1,#bytes,0
    local function step()
        operations=operations+1
        if checkpoint and operations%256==0 then checkpoint() end
    end
    local function take(n)
        assert(n>=0 and pos+n-1<=limit,'Truncated MIDI chunk')
        local result=bytes:sub(pos,pos+n-1); pos=pos+n; return result
    end
    local function number(n)
        local v=0
        for _,b in ipairs{take(n):byte(1,n)} do v=v*256+b end
        return v
    end
    local function vlq()
        local v=0
        for _=1,4 do local b=number(1);v=v*128+b%128;if b<128 then return v end end
        error('MIDI VLQ exceeds four bytes')
    end
    assert(take(4)=='MThd','Expected MIDI header')
    local headerLength=number(4)
    assert(headerLength>=6 and headerLength<=1024,'Invalid MIDI header length')
    local format,tracks,division=number(2),number(2),number(2)
    assert(format<=1 and tracks>=1 and tracks<=64 and (format~=0 or tracks==1),'Unsupported MIDI format/track count')
    assert(division>0 and division<32768,'SMPTE MIDI timing is not supported')
    take(headerLength-6)
    local events,order,names,programs={},0,{},{}
    local currentTrack=0
    local function event(tick,kind,channel,note,velocity)
        order=order+1;assert(order<=60000,'MIDI event limit exceeded')
        local part=tostring(currentTrack)..':'..tostring(channel)
        if channel==9 and (kind=='on' or kind=='off') then part=part..':'..note end
        events[#events+1]={tick=tick,kind=kind,channel=channel,note=note,velocity=velocity,order=order,track=currentTrack,part=part}
    end
    local lastTick=0
    for track=1,tracks do
        currentTrack=track
        limit=#bytes
        assert(take(4)=='MTrk','Expected MIDI track')
        local length=number(4)
        assert(length<=#bytes-pos+1,'Truncated MIDI track')
        local ending=pos+length
        limit=ending-1
        local tick,running=0,nil
        while pos<ending do
            step();tick=tick+vlq();lastTick=math.max(lastTick,tick)
            local status=bytes:byte(pos)
            assert(status,'Missing MIDI status')
            if status>=128 then pos=pos+1 else assert(running,'Missing running status');status=running end
            if status<240 then
                running=status
                local kind,channel=math.floor(status/16),status%16
                local a=number(1);assert(a<128,'Invalid MIDI data byte')
                local b
                if kind~=12 and kind~=13 then b=number(1);assert(b<128,'Invalid MIDI data byte') end
                if kind==8 then event(tick,'off',channel,a)
                elseif kind==9 then event(tick,b==0 and 'off' or 'on',channel,a,b/127)
                elseif kind==11 then event(tick,'control',channel,a,b)
                elseif kind==12 then event(tick,'program',channel,a) end
            elseif status==255 then
                running=nil
                local kind=number(1)
                local n=vlq()
                if kind==3 then names[track]=take(n):gsub('%c',' '):sub(1,40)
                elseif kind==81 then
                    assert(n==3,'Invalid MIDI tempo')
                    local tempo=number(3);assert(tempo>0,'Invalid MIDI tempo')
                    event(tick,'tempo',nil,nil,tempo)
                else
                    take(n)
                    if kind==47 then assert(n==0,'Invalid MIDI end marker');pos=ending end
                end
            elseif status==240 or status==247 then running=nil;take(vlq())
            else error('Unsupported MIDI system message') end
        end
        pos=ending
    end
    table.sort(events,function(a,b)
        if a.tick~=b.tick then return a.tick<b.tick end
        if (a.kind=='program')~=(b.kind=='program') then return a.kind=='program' end
        return a.order<b.order
    end)
    local tick,seconds,tempo=0,0,500000
    local tempos={}
    local playable={}
    local channelPrograms,assigned={},{}
    for _,e in ipairs(events) do
        step()
        if e.kind=='program' then channelPrograms[e.channel]=e.note
        else
            -- Metadata must not split the clock's floating-point accumulation:
            -- preserve the exact note/tempo timing of the existing reader.
            seconds=seconds+(e.tick-tick)*tempo/(division*1000000);tick=e.tick
            assert(seconds<=1800,'MIDI exceeds 30 minutes')
            if e.kind=='tempo' then tempo=e.velocity;tempos[#tempos+1]={beat=e.tick/division,time=seconds,tempo=tempo}
            else
                if e.kind=='on' and not assigned[e.part] then programs[e.part]=channelPrograms[e.channel];assigned[e.part]=true end
                e.time=seconds;playable[#playable+1]=e
            end
        end
    end
    seconds=seconds+(lastTick-tick)*tempo/(division*1000000)
    assert(seconds<=1800,'MIDI exceeds 30 minutes')
    return {events=playable,duration=seconds,division=division,tracks=tracks,names=names,programs=programs,tempos=tempos,beats=lastTick/division}
end

-- Each (SMF track, MIDI channel) is a part. Preserve track names even when
-- several channels occur in one track, and never infer identity from pitch.
function M.parts(song)
    local list,byId={},{}
    for _,e in ipairs(song.events) do
        if e.kind=='on' and not byId[e.part] then
            local p={id=e.part,track=e.track,channel=e.channel,program=song.programs[e.part],
                name=(song.names[e.track] or ('Track '..e.track))..' / Ch '..(e.channel+1)..(e.channel==9 and (' / Drum '..e.note) or ''),note=e.channel==9 and e.note or nil}
            list[#list+1]=p;byId[p.id]=p
        end
    end
    return list
end
function M.noteSpans(song)
    local spans,held={},{}
    for _,e in ipairs(song.events) do
        local key=e.part..'/'..tostring(e.note)
        if e.kind=='on' then
            local n={n=e.note,s=e.tick/song.division*4,l=1,v=e.velocity}
            spans[e.part]=spans[e.part] or {};spans[e.part][#spans[e.part]+1]=n
            held[key]=held[key] or {first=1,last=0};local q=held[key];q.last=q.last+1;q[q.last]=n
        elseif e.kind=='off' and held[key] and held[key].first<=held[key].last then
            local q=held[key];local n=q[q.first];q[q.first]=nil;q.first=q.first+1;n.l=math.max(0.1,e.tick/song.division*4-n.s)
        end
    end
    return spans
end
function M.player(song,callbacks,options)
    options=options or {}
    local p={song=song,callbacks=callbacks,index=1,time=0,playing=true,held={},sounding={},pedal={},counts={}}
    local function identity(channel,note,part)
        return (options.multitimbral and part or channel)..'/'..note
    end
    local function output(note,part) return options.multitimbral and (part..'/'..note) or note end
    function p:release(channel,note,part)
        local key=identity(channel,note,part)
        if not self.sounding[key] then return end
        self.sounding[key]=nil
        local out=output(note,part);self.counts[out]=self.counts[out]-1
        if self.counts[out]==0 then callbacks.off(note,channel,part) end
    end
    function p:event(e)
        local channel,note,part=e.channel,e.note,e.part or tostring(e.channel)
        if channel==9 and options.percussion~=true then return end
        local key=identity(channel,note,part)
        if e.kind=='on' then
            self.held[key]=(self.held[key] or 0)+1
            if not self.sounding[key] then
                self.sounding[key]={channel=channel,note=note,part=part}
                local out=output(note,part);self.counts[out]=(self.counts[out] or 0)+1
            end
            callbacks.on(note,e.velocity,channel,part)
        elseif e.kind=='off' then
            local remaining=math.max(0,(self.held[key] or 0)-1)
            self.held[key]=remaining>0 and remaining or nil
            if not self.held[key] and not self.pedal[channel] then self:release(channel,note,part) end
        elseif e.kind=='control' then
            if note==64 or note==121 or note==120 or note==123 then
                self.pedal[channel]=note==64 and e.velocity>=64
                local pending={}
                for k,n in pairs(self.sounding) do
                    if n.channel==channel then
                        if note==120 or note==123 then self.held[k]=nil end
                        if not self.pedal[channel] and not self.held[k] then pending[#pending+1]=n end
                    end
                end
                for _,n in ipairs(pending) do self:release(n.channel,n.note,n.part) end
            end
        end
    end
    function p:stop(reason)
        if not self.playing then return end
        self.playing=false;self.held={};self.sounding={};self.counts={};self.pedal={}
        callbacks.panic()
        if callbacks.finished then callbacks.finished(reason) end
    end
    function p:update(dt)
        if not self.playing then return end
        if type(dt)~='number' or dt~=dt or dt<0 or dt>0.25 then self:stop('Playback stopped after a frame stall');return end
        self.time=self.time+dt
        local dispatched=0
        while self.song.events[self.index] and self.song.events[self.index].time<=self.time do
            dispatched=dispatched+1
            if dispatched>256 then self:stop('MIDI event density exceeds playback limit');return end
            self:event(self.song.events[self.index]);self.index=self.index+1
            if not self.playing then return end
        end
        if self.index>#self.song.events and self.time>=self.song.duration then self:stop() end
    end
    return p
end
-- Bake the same effective gates used by native playback, including sustain,
-- overlapping same-pitch notes and all-notes-off controls, into editable ticks.
function M.editableSpans(song,part)
    local notes,pending,at={},{},0
    local player=M.player(song,{on=function(note,velocity,_,id)
        if id~=part or at>=song.beats*song.division then return end
        local n={n=note,s=at/song.division*4,l=math.max(1,(song.beats or 0)*song.division-at)/song.division*4,v=velocity}
        notes[#notes+1]=n;pending[note]=pending[note] or {};pending[note][#pending[note]+1]=n
    end,off=function(note,_,id)
        if id~=part then return end
        for _,n in ipairs(pending[note] or {}) do n.l=math.max(0,at/song.division*4-n.s) end
        pending[note]=nil
    end,panic=function() end},{multitimbral=true,percussion=true})
    for _,e in ipairs(song.events) do at=e.tick;player:event(e) end
    return notes
end
return M
