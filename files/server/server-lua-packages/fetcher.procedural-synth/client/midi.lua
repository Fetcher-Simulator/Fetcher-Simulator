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
    local events,order={},0
    local function event(tick,kind,channel,note,velocity)
        order=order+1;assert(order<=60000,'MIDI event limit exceeded')
        events[#events+1]={tick=tick,kind=kind,channel=channel,note=note,velocity=velocity,order=order}
    end
    local lastTick=0
    for _=1,tracks do
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
                elseif kind==11 then event(tick,'control',channel,a,b) end
            elseif status==255 then
                running=nil
                local kind=number(1)
                local n=vlq()
                if kind==81 then
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
    table.sort(events,function(a,b) if a.tick==b.tick then return a.order<b.order end return a.tick<b.tick end)
    local tick,seconds,tempo=0,0,500000
    local playable={}
    for _,e in ipairs(events) do
        step();seconds=seconds+(e.tick-tick)*tempo/(division*1000000);tick=e.tick
        assert(seconds<=1800,'MIDI exceeds 30 minutes')
        if e.kind=='tempo' then tempo=e.velocity
        else e.time=seconds;playable[#playable+1]=e end
    end
    seconds=seconds+(lastTick-tick)*tempo/(division*1000000)
    assert(seconds<=1800,'MIDI exceeds 30 minutes')
    return {events=playable,duration=seconds,division=division,tracks=tracks}
end

-- Note identity is per channel/pitch; the mono-timbral synth has pitch identity.
-- Reference counts prevent a note-off on one track cutting another channel off.
function M.player(song, callbacks, options)
    options=options or {}
    local p={song=song,callbacks=callbacks,index=1,time=0,playing=true,held={},sounding={},pedal={},counts={}}
    function p:release(channel,note)
        local key=channel*128+note
        if not self.sounding[key] then return end
        self.sounding[key]=nil
        self.counts[note]=self.counts[note]-1
        if self.counts[note]==0 then callbacks.off(note) end
    end
    function p:event(e)
        local channel,note=e.channel,e.note
        if channel==9 and options.percussion~=true then return end
        local key=channel*128+note
        if e.kind=='on' then
            self.held[key]=(self.held[key] or 0)+1
            if not self.sounding[key] then
                self.sounding[key]=true;self.counts[note]=(self.counts[note] or 0)+1
            end
            callbacks.on(note,e.velocity)
        elseif e.kind=='off' then
            local remaining=math.max(0,(self.held[key] or 0)-1)
            self.held[key]=remaining>0 and remaining or nil
            if not self.held[key] and not self.pedal[channel] then self:release(channel,note) end
        elseif e.kind=='control' then
            if note==64 or note==121 then
                self.pedal[channel]=note==64 and e.velocity>=64
                if not self.pedal[channel] then
                    for n=0,127 do if not self.held[channel*128+n] then self:release(channel,n) end end
                end
            elseif note==120 or note==123 then
                for n=0,127 do self.held[channel*128+n]=nil;self:release(channel,n) end
                self.pedal[channel]=nil
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
return M
