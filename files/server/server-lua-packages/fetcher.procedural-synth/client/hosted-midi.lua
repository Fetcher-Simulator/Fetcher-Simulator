-- Connection-scoped, bounded memory cache. Selected songs arrive through the
-- game connection; local files and Studio exports remain available alongside it.
local M={}
local function integer(n,lo,hi) return type(n)=='number' and n==n and n%1==0 and n>=lo and n<=hi end
local function validHash(s) return type(s)=='string' and #s==64 and s:match('^%x+$') end
local function validPath(s)
    return type(s)=='string' and #s<=192 and s:sub(1,5)=='midi/' and not s:find('%c')
        and not s:find('..',1,true) and not s:find('\\',1,true) and not s:find(':',1,true)
        and s:lower():match('%.mid[i]?$')
end
function M.new(cb)
    local self={catalog={},cache={},cacheBytes=0,serial=0}
    function self:token() self.serial=self.serial%2147483647+1;return self.serial end
    function self:bind(id,epoch)
        self.scope=id and {id=id,epoch=epoch} or nil;self.pending=nil;self.listing=nil
    end
    function self:clear()
        self.scope=nil;self.pending=nil;self.listing=nil;self.catalog={};self.cache={};self.cacheBytes=0
    end
    function self:list()
        if not self.scope then return end
        local requestId=self:token()
        self.listing={requestId=requestId,expires=cb.now()+30}
        cb.send('midiCatalog',{requestId=requestId})
    end
    function self:cancel()
        if self.pending then
            cb.send('midiCancel',{})
            self.pending=nil
        end
    end
    function self:has(path) return self.catalog[path]~=nil end
    function self:load(path)
        local entry=self.catalog[path]
        assert(entry,'Server MIDI is no longer listed.')
        local cached=self.cache[path]
        if cached and cached.hash==entry.hash then cached.at=cb.now();return cached.bytes end
        self:cancel()
        local job={requestId=self:token(),path=path,entry=entry,expires=cb.now()+30}
        self.pending=job
        cb.send('midiFile',{requestId=job.requestId,path=path})
        while not job.done and not job.error do coroutine.yield() end
        if self.pending==job then self.pending=nil end
        assert(not job.error,job.error)
        return job.bytes
    end
    function self:fail(job,message)
        job.error=message;job.chunks=nil
        if job==self.pending then cb.send('midiCancel',{}) end
    end
    function self:frame()
        local now=cb.now()
        if self.pending and not self.pending.done and not self.pending.error and now>self.pending.expires then
            self:fail(self.pending,'Server MIDI download timed out. Try again.')
        end
        if self.listing and now>self.listing.expires then self.listing=nil end
    end
    function self:receive(data)
        if type(data.op)~='string' or data.op:sub(1,4)~='midi' then return false end
        if not self.scope or data.id~=self.scope.id or data.epoch~=self.scope.epoch then return true end
        local list=self.listing
        if list and data.requestId==list.requestId then
            if data.op=='midiCatalogBegin' and integer(data.count,0,1024) then
                list.count=data.count;list.items={};list.offset=0
            elseif data.op=='midiCatalogPage' and list.items and data.offset==list.offset
                and type(data.entries)=='table' and #data.entries>=1 and #data.entries<=32 then
                for _,v in ipairs(data.entries) do
                    if type(v)~='table' or not validPath(v.path) or not integer(v.size,4,512*1024)
                        or not validHash(v.hash) or list.items[v.path] or list.offset>=list.count then
                        self.listing=nil;return true
                    end
                    list.items[v.path]={size=v.size,hash=v.hash};list.offset=list.offset+1
                end
            elseif data.op=='midiCatalogEnd' and list.items and list.offset==list.count then
                self.catalog=list.items;self.listing=nil
                -- Evict revisions removed or replaced by this catalogue.
                for path,v in pairs(self.cache) do
                    if not self.catalog[path] or self.catalog[path].hash~=v.hash then
                        self.cacheBytes=self.cacheBytes-#v.bytes;self.cache[path]=nil
                    end
                end
                cb.changed()
            else self.listing=nil end
            return true
        end
        local job=self.pending
        if not job or data.requestId~=job.requestId or job.done or job.error then return true end
        if data.op=='midiError' then self:fail(job,tostring(data.message or 'Server MIDI is unavailable.'))
        elseif data.op=='midiFileBegin' and not job.chunks and data.path==job.path
            and data.size==job.entry.size and data.hash==job.entry.hash then job.chunks={};job.offset=0
        elseif data.op=='midiFileChunk' and job.chunks and data.offset==job.offset
            and type(data.bytes)=='string' and #data.bytes>=1 and #data.bytes<=16384
            and job.offset+#data.bytes<=job.entry.size then
            job.chunks[#job.chunks+1]=data.bytes;job.offset=job.offset+#data.bytes
        elseif data.op=='midiFileEnd' and job.chunks and job.offset==job.entry.size then
            job.bytes=table.concat(job.chunks);job.chunks=nil;job.done=true
            while self.cacheBytes+#job.bytes>8*1024*1024 do
                local oldest
                for path,v in pairs(self.cache) do if not oldest or v.at<self.cache[oldest].at then oldest=path end end
                if not oldest then break end
                self.cacheBytes=self.cacheBytes-#self.cache[oldest].bytes;self.cache[oldest]=nil
            end
            self.cache[job.path]={hash=job.entry.hash,bytes=job.bytes,at=cb.now()};self.cacheBytes=self.cacheBytes+#job.bytes
        else self:fail(job,'Invalid or incomplete server MIDI transfer. Try again.') end
        return true
    end
    return self
end
return M
