local Server=dofile('apps/openmw-server/scripts/procedural_synth_midi.lua')
local Client=dofile('files/server/server-lua-packages/fetcher.procedural-synth/client/hosted-midi.lua')
local checks=0
local function check(b,m) checks=checks+1;assert(b,m) end
local now,reads,scans,events,changed=0,0,0,{},0
local entries,bytes,clients={},{},{}
local function add(name,data,hash)
    local entry={name=name,size=#data,hash=hash or string.rep('a',64)}
    entries[#entries+1]=entry;bytes[name]=data;return entry
end
for n=1,600 do add(string.format('Bardcraft/custom/%04d.mid',n),'MThd'..string.rep(string.char(n%127),n==1 and 40000 or 10)) end
add('unsupported..name.mid','MThdDATA') -- saved-project paths reject consecutive dots
local native={getUptime=function() return now end,getPlayer=function(id) return clients[id] end,
    listStudioMidiFiles=function() scans=scans+1;return entries end,
    readStudioMidiFile=function(name,hash)
        reads=reads+1;return bytes[name]
    end}
local server=Server(native,function(id,data)
    events[#events+1]={id=id,data=data};data.version=1
    if clients[id] then clients[id]:receive(data) end
end)
local function client(id)
    local c=Client.new{now=function() return now end,changed=function() changed=changed+1 end,
        send=function(op,data)
            data.op=op;data.id=id+6;data.epoch=id
            if op=='midiCancel' then server.cancel(id) else server.request(id,data) end
        end}
    clients[id]=c;c:bind(id+6,id);return c
end
local a,b=client(1),client(2)
local function ticks(n)
    for _=1,n do now=now+.05;local before=#events;server.tick();check(#events-before<=1,'one send per tick globally');for _,c in pairs(clients) do c:frame() end end
end
a:list();b:list();ticks(50)
local count=0;for _ in pairs(a.catalog) do count=count+1 end
check(count==600 and changed==2 and scans==1 and reads==0,'600 songs advertised without file downloads; unsupported names cannot poison the catalogue')
check(events[1].id~=events[2].id,'receivers get fair rotation')
local path='midi/bardcraft/custom/0001.mid'
local result
local co=coroutine.create(function() result=a:load(path) end)
check(coroutine.resume(co) and coroutine.status(co)=='suspended','selection waits asynchronously')
ticks(8);check(coroutine.resume(co) and coroutine.status(co)=='dead','complete transfer resumes importer')
check(result==bytes[entries[1].name] and reads==1,'multi-chunk file exactly matches server bytes')
co=coroutine.create(function() result=a:load(path) end);check(coroutine.resume(co) and coroutine.status(co)=='dead' and reads==1,'replay uses bounded memory cache')
local current=a.cacheBytes
co=coroutine.create(function() a:load('midi/bardcraft/custom/0002.mid') end);coroutine.resume(co)
local id=a.pending.requestId
a:receive{op='midiFileBegin',id=7,epoch=999,requestId=id,path='midi/bardcraft/custom/0002.mid',size=14,hash=string.rep('a',64)}
check(not a.pending.chunks,'wrong session epoch cannot start a transfer')
ticks(1)
a:receive{op='midiFileChunk',id=7,epoch=1,requestId=id,offset=1,bytes='wrong'}
check(a.pending.error and a.cacheBytes==current,'out-of-order chunks fail without entering cache')
local ok=coroutine.resume(co);check(not ok,'failed download reports importer error')
co=coroutine.create(function() a:load('midi/bardcraft/custom/0003.mid') end);coroutine.resume(co)
local old=a.pending.requestId;a:cancel()
a:receive{op='midiFileBegin',id=7,epoch=1,requestId=old,path='midi/bardcraft/custom/0003.mid',size=14,hash=string.rep('a',64)}
check(not a.pending,'cancelled selection ignores late responses')
server.request(1,{op='midiFile',id=7,epoch=1,requestId=999,path='midi/../../secret.mid'})
check(events[#events].data.op=='midiError' and reads==3,'uncatalogued traversal never reaches native reader')
-- File changes invalidate both server and client caches when the catalogue refreshes.
entries[1].hash=string.rep('b',64);bytes[entries[1].name]='MThdNEW-DATA';entries[1].size=#bytes[entries[1].name]
now=now+61;a:list();ticks(24)
check(not a.cache[path] and a.cacheBytes==0,'new revision evicts stale cached song')
co=coroutine.create(function() result=a:load(path) end);coroutine.resume(co);ticks(5);check(coroutine.resume(co) and result=='MThdNEW-DATA','updated file replaces prior revision')
-- Failed reads and timeouts cannot masquerade as completed songs.
bytes[entries[4].name]=nil
co=coroutine.create(function() a:load('midi/bardcraft/custom/0004.mid') end)
local missingOK=coroutine.resume(co)
check(not missingOK and not a.pending,'missing native file fails explicitly')
co=coroutine.create(function() a:load('midi/bardcraft/custom/0005.mid') end);coroutine.resume(co)
now=now+31;a:frame();check(a.pending.error and not coroutine.resume(co),'timeout terminates the pending importer')
-- Cache eviction stays at 8 MiB even after repeatedly choosing new large songs.
entries={};bytes={}
for n=1,18 do add('large/'..n..'.mid','MThd'..string.rep('x',512*1024-4)) end
now=now+61;a:list();ticks(5)
for n=1,18 do
    co=coroutine.create(function() result=a:load('midi/large/'..n..'.mid') end);coroutine.resume(co)
    ticks(35);check(coroutine.resume(co),'large download completes')
    check(a.cacheBytes<=8*1024*1024,'repeated downloads cannot grow cache past budget')
end
check(not a.cache['midi/large/1.mid'] and a.cache['midi/large/18.mid'],'oldest entries evicted')
a:clear();check(not a.scope and next(a.catalog)==nil and a.cacheBytes==0,'disconnect forgets old server library and cache')
print(string.format('PASS server MIDI catalogues, fair delivery, exact bytes, stale/cancelled messages, failures, cache eviction and isolation; %d assertions',checks))
