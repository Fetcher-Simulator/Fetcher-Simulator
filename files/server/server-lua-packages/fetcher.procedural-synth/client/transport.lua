local Sequence=require('scripts.multiplayer.fetcher.procedural-synth.shared.sequence')
local T={};T.__index=T
function T.new(synth,mp,core,message)
    return setmetatable({synth=synth,mp=mp,core=core,message=message,downloads={},clockAt=-10,lastClock=-10,pings={},samples=0},T)
end
function T:ping()
    local now=self.core.getRealTime();self.lastClock=now
    local count=0;for sent in pairs(self.pings) do if now-sent>5 then self.pings[sent]=nil else count=count+1 end end
    if count>=4 then return end
    self.pings[now]=true
    self.mp.sendToServer('Synth_Request',{version=1,op='clock',sent=now})
end
function T:studio(i)
    if not i.studio or i.studio.closed then
        i.studio=self.synth.createStudio(i.source)
        assert(i.studio:effects(i.effects or {},i.master or 0.7),'Invalid return settings')
    end
    return i.studio
end
function T:configure(i,n)
    local c=i.channels[n]
    assert(self:studio(i):configure(n,c.params,c.level),'Native instrument budget reached (32 units). Finish another performance.')
    assert(i.studio:mixer(n,c.mixer or {}),'Invalid mixer state')
end
function T:audio(i,n)
    self:configure(i,n);local studio=i.studio;local closed=false
    local audio={}
    function audio:set(name,value) return studio:set(n,name,value) end
    function audio:noteOn(note,velocity) return studio:noteOn(n,note,velocity) end
    function audio:noteOff(note) return studio:noteOff(n,note) end
    function audio:allNotesOff() studio:releaseTrack(n) end
    function audio:close() studio:releaseTrack(n);studio:set(n,'volume',0);closed=true end
    function audio:keepAlive() end -- the shared stream is kept alive once per frame
    return setmetatable(audio,{__index=function(_,key)
        if key=='peak' then return studio:trackPeak(n) elseif key=='voices' then return studio:trackVoices(n)
        elseif key=='closed' then return closed or studio.closed end
    end})
end
function T:queue(i,plan,replace,send)
    local ok,err=pcall(function()
        self:studio(i);for n,c in pairs(i.channels) do if not c.audio then c.audio=self:audio(i,n) else self:configure(i,n) end end
    end)
    if not ok then self.message(tostring(err));return false end
    i.nextPlayId=(i.nextPlayId or 0)+1
    i.upload={plan=plan,replace=replace,playId=i.nextPlayId,offset=1,index=1,budget=8,last=self.core.getRealTime(),send=send}
    i.scoreDeadline=self.core.getRealTime()+25
    send('scoreBegin',{playId=i.nextPlayId,count=#plan.bytes/11,length=plan.length,loop=plan.loop,quantum=plan.quantum,replace=replace})
    if not self.offset then self:ping() end
    return true
end
function T:stop(i,send)
    if i.studio then i.studio:stop() end
    if send and (i.upload or i.transport or i.nextPlayId and i.nextPlayId>(i.stoppedPlayId or 0)) then
        send('transportStop',{playId=i.nextPlayId})
    end
    i.stoppedPlayId=i.nextPlayId or i.stoppedPlayId or 0;i.upload=nil;i.transport=nil;i.scoreDeadline=nil;self.downloads[i.id]=nil
end
function T:receive(data,instances)
    if data.op=='clock' then
        local now=self.core.getRealTime();local rtt=now-(data.sent or -1e12)
        if self.pings[data.sent] and rtt>=0 and rtt<5 and type(data.serverTime)=='number' then
            self.pings[data.sent]=nil;self.samples=self.samples+1
            if not self.rtt or now-self.clockAt>30 or rtt<=self.rtt then
                self.offset=data.serverTime-(data.sent+now)/2;self.rtt=rtt;self.clockAt=now
            end
        end
        return true
    end
    if data.op~='scoreBegin' and data.op~='scoreChunk' and data.op~='scoreReady' and data.op~='scoreError' and data.op~='transportStop' then return false end
    local i=instances[data.id];local old=self.downloads[data.id]
    if i and i.epoch~=data.epoch then return true end
    if old and type(data.epoch)=='number' and old.epoch>data.epoch then return true end
    if i and (data.playId or 0)<=(i.stoppedPlayId or 0) then return true end
    if i and i.workstation and data.playId and data.playId<(i.nextPlayId or 0) then return true end
    if data.op=='scoreError' then
        if i and i.workstation then i.workstation:stop(true) end;self.message(data.message or 'Score rejected.');return true
    elseif data.op=='transportStop' then
        if i then
            i.stoppedPlayId=math.max(i.stoppedPlayId or 0,data.playId or 0)
            if i.studio then i.studio:stop() end;i.transport=nil
            if i.workstation then i.workstation.playing=false;i.workstation.native=false;i.workstation.awaiting=false end
        end
        self.downloads[data.id]=nil;return true
    elseif data.op=='scoreBegin' then
        if type(data.playId)~='number' or data.playId%1~=0 or data.playId<1 or type(data.epoch)~='number' then return true end
        if i and i.transport and i.transport.playId==data.playId then return true end
        if type(data.count)~='number' or data.count<1 or data.count>Sequence.maxNotes then return true end
        if old and old.epoch==data.epoch and old.playId>data.playId then return true end
        data.chunks={};data.size=0;data.next=1;data.received=self.core.getRealTime();self.downloads[data.id]=data
    elseif old and old.playId==data.playId and old.epoch==data.epoch then
        if data.op=='scoreReady' then old.ready=true
        elseif data.index==old.next and type(data.bytes)=='string' and #data.bytes<=Sequence.chunkSize and old.size+#data.bytes<=old.count*11 then
            old.chunks[#old.chunks+1]=data.bytes;old.size=old.size+#data.bytes;old.next=old.next+1
        end
    end
    return true
end
function T:finish(i,d)
    local bytes=table.concat(d.chunks);local valid,err=Sequence.validate(bytes,d.length,d.loop,i.channels)
    if not valid then error(err) end
    self:studio(i);for n,c in pairs(i.channels) do if not c.audio then c.audio=self:audio(i,n) else self:configure(i,n) end end
    assert(i.studio:loadScore(bytes,d.length,d.loop),'Native score rejected.')
    local now=self.core.getRealTime()+self.offset
    local delay=math.max(0,d.activate-now)
    if d.replace and i.transport and i.studio.playing then assert(i.studio:replace(delay),'Native replacement rejected.')
    else
        local offset=math.max(0,math.max(now,d.activate)-d.started)
        if d.loop then offset=offset%(d.length/Sequence.rate) elseif offset>=d.length/Sequence.rate then
            i.scoreDeadline=nil
            if i.workstation then i.workstation.awaiting=false;i.workstation.playing=false;i.workstation.native=false end
            return
        end
        assert(i.studio:start(delay,offset),'Native transport start rejected.')
    end
    i.transport={playId=d.playId,started=d.started,length=d.length,loop=d.loop}
    i.scoreDeadline=nil
    if i.workstation then i.workstation.awaiting=false end
end
function T:frame(instances)
    local now=self.core.getRealTime()
    if now-self.lastClock>(self.samples<3 and 0.25 or 5) then self:ping() end
    for id,i in pairs(instances) do
        if i.scoreDeadline and now>i.scoreDeadline then
            i.scoreDeadline=nil;if i.workstation then i.workstation:stop(true) end;self.message('Score transfer timed out; playback stopped.')
        end
        if i.studio and now-i.seen<5 then i.studio:keepAlive() end
        local u=i.upload
        -- Send channel settings ahead of the score: a fast edit/load can queue
        -- more settings than one network burst permits.
        if u and not next(i.pendingChannels or {}) then
            u.budget=math.min(8,u.budget+math.max(0,now-u.last)*32);u.last=now
            while u.budget>=1 and u.offset<=#u.plan.bytes do
                local bytes=u.plan.bytes:sub(u.offset,u.offset+Sequence.chunkSize-1)
                u.send('scoreChunk',{playId=u.playId,index=u.index,bytes=bytes})
                u.offset=u.offset+#bytes;u.index=u.index+1;u.budget=u.budget-1
            end
            if u.offset>#u.plan.bytes then u.send('scoreCommit',{playId=u.playId});i.upload=nil end
        end
        local d=self.downloads[id]
        if d and d.epoch==i.epoch and d.ready and d.size==d.count*11 and self.offset and (self.samples>=3 or self.rtt<0.05) then
            self.downloads[id]=nil
            local ok,err=pcall(self.finish,self,i,d)
            if not ok then
                if i.workstation then i.workstation:stop(true) end;self.message(tostring(err))
            end
        end
    end
    for id,d in pairs(self.downloads) do if now-d.received>20 then self.downloads[id]=nil end end
end
return T
