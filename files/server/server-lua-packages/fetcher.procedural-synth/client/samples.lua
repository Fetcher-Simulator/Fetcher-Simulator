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
    self.loop=nil;self.reverse=nil;self.gain=nil;self.waveform=nil;self.waveKey=nil;self.expectedHash=nil
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
    self.loop=nil;self.waveform=nil;self.waveKey=nil;return true
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
    self:stop();local ref=self:reference();if not ref then return false end
    self.preview=self.synth.create(self.source)
    self.preview:set('attack',0.002);self.preview:set('sustain',1);self.preview:set('release',0.08)
    self.preview:set('cutoff',18000);self.preview:set('resonance',0)
    self.preview:sample(ref);self.preview:noteOn(ref.root,0.85);return true
end
function M:release() if self.preview then self.preview:noteOff(self.root);return true end;return false end
function M:stop() if self.preview then self.preview:close();self.preview=nil end end
return M
