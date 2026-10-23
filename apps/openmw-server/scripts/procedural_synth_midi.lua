-- Studio's server library uses the same low-priority, round-robin delivery
-- model as Bardcraft. Only catalogued files may be requested; no client paths
-- reach the filesystem. Cache and transfers stay bounded independently of joins.
return function(mp, emit)
    local M = {}
    local CHUNK, PAGE, CACHE = 16384, 32, 8*1024*1024
    local catalog, byPath, catalogAt = {}, {}, -math.huge
    local queue, jobs, cache, cacheBytes, nextSend = {}, {}, {}, 0, 0
    local function integer(n,lo,hi) return type(n)=='number' and n==n and n%1==0 and n>=lo and n<=hi end
    local function hash(s) return type(s)=='string' and #s==64 and s:match('^%x+$') end
    local function refresh()
        local now=mp.getUptime()
        if now-catalogAt<60 then return end
        catalogAt=now;catalog={};byPath={};cache={};cacheBytes=0
        local entries=type(mp.listStudioMidiFiles)=='function' and mp.listStudioMidiFiles() or {}
        for _,entry in ipairs(entries) do
            local name=entry.name
            if type(name)=='string' and #name>0 and #name<=187 and not name:find('%c')
                and integer(entry.size,4,512*1024) and hash(entry.hash) then
                local path='midi/'..name:lower()
                if not byPath[path] then
                    local item={path=path,name=name,size=entry.size,hash=entry.hash}
                    byPath[path]=item;catalog[#catalog+1]=item
                    if #catalog==1024 then break end
                end
            end
        end
        table.sort(catalog,function(a,b) return a.path<b.path end)
        if mp.log then mp.log(string.format('[synth-midi] Hosted MIDI catalog: %d songs.',#catalog)) end
    end
    M.prepare=refresh
    function M.cancel(guid)
        jobs[guid]=nil
        for n=#queue,1,-1 do if queue[n]==guid then table.remove(queue,n) end end
    end
    local function send(job,op,fields)
        local out={op=op,id=job.id,epoch=job.epoch,requestId=job.requestId}
        for k,v in pairs(fields or {}) do out[k]=v end
        emit(job.guid,out)
    end
    function M.request(guid,data)
        if not integer(data.requestId,1,2147483647) then return end
        refresh();M.cancel(guid)
        local job={guid=guid,id=data.id,epoch=data.epoch,requestId=data.requestId,at=0,expires=mp.getUptime()+30}
        if data.op=='midiCatalog' then
            job.catalog=catalog
        else
            local entry=byPath[data.path or '']
            if not entry then send(job,'midiError',{message='This server MIDI is no longer available.'});return end
            local bytes=cache[entry.path]
            if not bytes and type(mp.readStudioMidiFile)=='function' then
                bytes=mp.readStudioMidiFile(entry.name,entry.hash)
            end
            if type(bytes)~='string' or #bytes~=entry.size then
                catalogAt=-math.huge
                send(job,'midiError',{message='Server MIDI changed or could not be read. Reopen Studio to refresh the song list.'});return
            end
            if not cache[entry.path] then
                if cacheBytes+#bytes>CACHE then cache={};cacheBytes=0 end
                cache[entry.path]=bytes;cacheBytes=cacheBytes+#bytes
            end
            job.entry=entry;job.bytes=bytes
        end
        jobs[guid]=job;queue[#queue+1]=guid
    end
    function M.tick()
        local now=mp.getUptime()
        if now<nextSend or #queue==0 then return end
        nextSend=now+.025
        local guid=table.remove(queue,1);local job=jobs[guid]
        if not job then return end
        if now>job.expires or not mp.getPlayer(guid) then jobs[guid]=nil;return end
        if job.catalog then
            if job.at==0 then send(job,'midiCatalogBegin',{count=#job.catalog});job.at=1
            elseif job.at<=#job.catalog then
                local entries={}
                for n=job.at,math.min(#job.catalog,job.at+PAGE-1) do
                    local v=job.catalog[n];entries[#entries+1]={path=v.path,size=v.size,hash=v.hash}
                end
                send(job,'midiCatalogPage',{offset=job.at-1,entries=entries});job.at=job.at+#entries
            else send(job,'midiCatalogEnd');jobs[guid]=nil;return end
        elseif job.at==0 then
            send(job,'midiFileBegin',{path=job.entry.path,size=job.entry.size,hash=job.entry.hash});job.at=1
        elseif job.at<=#job.bytes then
            local bytes=job.bytes:sub(job.at,job.at+CHUNK-1)
            send(job,'midiFileChunk',{offset=job.at-1,bytes=bytes});job.at=job.at+#bytes
        else send(job,'midiFileEnd');jobs[guid]=nil;return end
        queue[#queue+1]=guid
    end
    return M
end
