local Sequence=require('scripts.multiplayer.fetcher.procedural-synth.shared.sequence')
local T={};T.__index=T
function T.new(synth,mp,core,message,resolve)
    return setmetatable({synth=synth,mp=mp,core=core,message=message,downloads={},clockAt=-10,lastClock=-10,pings={},samples=0,resolve=resolve},T)
end
function T:ping()
    local now=self.core.getRealTime();self.lastClock=now
    local count=0;for sent in pairs(self.pings) do if now-sent>5 then self.pings[sent]=nil else count=count+1 end end
    if count>=4 then return end
    self.pings[now]=true
    self.mp.sendToServer('Synth_Request',{version=1,op='clock',sent=now})
end
function T:destination(i,n)
    local slot=i.routes and i.routes[n] or 0
    if slot==0 then return 0 end
    if i.availability and i.availability[slot]=='Ready' then return slot end
end
function T:each(i,fn)
    if i.studio and not i.studio.closed then fn(i.studio,0) end
    for slot,group in pairs(i.outputs or {}) do if not group.audio.closed then fn(group.audio,slot) end end
end
function T:close(i)
    self:each(i,function(a) a:close() end);i.studio=nil;i.outputs={}
    for _,c in pairs(i.channels or {}) do c.audio=nil end
end
function T:applyEffects(i)
    self:each(i,function(a,slot)
        local m=slot>0 and i.band and i.band[slot]
        a:effects(i.effects or {},(i.master or 0.7)*(m and (m.mute and 0 or m.level) or 1))
    end)
end
function T:band(i,band,routes,availability,performers)
    band=band or {};routes=routes or {};availability=availability or {}
    local structural=false
    for n=1,Sequence.maxTracks do if (routes[n] or 0)~=(i.routes and i.routes[n] or 0) then structural=true end end
    for n=1,8 do
        local a,b=band[n] and band[n].target,i.band and i.band[n] and i.band[n].target
        if (a and a.mpNum)~=(b and b.mpNum) or (a and a.characterId)~=(b and b.characterId) or (a and a.instrument)~=(b and b.instrument) or (a and a.cell)~=(b and b.cell) then structural=true end
    end
    if structural then self:close(i);i.transport=nil;i.lastScore=nil end
    i.band=band;i.routes=routes;i.availability=availability;i.performers=performers or {}
    for slot,g in pairs(i.outputs or {}) do if availability[slot]~='Ready' then g.audio:close();i.outputs[slot]=nil end end
    self:applyEffects(i)
    for n in pairs(i.channels or {}) do self:configure(i,n) end
    -- A reappearing bound object rejoins the current song phase. Existing
    -- outputs keep their voices and clocks; no other part changes destination.
    if i.transport and i.lastScore and self.offset then
        for slot,g in pairs(i.outputs or {}) do if not g.started then
            local d=i.lastScore;local chunks={}
            for at=1,#d.bytes,11 do if self:destination(i,d.bytes:byte(at+8))==slot then chunks[#chunks+1]=d.bytes:sub(at,at+10) end end
            assert(g.audio:loadScore(table.concat(chunks),d.length,d.loop),'Native rejoin score rejected.')
            local offset=math.max(0,self.core.getRealTime()+self.offset-d.started)
            if d.loop then offset=offset%(d.length/Sequence.rate) end
            if d.loop or offset<d.length/Sequence.rate then assert(g.audio:start(0,offset),'Native rejoin rejected.') end
            g.started=true
        end end
    end
end
function T:studio(i,slot)
    slot=slot or 0
    if slot==0 then
        if not i.studio or i.studio.closed then
            i.studio=self.synth.createStudio(i.source)
            assert(i.studio:effects(i.effects or {},i.master or 0.7),'Invalid return settings')
        end
        return i.studio
    end
    if not i.availability or i.availability[slot]~='Ready' then return end
    i.outputs=i.outputs or {};local g=i.outputs[slot]
    if not g or g.audio.closed then
        local target=i.band[slot].target;local source=self.resolve and self.resolve(target.mpNum,i.performers and i.performers[slot])
        if not source then return end -- A missing source never falls back to the workstation.
        g={source=source,audio=self.synth.createStudio(source),id=target.mpNum or i.performers[slot].guid};i.outputs[slot]=g;self:applyEffects(i)
    end
    return g.audio
end
function T:laneStudio(i,n,create)
    local slot=self:destination(i,n);if slot==nil then return end
    if create then return self:studio(i,slot) end
    return slot==0 and i.studio or i.outputs and i.outputs[slot] and i.outputs[slot].audio
end
function T:configure(i,n)
    local c=i.channels[n];local a=self:laneStudio(i,n,true)
    if not a then return end
    assert(a:configure(n,c.params,c.level),'Native instrument budget reached ('..(self.synth.maxUnits or 32)..' units). Finish another performance.')
    assert(a:mixer(n,c.mixer or {}),'Invalid mixer state')
end
function T:metrics(i,n)
    local a=n>0 and self:laneStudio(i,n,false)
    if n>0 then return a and a:trackPeak(n) or 0,a and a:trackRms(n) or 0,a and a:trackReduction(n) or 0 end
    local peak,rms=0,0
    self:each(i,function(output) peak=math.max(peak,output.mixPeak or 0);rms=math.max(rms,output.masterRms or 0) end)
    return peak,rms,0
end
function T:audio(i,n)
    self:configure(i,n);local owner=self;local closed=false;local audio={}
    function audio:set(name,value) local a=owner:laneStudio(i,n,false);return a and a:set(n,name,value) end
    function audio:noteOn(note,velocity) owner:configure(i,n);local a=owner:laneStudio(i,n,false);return not a or a:noteOn(n,note,velocity) end
    function audio:noteOff(note) local a=owner:laneStudio(i,n,false);return not a or a:noteOff(n,note) end
    function audio:allNotesOff() local a=owner:laneStudio(i,n,false);if a then a:releaseTrack(n) end end
    function audio:close() local a=owner:laneStudio(i,n,false);if a then a:releaseTrack(n);a:set(n,'volume',0) end;closed=true end
    function audio:keepAlive() end
    return setmetatable(audio,{__index=function(_,key)
        local a=owner:laneStudio(i,n,false)
        if key=='peak' then return a and a:trackPeak(n) or 0 elseif key=='voices' then return a and a:trackVoices(n) or 0
        elseif key=='closed' then return closed end
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
    if not self.offset then self:ping() end
    return true
end
function T:stop(i,send)
    self:each(i,function(a) a:stop() end)
    if send and (i.upload or i.transport or i.nextPlayId and i.nextPlayId>(i.stoppedPlayId or 0)) then
        send('transportStop',{playId=i.nextPlayId})
    end
    i.lastScore=nil
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
            self:each(i,function(a) a:stop() end);i.transport=nil;i.lastScore=nil
            if i.workstation then i.workstation.playing=false;i.workstation.native=false;i.workstation.awaiting=false end
        end
        self.downloads[data.id]=nil;return true
    elseif data.op=='scoreBegin' then
        if type(data.playId)~='number' or data.playId%1~=0 or data.playId<1 or type(data.epoch)~='number' then return true end
        if i and i.transport and i.transport.playId==data.playId then return true end
        if type(data.count)~='number' or data.count<1 or data.count>Sequence.maxEvents then return true end
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
    local split={[0]={}}
    for at=1,#bytes,11 do
        local n=bytes:byte(at+8);local slot=self:destination(i,n)
        if slot~=nil and self:laneStudio(i,n,false) then split[slot]=split[slot] or {};split[slot][#split[slot]+1]=bytes:sub(at,at+10) end
    end
    -- Empty native scores keep the home clock and silent outputs in phase.
    self:each(i,function(a,slot) assert(a:loadScore(table.concat(split[slot] or {}),d.length,d.loop),'Native score rejected.') end)
    local finished=false
    self:each(i,function(a)
        local now=self.core.getRealTime()+self.offset
        local delay=math.max(0,d.activate-now)
        if d.replace and i.transport and a.playing then assert(a:replace(delay),'Native replacement rejected.')
        else
            local offset=math.max(0,math.max(now,d.activate)-d.started)
            if d.loop then offset=offset%(d.length/Sequence.rate) elseif offset>=d.length/Sequence.rate then finished=true;return end
            assert(a:start(delay,offset),'Native transport start rejected.')
        end
    end)
    if finished then
        i.scoreDeadline=nil
        if i.workstation then i.workstation.awaiting=false;i.workstation.playing=false;i.workstation.native=false end
        return
    end
    for _,g in pairs(i.outputs or {}) do g.started=true end
    i.lastScore={bytes=bytes,length=d.length,loop=d.loop,started=d.started,activate=d.activate,playId=d.playId}
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
        if now-i.seen<5 then self:each(i,function(a) a:keepAlive() end) end
        for slot,g in pairs(i.outputs or {}) do
            if not g.source:isValid() then g.audio:close();i.outputs[slot]=nil end
        end
        -- Actor snapshots can precede the actor's local scene insertion.
        -- Retry unresolved Ready outputs only when their exact source is loaded.
        if now-(i.sourceRetry or -10)>0.5 then
            i.sourceRetry=now;local missing=false
            for n in pairs(i.channels) do local slot=self:destination(i,n)
                if slot and slot>0 and not (i.outputs and i.outputs[slot]) then
                    local t=i.band[slot].target
                    if self.resolve and self.resolve(t.mpNum,i.performers and i.performers[slot]) then missing=true end
                end
            end
            if missing then local ok,err=pcall(self.band,self,i,i.band,i.routes,i.availability,i.performers)
                if not ok then self:stop(i);self.message(tostring(err)) end
            end
        end
        local u=i.upload
        -- Send channel settings ahead of the score: a fast edit/load can queue
        -- more settings than one network burst permits.
        if u and not i.pendingBand and not next(i.pendingChannels or {}) then
            if not u.began then
                u.send('scoreBegin',{playId=u.playId,count=#u.plan.bytes/11,length=u.plan.length,loop=u.plan.loop,quantum=u.plan.quantum,barGrid=u.plan.barGrid,replace=u.replace})
                u.began=true
            end
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
                self:each(i,function(a) a:stop() end)
                if i.workstation then i.workstation:stop(true) end;self.message(tostring(err))
            end
        end
    end
    for id,d in pairs(self.downloads) do if now-d.received>20 then self.downloads[id]=nil end end
end
return T
