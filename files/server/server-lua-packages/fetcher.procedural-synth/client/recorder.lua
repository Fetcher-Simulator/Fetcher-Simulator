-- Local recording preferences, native click clock and timestamped input routing.
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=require(prefix..'shared.schema');local Format=require(prefix..'shared.project')(schema)
local R={};R.__index=R
local function finite(v,a,b) return type(v)=='number' and v==v and v>=a and v<=b end
local function pack(n) local a=n%256;n=math.floor(n/256);local b=n%256;n=math.floor(n/256);return string.char(a,b,n%256,math.floor(n/256)) end
function R.options(values)
    values=values or {};local bars=values.countIn
    return {countIn=({[0]=true,[1]=true,[2]=true,[4]=true})[bars] and bars or 1,
        metronome=values.metronome==true,countClick=values.countClick~=false,clickVolume=finite(values.clickVolume,0,1) and values.clickVolume or .55,
        latency=finite(values.latency,-250,250) and values.latency or 0,
        channel=finite(values.channel,0,16) and values.channel%1==0 and values.channel or 0}
end
function R.plan(w,mode,first,last,loop,count,enabled,countClick)
    local song=mode=='song';local p=song and {timeline=w.project.timeline or w.song,composition=w.project.composition} or {}
    local function seconds(beat) return song and Format.seconds(p.timeline,beat) or beat*60/w.project.bpm end
    local origin=seconds(first);local duration=seconds(last)-origin+(count and count.duration or 0)
    assert(duration>0 and duration<=1800,'Count-in plus recording length must fit within 30 minutes.')
    local length=math.max(1,math.floor(duration*48000+.5));local bytes={};local previous=-2400
    local function add(time,accent)
        local frame=math.max(0,math.floor(time*48000+.5))
        if frame>=length or frame-previous<2400 then return end -- bounded 20 Hz at extreme meters/tempos
        previous=frame;bytes[#bytes+1]=pack(frame)..pack(math.min(720,length-frame))..string.char(1,accent and 84 or 76,accent and 110 or 75)
    end
    if enabled or countClick and count and count.duration>0 then
        assert(Format.barBoundaries(p,first,last,4097),'Metronome supports at most 4096 bars per take.')
        if count and count.duration>0 and (countClick==true or countClick==nil and enabled) then
            local pulse=4/count.denominator;local total=count.beats*count.bars
            local beat=0;while beat<total-1e-8 do add(beat*count.tempo,math.abs(beat%count.beats)<1e-8);beat=beat+pulse end
        end
        local bar=Format.barAt(p,first)
        if enabled then
            while Format.barStart(p,bar)<last-1e-8 do
                local start,finish=Format.barStart(p,bar),math.min(last,Format.barStart(p,bar+1));local _,d=Format.meterAt(p,start)
                local beat=start;while beat<finish-1e-8 do
                    if beat>=first-1e-8 then add((count and count.duration or 0)+seconds(beat)-origin,beat==start) end
                    beat=beat+4/d
                end
                bar=bar+1
            end
        end
    end
    return {bytes=table.concat(bytes),length=length,loop=loop}
end
function R.calculation(taps)
    if #taps<8 then return end
    local sorted={};for _,v in ipairs(taps) do sorted[#sorted+1]=v end;table.sort(sorted)
    local function median(t) local n=#t;return n%2==1 and t[math.ceil(n/2)] or (t[n/2]+t[n/2+1])/2 end
    local value=median(sorted);local deviations={};for _,v in ipairs(sorted) do deviations[#deviations+1]=math.abs(v-value) end;table.sort(deviations)
    return value,median(deviations)
end
function R.new(w,engine,core,self,storage,message)
    local saved={};for _,key in ipairs{'countIn','countClick','metronome','clickVolume','latency','channel'} do saved[key]=storage:get(key) end
    return setmetatable({w=w,engine=engine,core=core,self=self,storage=storage,message=message,settings=R.options(saved),keys={},counts={},pedal={},taps={},inputStatus='Disconnected'},R)
end
function R:set(key,value)
    if self.w.recording then self.message('Keep or cancel the take before changing recording settings.');return false end
    local candidate={};for k,v in pairs(self.settings) do candidate[k]=v end;candidate[key]=value;candidate=R.options(candidate)
    if candidate[key]~=value then return false end
    self:releaseInput();self.settings=candidate;self.storage:set(key,value);self:stopClock();self.w.revision=self.w.revision+1;return true
end
function R:stopClock() if self.clock then self.clock:close();self.clock=nil end;self.clockKind=nil;self.calibrating=false end
function R:clockFor(plan,delay,offset)
    self:stopClock();assert(self.engine.createMonitor,'Recording clock requires native synth API 15.')
    local a=self.engine.createMonitor(self.self);self.clock=a;self.clockLength=plan.length/48000
    if #plan.bytes>0 then
        assert(a:configure(1,schema.state{volume=.55,wave1=0,attack=.002,decay=.018,sustain=0,release=.01,cutoff=16000,polyphony=2},self.settings.clickVolume))
    end
    assert(a:effects({},1) and a:loadScore(plan.bytes,plan.length,plan.loop) and a:start(delay or .15,offset or 0),'Local recording clock could not start.')
    return a
end
function R:start(r)
    self:releaseInput();self:stopClock()
    if not self.engine.createMonitor then return true end -- older test/legacy callback hosts retain visual count-in
    if r.overdub then return true end
    local count={duration=r.countDuration,beats=r.countBeats,bars=r.options.countIn,tempo=r.countTempo,denominator=r.countDenominator}
    local ok,err=pcall(function()
        assert(count.duration<1800,'Count-in exceeds the native 30-minute clock limit.')
        r.limit=math.min(r.limit,self.w:beatAt(r.seconds+1800-count.duration,r.mode)*4)
        local a=self:clockFor(R.plan(self.w,r.mode,r.startStep/4,r.limit/4,false,count,r.options.metronome,r.options.countClick))
        self.clockKind='take';r.starts=r.starts+.15
        r.clock=function() if not self.clock or not a.playing then return end;return a.audiblePosition-count.duration end
    end)
    if not ok then self:stopClock();self.message(tostring(err));return false end;return true
end
function R:ended() self:releaseInput();if self.clockKind=='take' then self:stopClock() end end
function R:devices() return self.engine.midiDevices and self.engine.midiDevices() or {} end
function R:connect(id)
    self:disconnect();local ok,input=pcall(self.engine.openMidiInput or function() error('Hardware MIDI requires native API 15 on Windows.') end,id)
    if not ok then self.message(tostring(input));return false end
    self.input=input;self.inputStatus='Connected';self.w.revision=self.w.revision+1;return true
end
function R:releaseInput()
    for note in pairs(self.counts) do
        if self.w.recording and self.w.recording.held[note] then self.w:recordOff(note)
        elseif self.inputTrack then self.w.cb.off(self.inputTrack,note) end
    end
    self.keys={};self.counts={};self.pedal={};self.inputTrack=nil
end
function R:disconnect() self:releaseInput();if self.input then self.input:close();self.input=nil end;self.inputStatus='Disconnected' end
function R:release(key,age)
    local entry=self.keys[key];if not entry then return end;self.keys[key]=nil
    local n=entry.note;self.counts[n]=self.counts[n]-1
    if self.counts[n]==0 then
        self.counts[n]=nil
        if entry.take and self.w.recording==entry.take then self.w:recordOff(n,nil,age)
        elseif not entry.take and self.inputTrack then self.w.cb.off(self.inputTrack,n) end
    end
end
function R:target()
    if self.w.recording then return self.w.recording.index end
    if self.monitorTrack~=nil then return self.monitorTrack end
    return self.w.selected
end
function R:event(e)
    if not finite(e.age,0,2) or not finite(e.channel,1,16) or e.channel%1~=0 or not finite(e.a,0,127) or not finite(e.b,0,127) then return false end
    if self.settings.channel~=0 and self.settings.channel~=e.channel then return true end
    local key=e.channel*128+e.a;local entry=self.keys[key]
    if e.status==144 and e.b>0 then
        if self.calibrating then self:tap(e.age);return true end
        if entry then return true end
        local n=e.a;local take=self.w.recording;local target=self:target()
        if target~=0 and not self.w.project.tracks[target] then return true end
        if not self.counts[n] then
            local ok
            if take then ok=self.w:recordOn(n,e.b/127,e.age)
            else if target>0 then self.w:configure(target) end;ok=self.w.cb.on(target,n,e.b/127)~=false end
            if not ok then return true end
        end
        self.inputTrack=target
        self.counts[n]=(self.counts[n] or 0)+1;self.keys[key]={note=n,channel=e.channel,take=take,down=true}
    elseif e.status==128 or e.status==144 and e.b==0 then
        if entry then entry.down=false;if not self.pedal[e.channel] then self:release(key,e.age) end end
    elseif e.status==176 and (e.a==64 or e.a==120 or e.a==123) then
        self.pedal[e.channel]=e.a==64 and e.b>=64
        local release={};for id,k in pairs(self.keys) do if k.channel==e.channel and (e.a~=64 or not k.down and not self.pedal[e.channel]) then release[#release+1]=id end end
        for _,id in ipairs(release) do self:release(id,e.age) end
    end
    return true
end
function R:beginCalibration()
    if self.w.recording then self.message('Keep or cancel the take before calibration.');return false end
    self.w:stop(true);self:releaseInput();self.taps={};self.lastTap=nil
    local temp={project={bpm=120}};local ok,err=pcall(function() self:clockFor(R.plan(temp,'clips',0,4,true,nil,true));self.calibrating=true;self.clockKind='calibration' end)
    if not ok then self:stopClock();self.message(tostring(err));return false end;self.w.revision=self.w.revision+1;return true
end
function R:tap(age)
    if not self.calibrating or not self.clock or not self.clock.playing then return false end
    local time=self.clock.audiblePosition-(age or 0);local beat=math.floor(time/.5+.5)
    if beat<2 or beat==self.lastTap then return false end
    local error=(time-beat*.5)*1000;if math.abs(error)>200 then return false end
    self.lastTap=beat;self.taps[#self.taps+1]=error;if #self.taps>32 then table.remove(self.taps,1) end
    self.w.revision=self.w.revision+1;return true
end
function R:applyCalibration()
    local value,jitter=R.calculation(self.taps)
    if not value or jitter>40 then self.message('Tap at least 8 times steadily; timing spread must be under 40 ms.');return false end
    return self:set('latency',math.floor(value+.5))
end
function R:frame(visible,track)
    local w=self.w
    self.monitorTrack=visible and track or nil
    if not visible then
        if self.input or next(self.keys) then self:disconnect() end
        if self.calibrating then self:stopClock() end
    end
    if self.clockKind=='take' and self.clock and w.recording then
        if self.clock.closed or (self.clock.fault or 0)~=0 or not self.clock.playing and self.core.getRealTime()>w.recording.starts+.75 then
            local complete=(self.clock.position or 0)>=(self.clockLength or math.huge)
            w:endTake(false,complete and w.recording.limit or nil);self:stopClock()
            if not complete then self.message('Recording clock stopped; partial take kept.') end
        end
    end
    if self.inputTrack and self.inputTrack~=self:target() then self:releaseInput() end
    if self.input then
        local input=self.input;local ok,batch=pcall(input.poll,input)
        if not ok or not batch.connected or batch.dropped>0 then
            if w.recording then w:endTake(false) end;self:disconnect();self.message('MIDI disconnected or input overflowed; held notes released and partial take kept.');w.revision=w.revision+1
        else for _,e in ipairs(batch.events) do
            if self.input~=input then break end
            if visible then if not self:event(e) then self:releaseInput() end end
        end end
    end
    if not self.calibrating and self.clockKind~='take' then
        local playing,time=false,0;if w.cb.progress then playing,time=w.cb.progress() end
        if self.settings.metronome and w.playing and w.native and not w.awaiting and playing then
            if not self.clock then
                local mode=w.mode=='song' and 'song' or 'clips';local first,last=Format.songRange(w.project)
                if mode~='song' or not first then first=0;last=mode=='song' and (w.project.timeline or w.song).beats or (w:clipSource() and w:clipSource().length or w.project.length)/4 end
                local loop=w.mode=='pattern' or mode=='song' and w.project.composition and w.project.composition.loop or mode~='song' and w.project.loop
                local ok,err=pcall(function() local plan=R.plan(w,mode,first,last,loop,nil,true);local offset=(time or 0)+.15;if loop then offset=offset%(plan.length/48000) end;self:clockFor(plan,.15,offset);self.clockKind='play' end)
                if not ok then self:stopClock();self.settings.metronome=false;self.message(tostring(err)) end
            end
        elseif self.clockKind=='play' then self:stopClock() end
    end
    if self.clock then self.clock:keepAlive() end
end
function R:close() self:disconnect();self:stopClock() end
return R
