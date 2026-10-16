-- lua scripts/test_procedural_synth.lua (repository root)
package.path='apps/openmw-server/scripts/?.lua;'..package.path
local schema=require('procedural_synth_schema')
local now,sent,players,objects,storage,handler,module
local checks,tests=0,0
local function check(b,m) checks=checks+1; assert(b,m) end
local function copy(t)
    if type(t)~='table' then return t end
    local r={}; for k,v in pairs(t) do r[k]=copy(v) end; return r
end
local function reset(preserve)
    now,sent=100,{}
    players={ [1]={guid=1,cell='INT:A',position={x=0,y=0,z=0}},
        [2]={guid=2,cell='INT:A',position={x=0,y=0,z=0}},
        [3]={guid=3,cell='INT:B',position={x=0,y=0,z=0}} }
    objects={[7]={mpNum=7,refId='fetcher_procedural_synth_v1',cell='INT:A',position={x=0,y=0,z=0}}}
    storage=preserve or {}
    package.loaded.config={PROCEDURAL_SYNTH_ENABLED=true}
    package.loaded.intent_policy={registerHandler=function(_,_,fn) handler=fn end}
    package.loaded.mp={
        getUptime=function() return now end,getPlayer=function(id) return players[id] end,
        getPlayers=function() local t={}; for _,p in pairs(players) do t[#t+1]=p end; return t end,
        getObjectByMpNum=function(id) return objects[id] end,
        send=function(guid,event,data) sent[#sent+1]={guid=guid,event=event,data=copy(data)} end,
        getCharacterStorageForCharacter=function(id,namespace,key) check(namespace=='ProceduralSynth'); return copy(storage[id]) end,
        setCharacterStorageForCharacter=function(id,namespace,key,data) storage[id]=copy(data); return true end,
        upsertDynamicRecord=function() return true end,placeObject=function() return true end,log=function() end,
    }
    module=dofile('apps/openmw-server/scripts/procedural_synth.lua')
end
local function acquire(id)
    handler{object={recordId='fetcher_procedural_synth_v1',mpNum=7},player=players[id or 1]}
end
local sequence=0
local function request(op,extra)
    sequence=sequence+1
    local d={version=1,pid=1,characterId=11,id=7,epoch=1,seq=sequence,op=op}
    for k,v in pairs(extra or {}) do d[k]=v end
    module.eventHandlers.Synth_Request(d)
end
local function test(name,fn) sequence=0; reset(); fn(); tests=tests+1; print('PASS '..name) end
test('schema defaults, validation, bounded payload and migration',function()
    local defaults=schema.state({}); check(defaults.polyphony==16)
    check(schema.state({version=2})==nil); check(schema.state({gain=0/0})==nil)
    check(schema.state({cutoff=math.huge})==nil); check(schema.state({wave1=0.5})==nil)
    check(schema.state({unknown=1})==nil); check(schema.state({attack={}})==nil)
    check(#schema.builtins==5)
    for _,p in ipairs(schema.builtins) do check(schema.state(p.state)~=nil) end
    local f=assert(io.open('files/server/server-lua-packages/fetcher.procedural-synth/shared/schema.lua','rb'))
    local client=f:read('*a'); f:close()
    local g=assert(io.open('apps/openmw-server/scripts/procedural_synth_schema.lua','rb'))
    check(client==g:read('*a'),'server/client schema drift'); g:close()
end)
test('exclusive activator session, verified object, range and cell',function()
    acquire(); check(#sent==2); check(sent[1].data.params~=nil)
    sent={}; acquire(2); check(#sent==1 and sent[1].data.op=='error')
    request('on',{pid=2,note=60,velocity=1}); check(#sent==1)
    request('on',{epoch=9,note=60,velocity=1}); check(#sent==1)
    request('on',{id=8,note=60,velocity=1}); check(#sent==1)
    players[1].position.x=301; request('heartbeat'); check(sent[#sent].data.op=='close')
end)
test('authenticated note relay, sequence and 3D cell eligibility',function()
    acquire(); sent={}
    request('on',{note=60,velocity=0.8,sourceGuid=3})
    check(#sent==1 and sent[1].guid==2 and sent[1].data.op=='on')
    check(sent[1].data.pid==nil and sent[1].data.sourceGuid==nil)
    local old=sequence
    request('on',{seq=old,note=62,velocity=1}); check(#sent==1)
    request('off',{note=60}); check(#sent==2 and sent[2].data.op=='off')
end)
test('malformed and oversized events are ignored',function()
    acquire(); sent={}
    for _,e in ipairs({{note=-1},{note=128},{velocity=0/0},{velocity=2},{version=2},
        {params={cutoff=math.huge}},{foo=string.rep('a',97)},{foo={}},{pid=99}}) do
        local d={note=60,velocity=1}; for k,v in pairs(e) do d[k]=v end
        request('on',d)
    end
    check(#sent==0)
end)
test('rate limits bound notes and edits; safety release still arrives',function()
    acquire(); sent={}
    for i=1,1000 do request('on',{note=i%128,velocity=1}) end
    check(#sent<=82,'unbounded note burst')
    now=now+1; request('on',{note=60,velocity=1})
    sent={}
    for i=1,1000 do request('edit',{params={cutoff=100+i}}) end
    check(#sent<=16,'unbounded edit burst')
    request('off',{note=60}); check(sent[#sent].data.op=='off')
    request('close'); check(sent[#sent].data.op=='close')
end)
test('save/load/save-as/rename/delete and persistence across reload',function()
    acquire(); request('saveAs',{name='My Tone',params={wave1=3,cutoff=1234}})
    check(storage[11].entries[1].id=='p:1')
    now=now+1; request('saveAs',{name='Second',params={wave1=1}})
    now=now+1; request('rename',{presetId='p:1',name='Renamed'})
    check(storage[11].entries[1].name=='Renamed' and #storage[11].entries==2)
    local persisted=copy(storage); reset(persisted); sequence=0; acquire(); sent={}
    request('load',{presetId='p:1'}); check(sent[#sent].data.params.cutoff==1234)
    request('save',{presetId='p:1',name='Updated',params={cutoff=4321}})
    check(storage[11].entries[1].state.cutoff==4321)
    now=now+1; request('delete',{presetId='p:2'}); check(#storage[11].entries==1)
    now=now+1; request('saveAs',{name='Third',params={}})
    check(storage[11].entries[2].id=='p:3','stable identifiers never reused')
    request('load',{presetId='p:1',pid=2,characterId=22}); check(storage[22]==nil)
end)
test('future library version preserved and builtin load works',function()
    storage[11]={version=2,entries={},nextId=1}; acquire()
    request('saveAs',{name='No',params={}}); check(storage[11].version==2)
    storage[11]=nil; request('load',{presetId='b:pad'})
    check(sent[#sent].data.params.attack==0.9)
end)
test('disconnect, timeout, cell change and activator removal release locks',function()
    for _,cause in ipairs({'disconnect','timeout','cell','remove'}) do
        reset(); sequence=0; acquire(); sent={}
        if cause=='disconnect' then module.eventHandlers.OnPlayerDisconnect{guid=1}
        elseif cause=='cell' then module.eventHandlers.OnPlayerCellChange{guid=1}
        elseif cause=='remove' then objects[7]=nil; now=now+1; module.engineHandlers.onUpdate()
        else now=now+9; module.engineHandlers.onUpdate() end
        check(#sent==2 and sent[1].data.op=='close')
        objects[7]={mpNum=7,refId='fetcher_procedural_synth_v1',cell='INT:A',position={x=0,y=0,z=0}}
        sent={}; acquire(2); check(sent[#sent].data.op=='granted')
    end
end)
test('late listener snapshot has parameters and no historical notes',function()
    acquire(); request('on',{note=60,velocity=1}); sent={}
    players[3].cell='INT:A'; now=now+2.1; module.engineHandlers.onUpdate()
    local found=false
    for _,event in ipairs(sent) do
        check(event.data.op~='on')
        if event.guid==3 and event.data.op=='state' then found=true; check(event.data.params~=nil) end
    end
    check(found)
end)
test('performer identity is derived from the authenticated owner',function()
    acquire()
    for _,event in ipairs(sent) do
        if event.data.op=='state' or event.data.op=='granted' then check(event.data.owner==1,'wrong performer GUID') end
    end
    sent={};request('on',{note=60,velocity=0.8,owner=3})
    check(#sent>0)
    for _,event in ipairs(sent) do check(event.data.owner==1,'payload spoofed performer GUID') end
end)
print(string.format('%d tests, %d assertions passed',tests,checks))
