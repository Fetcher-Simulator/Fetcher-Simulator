-- Local installed-asset discovery and preview. No speech commands, decoded PCM,
-- or game files cross the network; part parameters carry only a reference.
local schema=require('scripts.multiplayer.fetcher.procedural-synth.shared.schema')
local metadata=require('scripts.multiplayer.fetcher.procedural-synth.client.speech-catalog')
local M={};M.__index=M
local catalog,scan,scanError,texts
local function decorate(entry)
    if entry.category~='Voices' then return entry end
    local text,source,alternatives=texts:lookup(entry.id)
    entry.text=text;entry.textSource=source
    entry.label=entry.id:match('([^/]+)$')..(text~='' and '  /  '..text:sub(1,150) or '')
    entry.search=entry.id..' '..(metadata[entry.id] or '')..' '..text..' '..alternatives
    return entry
end
function M.category(path)
    return path:sub(1,6)=='music/' and 'Music' or (path:sub(1,9)=='sound/vo/' and 'Voices' or 'Effects')
end
function M.status(synth,reference)
    if not reference then return {status='Synth'} end
    if (reference.reverse or reference.loop) and (synth.version or 0)<38 then
        return {status='Unavailable',error='Sample editing requires the updated Studio client (API 38).'}
    end
    local info=synth.sampleInfo(reference.path,reference.offset,reference.duration)
    if info.status=='Ready' and reference.hash and info.hash~=reference.hash then
        return {status='Mismatch',error='Installed file differs from the saved sample'}
    end
    if info.status=='Ready' and reference.loop and reference.loop.finish>info.duration+1e-6 then
        return {status='Unavailable',error='Loop extends past the decoded excerpt. Shorten its end point.'}
    end
    local pending=info
    for _,attack in ipairs(schema.attackOrder) do if reference.attacks and reference.attacks[attack] then
        local status=M.status(synth,reference.attacks[attack])
        if status.status=='Mismatch' or status.status=='Unavailable' then return {status=status.status,error=attack..' attack: '..(status.error or status.status)} end
        if status.status~='Ready' then pending=status end
    end end
    if info.status=='Ready' then return pending end
    return info
end
function M.new(synth,vfs,source,dialogue)
    if not texts then
        local VoiceText=require('scripts.multiplayer.fetcher.procedural-synth.client.voice-text')
        texts=VoiceText.new(dialogue,require('scripts.multiplayer.fetcher.procedural-synth.shared.voice-transcripts'))
    end
    return setmetatable({synth=synth,vfs=vfs,source=source,category='Voices',root=60,oneShot=true,offset=0,duration=30},M)
end
function M:start()
    if catalog then return end
    catalog={}
    scan=coroutine.create(function()
        local count=0
        for _,prefix in ipairs{'sound/','music/'} do for path in self.vfs.pathsWithPrefix(prefix) do
            if path:match('%.mp3$') or path:match('%.wav$') or path:match('%.ogg$') or path:match('%.flac$') or path:match('%.opus$') then
                catalog[#catalog+1]=decorate{id=path,label=path,category=M.category(path),search=path..' '..(metadata[path] or '')}
                if #catalog>=100000 then scanError='Showing the first 100000 installed sounds';return end
            end
            count=count+1;if count%300==0 then coroutine.yield() end
        end end
        table.sort(catalog,function(a,b) return a.id<b.id end)
    end)
end
function M:frame()
    texts:frame()
    if scan and coroutine.status(scan)~='dead' then
        local ok,err=coroutine.resume(scan);if not ok then scanError=tostring(err);scan=nil end
    end
    if self.preview then self.preview:keepAlive() end
end
function M:entries()
    local out={};for _,entry in ipairs(catalog or {}) do
        if self.category=='All' or self.category==entry.category then out[#out+1]=decorate(entry) end
    end;return out
end
function M:scanStatus()
    return scanError or ((scan and coroutine.status(scan)~='dead' and 'Scanning: ' or 'Installed: ')..#(catalog or {})..' sounds')
end
function M:select(path)
    self:stop();self.path=path;self.offset=0;self.duration=30;self.root=60;self.oneShot=true
    self.loop=nil;self.reverse=nil;self.gain=nil;self.waveform=nil;self.waveKey=nil;self.expectedHash=nil;self.editView=nil
end
function M:loadReference(ref)
    ref=schema.sample(ref);if not ref or schema.isBank(ref) then return false end
    self:select(ref.path)
    for k,value in pairs(ref) do self[k]=value end
    self.expectedHash=ref.hash;return true
end
function M:trim(first,last)
    if type(first)~='number' or type(last)~='number' or first~=first or last~=last or first<0 or first>600 or last-first<.01 or last-first>30 then return false end
    self:stop();self.offset=math.floor(first*1000+.5)/1000;self.duration=math.floor((last-first)*1000+.5)/1000
    self.loop=nil;self.waveform=nil;self.waveKey=nil;self.editView=nil;return true
end
function M:setLoop(first,last,fade)
    local info=self:info();if info.status~='Ready' then return false end
    local reference={path=self.path,offset=self.offset,duration=self.duration,root=self.root,oneShot=false,hash=info.hash,
        reverse=self.reverse,loop={start=first,finish=last,crossfade=fade}}
    local ref=schema.sample(reference)
    if not ref or ref.loop.finish>info.duration+1e-6 then return false end
    self:stop();self.oneShot=false;self.loop=ref.loop;return true
end
function M:toggleLoop()
    if self.loop then self:stop();self.loop=nil;return true end
    local info=self:info();if info.status~='Ready' then return false end
    local length=math.floor(info.duration*1000)/1000
    if length<.02 then return false end
    return self:setLoop(0,length,math.min(.02,math.floor(length*250)/1000))
end
function M:wave()
    if not self.path or not self.synth.sampleWaveform then return nil end
    local key=table.concat({self.path,self.offset,self.duration},'|')
    if key~=self.waveKey then
        local result=self.synth.sampleWaveform(self.path,self.offset,self.duration)
        if result.minimum then self.waveform=result;self.waveKey=key end
    end
    return self.waveform
end
function M:slices(points)
    local ref=self:reference();local info=self:info()
    if not ref or type(points)~='table' or #points<1 or #points>15 then return nil end
    local length=math.floor(math.min(info.duration,ref.duration)*1000+1e-6)
    local edges={0};for _,point in ipairs(points) do
        if type(point)~='number' or point~=point then return nil end
        local ms=math.floor(point*1000+.5)
        if ms-edges[#edges]<10 or ms>length-10 then return nil end
        edges[#edges+1]=ms
    end
    edges[#edges+1]=length
    local out={}
    for n=1,#edges-1 do
        local first,last=edges[n],edges[n+1]
        out[n]=schema.sample{path=ref.path,offset=ref.offset+(ref.reverse and length-last or first)/1000,
            duration=(last-first)/1000,root=ref.root,oneShot=true,hash=ref.hash,gain=ref.gain,reverse=ref.reverse}
        if not out[n] then return nil,'Every slice must start within the first ten minutes of the file.' end
    end
    return out
end
function M:chops(count)
    if type(count)~='number' or count%1~=0 or count<2 or count>16 then return nil end
    local info=self:info();if info.status~='Ready' then return nil end
    local length=math.floor(math.min(info.duration,self.duration)*1000+1e-6)
    local points={};for n=1,count-1 do points[n]=math.floor(n*length/count)/1000 end
    return self:slices(points)
end
function M:detect(sensitivity,spacing,snap)
    if not self.synth.sampleTransients then return nil,'Automatic slicing requires the updated Studio client (API 40).' end
    if type(sensitivity)~='number' or sensitivity~=sensitivity or sensitivity<0 or sensitivity>1
        or type(spacing)~='number' or spacing~=spacing or spacing<.01 or spacing>2 then return nil,'Choose sensitivity 0-100% and minimum gap 10-2000ms.' end
    local info=self:info();if info.status~='Ready' then return nil,'Wait until the sample is Ready.' end
    local result=self.synth.sampleTransients(self.path,self.offset,self.duration,sensitivity,spacing,self.reverse or false,snap==true)
    if not result.points or result.hash~=info.hash then return nil,'The sample cannot be analysed as a raw excerpt.' end
    local points={};local previous=0;local length=math.floor(info.duration*1000)/1000
    if #result.points>15 then return nil,'Invalid slice analysis.' end
    for _,point in ipairs(result.points) do
        if type(point)~='number' or point~=point or point-previous<.01-1e-7 or point>length-.01+1e-7 then return nil,'Invalid slice analysis.' end
        points[#points+1]=point;previous=point
    end
    return {points=points,found=result.found or #points,truncated=result.truncated==true}
end
function M:waveRange(first,last)
    if not self.path or not self.synth.sampleWaveformRange then return nil end
    local key=table.concat({self.path,self.offset,self.duration,first,last,tostring(self.reverse)},'|')
    if key~=self.rangeKey then
        local wave=self.synth.sampleWaveformRange(self.path,self.offset,self.duration,first,last,self.reverse or false)
        if not wave.minimum then return nil end
        self.rangeKey=key;self.rangeWave=wave
    end
    return self.rangeWave
end
function M:snapPoint(point,enabled)
    local info=self:info();local length=info.duration or 0
    if enabled and self.synth.sampleZeroCrossing then
        point=self.synth.sampleZeroCrossing(self.path,self.offset,self.duration,point,self.reverse or false) or point
    end
    return math.max(0,math.min(math.floor(length*1000)/1000,math.floor(point*1000+.5)/1000))
end
function M:trimView(first,last)
    local info=self:info();if info.status~='Ready' or first<0 or last>info.duration+1e-6 or last-first<.01 then return false end
    local offset=self.offset+(self.reverse and info.duration-last or first)
    return self:trim(offset,offset+last-first)
end
function M:info()
    if not self.path then return {status='Choose a sound'} end
    local info=self.synth.sampleInfo(self.path,self.offset,self.duration)
    if info.status=='Ready' and self.expectedHash and info.hash~=self.expectedHash then
        return {status='Mismatch',error='Installed file differs from the saved sample'}
    end
    return info
end
function M:description()
    if not self.path then return '' end
    local text,source=texts:lookup(self.path)
    return text~='' and (text..'  ['..source..']') or metadata[self.path] or M.category(self.path)
end
function M:reference()
    local info=self:info();if info.status~='Ready' then return nil end
    local ref=schema.sample{path=self.path,offset=self.offset,duration=self.duration,root=self.root,oneShot=self.oneShot,hash=info.hash,
        gain=self.gain,reverse=self.reverse,loop=self.loop}
    if ref and M.status(self.synth,ref).status=='Ready' then return ref end
end
function M:audition()
    return self:auditionReference(self:reference())
end
function M:auditionReference(ref)
    self:stop();if not ref or M.status(self.synth,ref).status~='Ready' then return false end
    self.preview=self.synth.create(self.source)
    self.preview:set('attack',0.002);self.preview:set('sustain',1);self.preview:set('release',0.08)
    self.preview:set('cutoff',18000);self.preview:set('resonance',0)
    self.preview:sample(ref);self.preview:noteOn(ref.root,0.85);return true
end
function M:release() if self.preview then self.preview:noteOff(self.root);return true end;return false end
function M:stop() if self.preview then self.preview:close();self.preview=nil end end
return M
