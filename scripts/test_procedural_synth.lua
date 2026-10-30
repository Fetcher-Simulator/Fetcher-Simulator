-- lua scripts/test_procedural_synth.lua (repository root)
package.path='apps/openmw-server/scripts/?.lua;'..package.path
local schema=require('procedural_synth_schema')
package.loaded['openmw.types']={Actor={inventory=function(p) return p.inventory or {} end}}
local now,sent,players,objects,storage,handler,module
local pickups,records
local checks,tests=0,0
local function check(b,m) checks=checks+1; assert(b,m) end
local function copy(t)
    if type(t)~='table' then return t end
    local r={}; for k,v in pairs(t) do r[k]=copy(v) end; return r
end
local function reset(preserve)
    now,sent,pickups,records=100,{},{},{}
    players={ [1]={guid=1,characterId=11,health=100,cell='INT:A',position={x=0,y=0,z=0}},
        [2]={guid=2,characterId=22,health=100,cell='INT:A',position={x=0,y=0,z=0}},
        [3]={guid=3,characterId=33,health=100,cell='INT:B',position={x=0,y=0,z=0}} }
    objects={[7]={mpNum=7,refId='fetcher_procedural_synth_v1',cell='INT:A',position={x=0,y=0,z=0}}}
    storage=preserve or {}
    package.loaded.config={PROCEDURAL_SYNTH_ENABLED=true}
    package.loaded.intent_policy={registerHandler=function(_,_,fn) handler=fn end}
    package.loaded.mp={
        getUptime=function() return now end,getPlayer=function(id) return players[id] end,
        getPlayers=function() local t={}; for _,p in pairs(players) do t[#t+1]=p end; return t end,
        getObjectByMpNum=function(id) assert(type(id)=='number','native object lookup requires a numeric mpNum');return objects[id] end,
        send=function(guid,event,data) sent[#sent+1]={guid=guid,event=event,data=copy(data)} end,
        getCharacterStorageForCharacter=function(id,namespace,key) check(namespace=='ProceduralSynth'); return copy(storage[key=='presets' and id or (key..':'..id)]) end,
        setCharacterStorageForCharacter=function(id,namespace,key,data) storage[key=='presets' and id or (key..':'..id)]=copy(data); return true end,
        upsertDynamicRecord=function(kind,id,data) records[id]={kind=kind,data=copy(data)};return true end,
        pickupPlacedObject=function(guid,id,ref) pickups[#pickups+1]={guid=guid,id=id,ref=ref};return true end,
        placeObject=function() return true end,log=function() end,
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
    local legacy=schema.state({wave1=4,decay=0.12,sustain=0});check(legacy.drumType==0 and legacy.ampCurve==1,'legacy presets retain the original engine and envelope curve')
    check(not schema.state({drumType=5}) and not schema.state({noiseDecay=0}) and not schema.state({bodyFrequency=0/0}),'new drum controls validated')
    check(#schema.builtins>=40)
    check(legacy.instrumentType==0 and legacy.driveType==0 and legacy.cabinet==0 and legacy.ampBass==0,'legacy saves bypass new voices/effects')
    check(not schema.state({driveType=4}) and not schema.state({drive=37}) and not schema.state({stringPick=0}),'new string/effects controls bounded')
    local ids={};for _,p in ipairs(schema.builtins) do check(not ids[p.id],'unique built-in ID');ids[p.id]=true end
    for _,kit in ipairs(schema.kits) do for _,id in ipairs(kit.ids) do check(ids[id],'kit refers to a built-in') end end
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

test('non-playing leader roams in the same cell; pickup remains local',function()
    acquire();check(sent[#sent].data.leaderPlays==true,'legacy sessions default to playing')
    local format=require('procedural_synth_project')(schema)
    request('band',{band=format.band(),routes={},leaderPlays=false})
    check(sent[#sent].data.op=='band' and sent[#sent].data.leaderPlays==false,'mode broadcasts to observers')
    local project=format.new();project.tracks[1]=format.newTrack('piano','Conductor test',16)
    project.tracks[1].notes={{n=60,s=0,l=2,v=1}}
    local score=require('procedural_synth_sequence').compile(project,nil,nil,'clips',format)
    request('channel',{channel=1,params={},level=1})
    request('scoreBegin',{playId=1,count=#score.bytes/11,length=score.length,loop=true,quantum=score.quantum,replace=false})
    request('scoreChunk',{playId=1,index=1,bytes=score.bytes});request('scoreCommit',{playId=1})
    check(sent[#sent].data.op=='scoreReady','band playback starts')
    players[1].position.x=1500;now=now+2.1;request('heartbeat');sent={};module.engineHandlers.onUpdate()
    for _,e in ipairs(sent) do check(e.data.op~='close','conductor movement must not close playback') end
    request('edit',{params={cutoff=1234}});check(sent[#sent].data.op=='params','remote controls remain usable')
    request('pickup');check(#pickups==0 and sent[#sent].data.pickupFailed,'distant pickup rejected')
    sent={};request('sync',{pid=2});local state,running=false,false
    for _,e in ipairs(sent) do
        if e.data.op=='state' then state=e.data.leaderPlays==false end
        if e.data.op=='scoreReady' then running=true end
    end
    check(state and running,'late listener receives conductor mode and the still-playing score')
    players[3].cell='INT:A';now=now+2.1;request('heartbeat');sent={};module.engineHandlers.onUpdate()
    local snapshot=false
    for _,e in ipairs(sent) do if e.guid==3 and e.data.op=='state' then snapshot=true;check(e.data.leaderPlays==false,'new cell listener receives mode') end end
    check(snapshot,'new listener receives session snapshot')
    players[1].position.x=0;request('pickup');check(#pickups==1,'nearby pickup still works')
end)

test('leader mode validates ownership and retains lease cleanup',function()
    local format=require('procedural_synth_project')(schema)
    acquire();sent={}
    request('band',{band=format.band(),routes={},leaderPlays='false'});check(#sent==0,'nonboolean mode rejected')
    request('band',{pid=2,band=format.band(),routes={},leaderPlays=false});check(#sent==0,'listener cannot change owner mode')
    players[1].position.x=301;request('heartbeat');check(sent[#sent].data.op=='close','invalid toggle did not relax proximity')
    for _,cause in ipairs({'cell','remove','timeout','playing'}) do
        reset();sequence=0;acquire();request('band',{band=format.band(),routes={},leaderPlays=false});sent={}
        players[1].position.x=1500
        if cause=='cell' then players[1].cell='INT:B'
        elseif cause=='remove' then objects[7]=nil
        elseif cause=='timeout' then now=now+9
        else request('band',{band=format.band(),routes={},leaderPlays=true}) end
        module.engineHandlers.onUpdate()
        check(sent[#sent].data.op=='close','conductor cleanup: '..cause)
    end
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
test('one-shot tails can be stopped after MIDI note-off without opening a panic flood',function()
    acquire();request('channel',{channel=1,params={drumType=4},level=1})
    request('on',{channel=1,note=46,velocity=1});request('off',{channel=1,note=46})
    sent={};request('panic');check(#sent==1 and sent[1].data.op=='panic','one-shot tail is released even after its MIDI gate ended')
    local count=#sent;for _=1,1000 do request('panic') end
    check(#sent==count,'redundant panic remains bounded')
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
test('DAW channels isolate shared pitches and late listener state',function()
    acquire();sent={}
    request('channel',{channel=1,params={wave1=2},level=0.7})
    request('channel',{channel=2,params={wave1=0},level=0.4})
    check(sent[#sent].data.channel==2 and sent[#sent].data.params.wave1==0)
    request('on',{channel=1,note=60,velocity=1});request('on',{channel=2,note=60,velocity=1})
    request('off',{channel=1,note=60});check(sent[#sent].data.channel==1 and sent[#sent].data.op=='off')
    request('off',{channel=2,note=60});check(sent[#sent].data.channel==2 and sent[#sent].data.op=='off')
    local count=#sent
    request('on',{channel=17,note=60,velocity=1});request('on',{channel=3,note=60,velocity=1})
    request('channel',{channel=3,params={},level=0/0});request('channel',{channel=3,params={},level=2})
    check(#sent==count,'invalid and unconfigured lanes rejected')
    request('channel',{channel=3,params={polyphony=32},level=1})
    check(sent[#sent].data.params.polyphony==8,'DAW polyphony bounded independently of manual synth')
    players[3].cell='INT:A';request('on',{channel=2,note=61,velocity=1})
    local snapshot
    for _,e in ipairs(sent) do if e.guid==3 and e.data.op=='state' then snapshot=e.data end end
    check(snapshot and snapshot.channels[1].params.wave1==2 and snapshot.channels[2].level==0.4,'late listener receives all timbres before note')
    request('channelRemove',{channel=2});check(sent[#sent].data.op=='channelRemove','removed channel is broadcast')
    local count=#sent;request('on',{channel=2,note=60,velocity=1});check(#sent==count,'removed channel cannot start another voice')
end)
test('DAW project saves per character and preserves presets and future documents',function()
    local f=require('procedural_synth_project')(schema)
    local project=f.new();project.name='My session';project.tracks[1]=f.newTrack('piano','Lead',16)
    project.tracks[1].notes={{n=64,s=0,l=4,v=0.8},{n=72,s=255,l=1,v=0.9}}
    project.tracks[2]=f.newTrack('drum','Drums',16)
    project.tracks[2].steps=string.rep('0',255)..'9'
    acquire();request('saveAs',{name='Personal tone',params={wave1=2}})
    request('dawSave',{project=project})
    check(storage['project:11'].name=='My session' and storage[11].entries[1].name=='Personal tone','project and presets use different keys')
    local persisted=copy(storage);reset(persisted);sequence=0;acquire();request('dawLoad')
    check(sent[#sent].data.op=='dawProject' and sent[#sent].data.project.tracks[1].notes[1].n==64,'project survives server reload')
    check(sent[#sent].data.project.length==16 and sent[#sent].data.project.tracks[1].notes[2].s==255 and sent[#sent].data.project.tracks[2].steps:sub(256,256)=='9','server round trip retains hidden bar 16 piano and drum data')
    now=now+3;request('dawLoad',{pid=2,characterId=22});check(storage['project:22']==nil,'another player cannot use owner session')
    project.tracks[1].notes[1].v=math.huge;request('dawSave',{project=project})
    check(storage['project:11'].tracks[1].notes[1].v==0.8,'malformed document cannot overwrite')
    storage['project:11']={version=99};now=now+3;request('dawLoad')
    check(sent[#sent].data.op=='error' and storage['project:11'].version==99,'future project preserved')
    now=now+3;project.tracks[1].notes[1].v=0.8;request('dawSave',{project=project});check(storage['project:11'].version==99,'save cannot overwrite an unsupported future project')
    local a=assert(io.open('files/server/server-lua-packages/fetcher.procedural-synth/shared/project.lua','rb'))
    local b=assert(io.open('apps/openmw-server/scripts/procedural_synth_project.lua','rb'))
    check(a:read('*a')==b:read('*a'),'project schema parity');a:close();b:close()
end)
test('guitar effects round trip in personal patches and late listener snapshots',function()
    acquire();local patch
    for _,p in ipairs(schema.builtins) do if p.id=='b:guitar-metal' then patch=copy(p.state) end end
    request('saveAs',{name='QA Guitar',params=patch})
    check(storage[11].entries[1].state.stringMute==0.7 and storage[11].entries[1].state.drive==32,'personal patch includes string and effects')
    request('channel',{channel=1,params=patch,level=0.7})
    players[3].cell='INT:A';sent={};request('on',{channel=1,note=40,velocity=0.8})
    local snapshot;for _,event in ipairs(sent) do if event.guid==3 and event.data.op=='state' then snapshot=event.data end end
    check(snapshot and snapshot.channels[1].params.cabinet==3 and snapshot.channels[1].params.instrumentType==1,'late listener gets the full guitar insert')
    now=now+3;request('list')
    local entries=sent[#sent].data.entries
    check(entries and #entries==#schema.builtins+1 and entries[#entries].bank=='Personal','catalog keeps personal patches and metadata')
    local guitars=0;for _,p in ipairs(entries) do if schema.inBank(p,'Guitar') then guitars=guitars+1 end end
    local expected=0;for _,p in ipairs(schema.builtins) do if schema.inBank(p,'Guitar') then expected=expected+1 end end
    check(guitars==expected and guitars>=14,'genre catalog exposes original and new guitar starting points')
end)
test('native scores are validated, timestamped and included in late sync',function()
    local Sequence=require('procedural_synth_sequence');local Format=require('procedural_synth_project')(schema)
    local p=Format.new();p.tracks[1]=Format.newTrack('piano','Clock',16);p.tracks[1].notes={{n=60,s=0,l=2,v=0.8},{n=64,s=4,l=2,v=0.7}}
    local score=Sequence.compile(p,nil,nil,'clips')
    acquire();request('channel',{channel=1,params={},level=1});sent={}
    request('clock',{sent=12});check(sent[#sent].data.op=='clock' and sent[#sent].data.serverTime==now,'clock echo uses server time')
    request('scoreBegin',{playId=1,count=#score.bytes/11,length=score.length,loop=score.loop,quantum=score.quantum,replace=false})
    request('scoreChunk',{playId=1,index=1,bytes=score.bytes});request('scoreCommit',{playId=1})
    local ready,meta=0,nil
    for _,e in ipairs(sent) do if e.data.op=='scoreReady' then ready=ready+1 elseif e.data.op=='scoreBegin' then meta=e.data end end
    check(ready==2 and meta.started>now and meta.started==meta.activate,'both clients get one future shared start')
    sent={};players[3].cell='INT:A';request('sync',{pid=3})
    check(sent[#sent].data.op=='scoreReady','late listener receives the compiled score with its original clock anchor')
    for _,e in ipairs(sent) do check(e.data.op~='on','score playback does not relay frame-dispatched note events') end
    request('transportStop',{playId=1});local before=#sent
    for id=1,1000 do request('transportStop',{playId=id}) end
    check(#sent==before,'duplicate and invented Stop generations cannot flood listeners')
    now=now+3;sent={};request('sync',{pid=3})
    check(sent[#sent].data.op=='state','stopped scores are absent from snapshots')
end)
test('native automation uploads replicate with the shared clock and remain owner controlled',function()
    local f=require('procedural_synth_project')(schema);local p=f.new();p.tracks[1]=f.newTrack('piano','Curve',16)
    p.tracks[1].notes={{n=60,s=0,l=2,v=1}};p.tracks[1].automation={expression={{s=0,v=0,linear=true},{s=8,v=1,linear=false}}}
    local score=require('procedural_synth_sequence').compile(p,nil,nil,'clips',f)
    acquire();request('channel',{channel=1,params={},level=1});sent={}
    request('scoreBegin',{playId=1,count=#score.bytes/11,length=score.length,loop=score.loop,quantum=score.quantum,replace=false})
    request('scoreChunk',{playId=1,index=1,bytes=score.bytes});request('scoreCommit',{playId=1})
    check(sent[#sent].data.op=='scoreReady','validated curves are relayed with notes')
    local before=#sent;request('scoreBegin',{pid=2,playId=2,count=3,length=score.length,loop=true,quantum=score.quantum,replace=false})
    check(#sent==before,'observer cannot replace owner automation')
    now=now+3;players[3].cell='INT:A';sent={};request('sync',{pid=3})
    local bytes='';for _,e in ipairs(sent) do if e.data.op=='scoreChunk' then bytes=bytes..e.data.bytes end end
    check(bytes==score.bytes,'late observer receives identical curve bytes for native chase')
    request('dawSave',{project=p});check(storage['project:11'].tracks[1].automation.expression[2].v==1,'saved curves stay with character project')
end)
test('tempo/meter bar grids schedule replacements and persist privately',function()
    local f=require('procedural_synth_project')(schema);local seq=require('procedural_synth_sequence')
    local p=f.new();p.composition={bars=8,numerator=4,denominator=4,loopStart=1,loopEnd=8,loop=false}
    p.timeline={beats=28,tempos={{beat=0,tempo=500000},{beat=8,tempo=1000000},{beat=14,tempo=750000}},
        meters={{beat=8,numerator=3,denominator=4},{beat=14,numerator=7,denominator=8}}}
    p.tracks[1]=f.newTrack('midi','Timing',16);p.tracks[1].edited=f.encodeNotes({{n=60,s=32,l=4,v=1}},960)
    local score=seq.compile(p,nil,nil,'song',f)
    acquire();request('channel',{channel=1,params={},level=1})
    local function upload(id,replace,grid,pid)
        request('scoreBegin',{pid=pid or 1,playId=id,count=#score.bytes/11,length=score.length,loop=false,quantum=score.quantum,replace=replace,barGrid=grid})
        request('scoreChunk',{pid=pid or 1,playId=id,index=1,bytes=score.bytes});request('scoreCommit',{pid=pid or 1,playId=id})
    end
    local function meta() for _,e in ipairs(sent) do if e.data.op=='scoreBegin' then return e.data end end end
    sent={};upload(1,false,score.barGrid);local first=assert(meta())
    now=first.started+4.1;sent={};upload(2,true,score.barGrid);local edit=assert(meta())
    check(edit.started==first.started and edit.activate==first.started+7,'replacement follows 3/4 bar at changed tempo, not fixed seconds')
    now=now+1;sent={};upload(3,true,score.barGrid,2);check(not meta(),'observer cannot dictate timing map')
    now=now+1;sent={};upload(4,true,'bad');check(not meta(),'malformed bar grid cannot replace running score')
    request('heartbeat')
    while now<first.started+20.4 do now=math.min(now+4,first.started+20.4);request('heartbeat') end
    sent={};upload(5,true,score.barGrid)
    check(not meta() and sent[#sent].data.op=='scoreError','ending finite score rejects replacement without inventing a bar')
    sent={};request('sync',{pid=2});check(meta().playId==2,'invalid/ending edit preserves previous transport')
    now=now+3;request('dawSave',{project=p});now=now+3;request('dawLoad')
    check(sent[#sent].data.project.timeline.meters[2].denominator==8 and storage['project:11'].timeline.tempos[3].tempo==750000,'maps survive owner SQLite contract')
end)
test('malformed and unauthorized native uploads preserve the running score',function()
    acquire();request('channel',{channel=1,params={},level=1});sent={}
    request('scoreBegin',{pid=2,playId=1,count=1,length=48000,loop=true,quantum=48000,replace=false})
    check(#sent==0,'nonowner cannot upload a score')
    request('scoreBegin',{playId=1,count=1,length=48000,loop=true,quantum=48000,replace=false})
    request('scoreChunk',{playId=1,index=2,bytes=string.rep('\0',11)});request('scoreCommit',{playId=1})
    check(sent[#sent].data.op=='scoreError','missing and reordered chunks cannot start playback')
    request('scoreChunk',{playId=1,index=1,bytes=string.rep('\0',11)});request('scoreCommit',{playId=1})
    check(sent[#sent].data.op=='scoreError','invalid binary note controls are rejected before relay')
end)
test('mixer migration, persistence, ownership and late listener state',function()
    local f=require('procedural_synth_project')(schema);local p=f.new();p.tracks[1]=f.newTrack('piano','Mix test',16)
    p.tracks[1].mixer=nil
    local old=f.project(p);check(old.tracks[1].mixer.eqEnabled==0 and old.tracks[1].mixer.compEnabled==0,'old projects migrate to bypass')
    local m=f.mixer();m.eqEnabled=1;m.highpass=180;m.mid=-4;m.compEnabled=1;m.threshold=-26;m.ratio=6
    check(not f.mixer({attack=0}) and not f.mixer({unknown=1}) and not f.mixer({eqEnabled=0.5}) and not f.mixer({makeup=math.huge}),'malformed mixer controls rejected')
    p.tracks[1].mixer=m;acquire();request('dawSave',{project=p});request('channel',{channel=1,params={},level=0.8,mixer=m})
    check(storage['project:11'].tracks[1].mixer.threshold==-26,'mixer stored with character project')
    local before=#sent;request('channel',{pid=2,channel=1,params={},level=1,mixer={bass=18}});check(#sent==before,'observer cannot edit owner mixer')
    for _,bad in ipairs{{ratio=0},{attack=0/0},{eqEnabled=0.5},{unknown=1},'invalid'} do
        request('channel',{channel=1,params={},level=1,mixer=bad})
    end
    check(#sent==before,'bad strip cannot replace active channel')
    players[3].cell='INT:A';request('sync',{pid=3})
    check(sent[#sent].data.channels[1].mixer.highpass==180 and sent[#sent].data.channels[1].mixer.ratio==6,'late snapshot includes full mixer')
    local persisted=copy(storage);reset(persisted);sequence=0;acquire();request('dawLoad')
    check(sent[#sent].data.project.tracks[1].mixer.mid==-4,'strip survives server restart')
end)
test('shared returns validate, persist and reach late listeners',function()
    local f=require('procedural_synth_project')(schema);local p=f.new();p.tracks[1]=f.newTrack('piano','Wet',16)
    p.tracks[1].mixer.delaySend=0.8;p.tracks[1].mixer.reverbSend=0.6
    p.effects.delayTime=375;p.effects.reverbDecay=3.2;p.master=0.4
    acquire();request('dawSave',{project=p});request('channel',{channel=1,params={},level=0.8,mixer=p.tracks[1].mixer})
    request('effects',{effects=p.effects,master=p.master});check(sent[#sent].data.op=='effects' and sent[#sent].data.master==0.4,'return state relayed separately from fader')
    local before=#sent
    request('effects',{pid=2,effects=p.effects,master=1});request('effects',{effects={delayFeedback=1},master=1})
    request('effects',{effects={reverbDecay=0/0},master=1});request('effects',{effects={},master=2})
    check(#sent==before,'unauthorized and unstable return settings rejected')
    players[3].cell='INT:A';request('sync',{pid=3});local s=sent[#sent].data
    check(s.master==0.4 and s.effects.delayTime==375 and s.channels[1].mixer.reverbSend==0.6,'late snapshot has sends, returns and master')
    reset(copy(storage));sequence=0;acquire();request('dawLoad')
    check(sent[#sent].data.project.effects.reverbDecay==3.2,'global returns survive database reload')
    local legacy=f.new();legacy.effects=nil;legacy.tracks[1]=f.newTrack('piano','Old',16);legacy.tracks[1].mixer=nil
    local migrated=f.project(legacy);check(migrated.effects.delayTime==250 and migrated.tracks[1].mixer.delaySend==0,'old songs remain dry')
end)
test('portable keyboard registers a real inventory record and saves before pickup',function()
    module.eventHandlers.OnServerInit()
    local item=records.fetcher_procedural_keyboard_v1
    check(item and item.kind=='miscellaneous' and item.data.weight==8 and item.data.icon,'portable keyboard has an inventory icon and item metadata')
    local f=require('procedural_synth_project')(schema);local p=f.new();p.name='Carry my song'
    acquire();request('pickup',{project=p})
    check(#pickups==1 and pickups[1].guid==1 and pickups[1].ref=='fetcher_procedural_keyboard_v1','owner queues one native atomic transfer')
    check(storage['project:11'].name=='Carry my song','dirty project is saved before moving the instrument')
    request('pickup',{project=p});check(#pickups==1,'double click cannot queue duplicate pickups')
    module.eventHandlers.OnPlacedObjectPickup{pid=1,guid=1,mpNum=7,refId=pickups[1].ref,accepted=true}
    acquire(2);check(sent[#sent].data.op=='error','client cannot forge native pickup completion')
    objects[7]=nil
    module.eventHandlers.OnPlacedObjectPickup{guid=1,mpNum=7,refId=pickups[1].ref,accepted=true}
    check(sent[#sent].data.op=='close','successful pickup releases every listener and editor')
end)
test('pickup preserves untouched saves, rejects other owners and recovers from failure',function()
    local f=require('procedural_synth_project')(schema);storage['project:11']=f.new();storage['project:11'].name='Existing song'
    acquire();request('pickup',{pid=2});check(#pickups==0,'other player cannot take an occupied keyboard via this session')
    request('pickup');check(#pickups==1 and storage['project:11'].name=='Existing song','fresh editor pickup does not overwrite saved song with empty defaults')
    module.eventHandlers.OnPlacedObjectPickup{guid=1,mpNum=7,refId='fetcher_procedural_keyboard_v1',accepted=false}
    check(sent[#sent].data.pickupFailed and objects[7],'failed native transaction leaves object/session available')
    now=now+3;request('on',{note=60,velocity=1});check(sent[#sent].data.op=='on','performance still works after pickup failure')
    package.loaded.mp.setCharacterStorageForCharacter=function() return false end
    request('pickup',{project=f.new()});check(#pickups==1 and sent[#sent].data.pickupFailed,'save failure cannot remove the world instrument')
end)
test('placed portable items acquire the same synth session',function()
    objects[7].refId='fetcher_procedural_keyboard_v1'
    handler{object={recordId=objects[7].refId,mpNum=7},player=players[1]}
    check(sent[#sent].data.op=='granted','inventory placement reopens the normal synth')
end)
test('band slots reserve placed keyboards and reject forged or competing control',function()
    objects[8]={mpNum=8,refId='fetcher_procedural_keyboard_v1',cell='INT:A',position={x=120,y=0,z=0}}
    acquire();local band=require('procedural_synth_project')(schema).band()
    band[1].target={mpNum=8,refId=objects[8].refId,cell='INT:A'}
    request('band',{band=band,routes={1,0}})
    check(sent[#sent].data.availability[1]=='Ready' and sent[#sent].data.routes[1]==1,'server resolves physical output')
    local before=#sent;request('band',{pid=2,band=band,routes={0}});check(#sent==before,'observer cannot change routing')
    handler{object={recordId=objects[8].refId,mpNum=8},player=players[2]}
    check(sent[#sent].data.op=='error','reserved instrument cannot become another session')
    request('sync',{pid=2});check(sent[#sent].data.band[1].target.mpNum==8,'late state contains binding and routing')
    request('close');handler{object={recordId=objects[8].refId,mpNum=8},player=players[2]}
    check(sent[#sent].data.op=='granted','Finish releases reservations')
end)
test('missing, duplicate, busy and distant band members never become another output',function()
    local f=require('procedural_synth_project')(schema);local band=f.band()
    for _,id in ipairs{8,9} do objects[id]={mpNum=id,refId='fetcher_procedural_keyboard_v1',cell='INT:A',position={x=100,y=0,z=0}} end
    handler{object={recordId=objects[9].refId,mpNum=9},player=players[2]};acquire()
    for slot,id in ipairs{8,8,9,10} do band[slot].target={mpNum=id,refId='fetcher_procedural_keyboard_v1',cell='INT:A'} end
    request('band',{epoch=2,band=band,routes={1,2,3,4}})
    local a=sent[#sent].data.availability
    check(a[1]=='Ready' and a[2]=='Busy' and a[3]=='Busy' and a[4]=='Missing','invalid destinations remain explicitly unavailable')
    objects[8]=nil;now=now+2.1;module.engineHandlers.onUpdate()
    local missing=false;for _,e in ipairs(sent) do if e.data.op=='band' and e.data.id==7 and e.data.availability[1]=='Missing' then missing=true end end
    check(missing,'removal reaches all listeners during native playback')
    objects[8]={mpNum=8,refId='fetcher_procedural_keyboard_v1',cell='INT:A',position={x=2100,y=0,z=0}}
    request('band',{epoch=2,band=band,routes={1}});check(sent[#sent].data.availability[1]=='Out of range','remote objects cannot be claimed')
    band[1].target.refId='wrong_record';local count=#sent;request('band',{epoch=2,band=band,routes={1}})
    check(#sent==count,'malformed record identity is rejected')
end)
test('band routing saves per character and survives format migration',function()
    local f=require('procedural_synth_project')(schema);local p=f.new();p.tracks[1]=f.newTrack('piano','Routed',16)
    p.band[1].name='Bass keyboard';p.band[1].level=0.4;p.band[1].mute=true
    p.band[1].target={mpNum=8,cell='INT:A',refId='fetcher_procedural_keyboard_v1'};p.tracks[1].output.slot=1
    acquire();request('dawSave',{project=p});reset(copy(storage));sequence=0;acquire();request('dawLoad')
    local saved=sent[#sent].data.project
    check(saved.band[1].name=='Bass keyboard' and saved.band[1].level==0.4 and saved.tracks[1].output.slot==1,'routing and member mix survive character storage')
    p.band=nil;p.tracks[1].output.slot=nil;local migrated=f.project(p)
    check(migrated and #migrated.band==32 and migrated.tracks[1].output.slot==0,'older songs retain home keyboard routing')
    check(not f.routes({33}) and not f.routes({[2]=1}) and not f.band({{name='Bad',level=0/0,mute=false}}),'invalid slots, sparse routes and gains rejected')
end)
test('member mix changes preserve native transport but structural routes cancel it',function()
    local f=require('procedural_synth_project')(schema);local p=f.new();p.tracks[1]=f.newTrack('piano','Clock',16);p.tracks[1].notes={{n=60,s=0,l=2,v=1}}
    local score=require('procedural_synth_sequence').compile(p,nil,nil,'clips')
    acquire();request('channel',{channel=1,params={},level=1})
    request('scoreBegin',{playId=1,count=#score.bytes/11,length=score.length,loop=score.loop,quantum=score.quantum,replace=false})
    request('scoreChunk',{playId=1,index=1,bytes=score.bytes});request('scoreCommit',{playId=1})
    local band=f.band();band[1].mute=true;band[1].level=0.2;sent={}
    request('band',{band=band,routes={0}})
    for _,e in ipairs(sent) do check(e.data.op~='transportStop','member mute cannot reset score clock') end
    sent={};request('band',{band=band,routes={1}})
    local stopped=false;for _,e in ipairs(sent) do if e.data.op=='transportStop' then stopped=true end end
    check(stopped,'structural route change stops old score before assigning a new destination')
end)
test('performer commands are authenticated outside leader-only Studio operations',function()
    players[2].inventory={{refId='misc_de_lute_01',count=1}}
    acquire();local f=require('procedural_synth_project')(schema);local band=f.band()
    band[1].target={kind='player',characterId=22,instrument='lute'}
    request('band',{band=band,routes={1}})
    local token
    for _,e in ipairs(sent) do if e.guid==2 and e.data.op=='performer' and e.data.invitation then token=e.data.invitation.token end end
    check(token and sent[#sent].data.availability[1]=='Invitation pending','leader binding sends a consent request rather than acquiring another player')
    request('respond',{pid=3,characterId=33,token=token,accept=true})
    check(sent[#sent].data.op~='band' or sent[#sent].data.availability[1]~='Ready','observer cannot accept on behalf of member')
    request('hold',{pid=2,characterId=22,instrument='lute'})
    request('respond',{pid=2,characterId=22,token=token,accept=true})
    check(sent[#sent].data.op=='band' and sent[#sent].data.availability[1]=='Ready' and sent[#sent].data.performers[1].guid==2,'native sender identity authorizes the member and broadcasts the actor source')
    request('leaveBand',{pid=2,characterId=22})
    check(sent[#sent].data.op=='band' and sent[#sent].data.availability[1]=='Invite required' and not sent[#sent].data.performers[1],'leaving silences the member without a leader command')
end)
test('edited MIDI and its tempo map save privately without resurrecting empty parts',function()
    local f=require('procedural_synth_project')(schema);local project=f.new()
    project.midi='midi/edit.mid';project.timeline={beats=8,tempos={{beat=0,tempo=500000},{beat=4,tempo=1000000}}}
    project.tracks[1]=f.newTrack('midi','Edited',16);project.tracks[1].part='1:0'
    project.tracks[1].edited=assert(f.encodeNotes({{n=60,s=4.5,l=2.5,v=1}},480))
    acquire();request('dawSave',{project=project});request('dawLoad')
    check(sent[#sent].data.project.tracks[1].edited.bytes==project.tracks[1].edited.bytes and sent[#sent].data.project.timeline.tempos[2].tempo==1000000,'edited data and tempo snapshot round trip through private character storage')
    now=now+3;project.tracks[1].edited.bytes='';request('dawSave',{project=project});now=now+2;request('dawLoad')
    check(sent[#sent].data.project.tracks[1].edited.bytes=='','empty edited part stays explicitly empty across storage')
    now=now+3;project.tracks[1].edited.bytes='bad';request('dawSave',{project=project})
    check(storage['project:11'].tracks[1].edited.bytes=='' and not storage['project:22'],'invalid binary cannot overwrite saved edits or another character')
end)
test('authored meters share bounded native replacement quanta',function()
    local f=require('procedural_synth_project')(schema);local seq=require('procedural_synth_sequence')
    acquire();request('channel',{channel=1,params={},level=1})
    for index,quantum in ipairs{3000,1728000} do
        now=now+3;sent={}
        local p=f.new();p.tracks[1]=f.newTrack('piano','Hit',16);p.tracks[1].notes={{n=60,s=0,l=1,v=0.8}}
        local score=seq.compile(p,nil,nil,'clips')
        request('scoreBegin',{playId=index,count=1,length=score.length,loop=false,quantum=quantum,replace=false})
        request('scoreChunk',{playId=index,index=1,bytes=score.bytes});request('scoreCommit',{playId=index})
        local accepted=false;for _,event in ipairs(sent) do if event.data.op=='scoreReady' then accepted=true end end
        check(accepted,'supported meter quantum accepted: '..quantum)
        request('transportStop',{playId=index})
    end
    for index,quantum in ipairs{2999,1728001} do
        now=now+3;sent={};request('scoreBegin',{playId=100+index,count=1,length=48000,loop=false,quantum=quantum,replace=false})
        request('scoreCommit',{playId=100+index})
        local accepted=false;for _,event in ipairs(sent) do if event.data.op=='scoreReady' then accepted=true end end
        check(not accepted,'out-of-range quantum cannot create a transport')
    end
end)
test('source variations and placement offsets retain private persistence and validation',function()
    local f=require('procedural_synth_project')(schema);local p=f.new();p.timeline={beats=64,tempos={{beat=0,tempo=500000}}}
    local t=f.newTrack('midi','Offset copy',16);t.edited=f.encodeNotes({},960)
    t.arrangement={clips={{name='Original',length=16,notes=f.encodeNotes({{n=60,s=0,l=4,v=1}},960)},
        {name='Variation',length=16,notes=f.encodeNotes({{n=64,s=0,l=4,v=1}},960)}},
        regions={{clip=2,s=16,l=24,offset=2,transpose=0,mute=false}}};p.tracks[1]=t
    acquire();request('dawSave',{project=p});now=now+3;request('dawLoad')
    check(sent[#sent].data.project.tracks[1].arrangement.regions[1].offset==2,'source offset and independent variation survive storage')
    now=now+3;request('dawLoad',{pid=2,characterId=22});check(not storage['project:22'],'copied sources remain character private')
    t.arrangement.regions[1].offset=16;request('dawSave',{project=p})
    check(storage['project:11'].tracks[1].arrangement.regions[1].offset==2,'invalid offset cannot overwrite saved source phase')
end)
test('authored song meter and loop markers survive private character reload',function()
    local f=require('procedural_synth_project')(schema);local project=f.new()
    project.name='Blank song';project.composition={bars=8,numerator=6,denominator=8,loopStart=2,loopEnd=4,loop=true}
    project.timeline={beats=24,tempos={{beat=0,tempo=500000}}}
    project.tracks[1]=f.newTrack('midi','Keys',16);project.tracks[1].sourceRole='keys'
    project.tracks[1].edited=assert(f.encodeNotes({{n=65,s=12,l=2,v=0.8}},960))
    acquire();request('dawSave',{project=project});check(storage['project:11'].composition.numerator==6,'composition reaches character storage')
    reset(copy(storage));sequence=0;acquire();request('dawLoad')
    local loaded=sent[#sent].data.project
    check(loaded.midi=='' and loaded.composition.loopEnd==4 and loaded.tracks[1].edited.bytes==project.tracks[1].edited.bytes,'authored song reloads after server module restart')
    check(not storage['project:22'],'other character does not inherit the authored song')
    now=now+3;project.composition.loopEnd=257;request('dawSave',{project=project})
    check(storage['project:11'].composition.loopEnd==4,'invalid markers cannot overwrite the saved song')
end)
test('linked clip sources and placements persist privately with bounded references',function()
    local f=require('procedural_synth_project')(schema);local project=f.new()
    project.timeline={beats=32,tempos={{beat=0,tempo=500000}}}
    project.tracks[1]=f.newTrack('midi','Arranged',16);project.tracks[1].edited=f.encodeNotes({},960)
    project.tracks[1].arrangement={clips={{name='Verse',length=16,notes=f.encodeNotes({{n=60,s=0,l=2,v=1}},960)}},
        regions={{clip=1,s=0,l=16,transpose=0,mute=false},{clip=1,s=32,l=32,transpose=12,mute=false}}}
    acquire();request('dawSave',{project=project});reset(copy(storage));sequence=0;acquire();request('dawLoad')
    local loaded=sent[#sent].data.project
    check(#loaded.tracks[1].arrangement.clips==1 and #loaded.tracks[1].arrangement.regions==2,'linked sources/placements survive module reload')
    check(loaded.tracks[1].arrangement.regions[2].transpose==12 and not storage['project:22'],'placement transforms stay private to the character')
    now=now+3;project.tracks[1].arrangement.regions[1].clip=9;request('dawSave',{project=project})
    check(storage['project:11'].tracks[1].arrangement.regions[1].clip==1,'invalid source reference cannot overwrite valid saved data')
end)
test('sample sounds persist privately and replicate references without file data',function()
    local ref={path='sound/vo/d/m/hlo_dm001.mp3',offset=.125,duration=1.5,root=60,oneShot=true,hash=string.rep('a',32)}
    local params=assert(schema.state{sample=ref});acquire();request('channel',{channel=1,params=params,level=.8})
    check(sent[#sent].data.params.sample.path==ref.path and sent[#sent].data.params.sample.hash==ref.hash,'listener receives normalized sample identity')
    request('on',{channel=1,note=60,velocity=1});request('off',{channel=1,note=60})
    local before=#sent;request('panic');check(#sent>before and sent[#sent].data.op=='panic','manual one-shot sample tail can stop after note-off')
    before=#sent;request('panic');check(#sent==before,'sample tail panic cannot be flooded without another note')
    request('saveAs',{name='Voice instrument',params=params})
    check(storage[11].entries[1].state.sample.duration==1.5,'personal preset retains sample trim')
    local f=require('procedural_synth_project')(schema);local project=f.new();project.tracks[1]=f.newTrack('piano','Voice',16);project.tracks[1].params=params
    request('dawSave',{project=project});check(storage['project:11'].tracks[1].params.sample.root==60,'project stores sample reference')
    reset(copy(storage));sequence=0;acquire();request('dawLoad')
    check(sent[#sent].data.project.tracks[1].params.sample.offset==.125 and not storage['project:22'],'sample project reload remains per character')
    now=now+3;params.sample.path='sound/../secret.wav';project.tracks[1].params=params;request('dawSave',{project=project})
    check(storage['project:11'].tracks[1].params.sample.path==ref.path,'unsafe sample cannot overwrite saved project')
end)
test('multisample bank identity is replicated and saved without PCM',function()
    local preset;for _,p in ipairs(schema.builtins) do if p.id=='b:sampled-electric-metal' then preset=p end end
    check(preset~=nil,'sampled guitar preset exists')
    acquire();request('channel',{channel=1,params=preset.state,level=.8})
    check(sent[#sent].data.params.sample.hash==preset.state.sample.hash and sent[#sent].data.params.sample.path==preset.state.sample.path,'listener receives the identical bank reference')
    request('saveAs',{name='Recorded metal',params=preset.state})
    check(storage[11].entries[1].state.sample.path==preset.state.sample.path,'personal sound stores bank identity')
    local f=require('procedural_synth_project')(schema);local p=f.new();p.tracks[1]=f.newTrack('piano','Recorded guitar',16);p.tracks[1].params=preset.state
    request('dawSave',{project=p});reset(copy(storage));sequence=0;acquire();request('dawLoad')
    check(sent[#sent].data.project.tracks[1].params.sample.hash==preset.state.sample.hash,'bank identity survives server restart and project load')
end)
test('project library requests stay private, versioned and bounded',function()
    local f=require('procedural_synth_project')(schema);local p=f.new();p.name='Private library song'
    acquire();request('libSave',{requestId=1,projectId=0,saveRevision=1,project=p})
    check(sent[#sent].data.op=='libResult' and sent[#sent].data.projectId==1 and storage['library:11'].entries[1].project.name==p.name,'library save uses authenticated character storage')
    local count=#sent
    request('libList',{pid=2,characterId=22,requestId=2});request('libSave',{characterId=22,requestId=3,projectId=0,saveRevision=1,project=p})
    check(#sent==count and not storage['library:22'],'observer and forged character cannot read or write owner library')
    request('libSave',{requestId=4,projectId=1,projectRevision=0,saveRevision=2,project=p})
    check(sent[#sent].data.message and storage['library:11'].entries[1].revision==1,'stale revision is rejected without replacing the project')
    request('libSnapshot',{requestId=5,projectId=1,token='private',saveRevision=2,project=p})
    check(storage['recovery:11'].entries[1].project.name==p.name and not storage['recovery:22'],'recovery stays private to owner')
    local before=#sent;for n=1,1000 do request('libList',{requestId=10+n}) end
    check(#sent-before<=4,'library rejection responses are bounded under a request flood')
end)
test('server MIDI requests require the current nearby keyboard owner',function()
    local scans=0
    package.loaded.mp.listStudioMidiFiles=function() scans=scans+1;return {} end
    acquire();sent={}
    request('midiCatalog',{pid=2,requestId=1});module.engineHandlers.onUpdate()
    check(scans==0 and #sent==0,'observer cannot request library')
    request('midiCatalog',{requestId=1});module.engineHandlers.onUpdate()
    check(scans==1 and sent[#sent].data.op=='midiCatalogBegin','owner requests hosted catalogue')
    request('close');sent={};now=now+.05;module.engineHandlers.onUpdate()
    check(#sent==0,'closing Studio cancels queued delivery')
end)
test('stage floor payload reaches owned-session validation',function()
    acquire();sent={}
    local f=require('procedural_synth_project')(schema);local p=f.new()
    request('autoBand',{requestId=1,project=p,stageLayout='seated',ground={anchor={x=0,y=0,z=0,rz=0},heights={}}})
    check(sent[#sent] and sent[#sent].data.op=='bandSetup','ground payload must reach stage service rather than silently timing out')
end)
print(string.format('%d tests, %d assertions passed',tests,checks))
