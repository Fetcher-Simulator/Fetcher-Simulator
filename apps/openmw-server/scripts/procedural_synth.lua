local mp = require('mp')
local config = require('config')
if config.PROCEDURAL_SYNTH_ENABLED ~= true then return {} end
local schema = require('procedural_synth_schema')
local projectFormat = require('procedural_synth_project')(schema)
local sequenceFormat = require('procedural_synth_sequence')
local policy = require('intent_policy')
local RECORD = 'fetcher_procedural_synth_v1'
local PORTABLE = 'fetcher_procedural_keyboard_v1'
local function isKeyboard(id) return id==RECORD or id==PORTABLE end
local sessions, budgets, epoch = {}, {}, 0
local claims = {}
local lastSweep = 0
local sendTransport
local function integer(n, low, high)
    return type(n) == 'number' and n == n and n >= low and n <= high and n % 1 == 0
end
local function text(s, max) return type(s) == 'string' and #s > 0 and #s <= max and not s:find('%c') end
local function relevant(player, cell)
    if player.cell == cell then return true end
    for _, c in ipairs(player.loadedActorCells or {}) do if c == cell then return true end end
    return false
end
local function emit(guid, data) data.version = 1; mp.send(guid, 'Synth_Message', data) end
local function broadcast(s, out, except)
    out.id, out.epoch, out.seq, out.revision, out.owner = s.id, s.epoch, s.outSeq, s.revision, s.owner
    for _, player in ipairs(mp.getPlayers()) do
        if player.guid ~= except and relevant(player, s.cell) then
            if out.op == 'state' then s.recipients[player.guid] = true end
            if out.op == 'on' and not s.recipients[player.guid] then
                emit(player.guid, {op='state',owner=s.owner,id=s.id,epoch=s.epoch,seq=s.outSeq-1,revision=s.revision,params=s.params,channels=s.channels,effects=s.effects,master=s.master,band=s.band,routes=s.routes,availability=s.availability,cell=s.cell})
                if s.transport then sendTransport(player.guid,s,s.transport) end
                s.recipients[player.guid] = true
            end
            emit(player.guid, out)
            if out.op=='state' and s.transport then sendTransport(player.guid,s,s.transport) end
        end
    end
end
sendTransport=function(guid,s,t)
    emit(guid,{op='scoreBegin',id=s.id,epoch=s.epoch,playId=t.playId,count=t.count,length=t.length,loop=t.loop,
        started=t.started,activate=t.activate,replace=t.replace})
    local index=0
    for at=1,#t.bytes,sequenceFormat.chunkSize do index=index+1
        emit(guid,{op='scoreChunk',id=s.id,epoch=s.epoch,playId=t.playId,index=index,bytes=t.bytes:sub(at,at+sequenceFormat.chunkSize-1)})
    end
    emit(guid,{op='scoreReady',id=s.id,epoch=s.epoch,playId=t.playId})
end
local function nextEvent(s) s.outSeq = s.outSeq + 1 end
local function release(s)
    nextEvent(s); broadcast(s, {op='close'})
    sessions[s.id] = nil
    for id,owner in pairs(claims) do if owner==s.id then claims[id]=nil end end
end
local function objectFor(id)
    local obj = mp.getObjectByMpNum(id)
    return obj and isKeyboard(obj.refId) and obj or nil
end
local function resolveBand(s)
    local statuses={};local changed=false
    for id,owner in pairs(claims) do if owner==s.id then claims[id]=nil end end
    for n,m in ipairs(s.band) do
        local t=m.target;local obj=t and objectFor(t.mpNum)
        local status='Unbound'
        if t then
            status='Missing'
            if obj and obj.cell==t.cell and obj.refId==t.refId then
                local home=objectFor(s.id);local a,b=home and home.position,obj.position
                if obj.cell~=s.cell or not a or not b or (a.x-b.x)^2+(a.y-b.y)^2+(a.z-b.z)^2>2000^2 then status='Out of range'
                elseif obj.mpNum==s.id then status='Use This keyboard output'
                elseif sessions[obj.mpNum] or claims[obj.mpNum] then status='Busy'
                else status='Ready';claims[obj.mpNum]=s.id end
            end
        end
        statuses[n]=status;if not s.availability or s.availability[n]~=status then changed=true end
    end
    s.availability=statuses;return changed
end
local function sendBand(s)
    nextEvent(s);broadcast(s,{op='band',band=s.band,routes=s.routes,availability=s.availability})
end
local function near(player, obj)
    if not obj or player.cell ~= obj.cell then return false end
    local a, b = player.position, obj.position
    if not a or not b then return false end
    return (a.x-b.x)^2 + (a.y-b.y)^2 + (a.z-b.z)^2 <= 300^2
end
local function catalog(guid, character)
    local saved = mp.getCharacterStorageForCharacter(character, 'ProceduralSynth', 'presets')
    if type(saved) ~= 'table' then saved = { version=1, nextId=1, entries={} } end
    if saved.version ~= 1 or type(saved.entries) ~= 'table' or not integer(saved.nextId,1,2147483647) then
        emit(guid, {op='error', message='Unsupported preset library version; existing data was preserved.'})
        return nil
    end
    return saved
end
local function sendCatalog(guid, library)
    local list = {}
    for _, p in ipairs(schema.builtins) do list[#list+1] = {id=p.id, name=p.name, state=p.state,bank=p.bank,role=p.role} end
    for _, p in ipairs(library.entries) do list[#list+1] = {id=p.id, name=p.name, state=p.state,bank='Personal',role='Personal'} end
    emit(guid, {op='catalog', entries=list})
end
local function acquire(player, obj)
    local now = mp.getUptime()
    local s = sessions[obj.mpNum]
    if s and (not mp.getPlayer(s.owner) or now > s.expires) then release(s); s = nil end
    if s then
        if s.owner ~= player.guid then emit(player.guid, {op='error', message='This synthesizer is already in use.'}) end
        return
    end
    if claims[obj.mpNum] then emit(player.guid,{op='error',message='This keyboard is assigned to another band.'});return end
    local count = 0
    for _ in pairs(sessions) do count = count + 1 end
    if count >= 8 then emit(player.guid, {op='error', message='All synthesizer sessions are in use.'}); return end
    epoch = epoch + 1
    s = { id=obj.mpNum, cell=obj.cell, owner=player.guid, epoch=epoch, inSeq=0, outSeq=1,
        revision=1, params=schema.state({}), expires=now+8, lastAlive=now, recipients={}, notes={}, channels={}, effects=projectFormat.effects(),master=0.7,band=projectFormat.band(),routes={},availability={} }
    sessions[s.id] = s;resolveBand(s)
    broadcast(s, {op='state', params=s.params,channels=s.channels,effects=s.effects,master=s.master,band=s.band,routes=s.routes,availability=s.availability,cell=s.cell}, player.guid)
    emit(player.guid, {op='granted', owner=s.owner, id=s.id, epoch=s.epoch, seq=s.outSeq, revision=s.revision, params=s.params,channels=s.channels,effects=s.effects,master=s.master,band=s.band,routes=s.routes,availability=s.availability,cell=s.cell})
end
policy.registerHandler('Activate', 'procedural-synth', function(context)
    local obj = context.object
    if not obj or not isKeyboard(obj.recordId) then return end
    local verified = objectFor(obj.mpNum or 0)
    if not context.player or not near(context.player, verified) then return {accepted=false, reason='synth_out_of_range'} end
    acquire(context.player, verified)
    return { applyStandardAction=false, action='synth', stop=true }
end)
local function budget(guid, now, category, rate, capacity)
    local b = budgets[guid] or {}; budgets[guid] = b
    local slot = b[category] or {time=now, tokens=capacity}; b[category] = slot
    slot.tokens = math.min(capacity, slot.tokens + math.max(0,now-slot.time)*rate); slot.time = now
    if slot.tokens < 1 then return false end
    slot.tokens = slot.tokens - 1; return true
end
local function request(data)
    if type(data) ~= 'table' or data.version ~= 1 or not integer(data.pid,1,4294967295) then return end
    local fields = 0
    for k,v in pairs(data) do
        fields = fields + 1
        if fields > 14 or not text(k,24) then return end
        if type(v) == 'table' then if k=='params' then if not schema.state(v) then return end
            elseif k=='effects' and data.op=='effects' then if not projectFormat.effects(v) then return end
            elseif k=='mixer' and data.op=='channel' then if not projectFormat.mixer(v) then return end
            elseif k=='band' and data.op=='band' then if not projectFormat.band(v) then return end
            elseif k=='routes' and data.op=='band' then if not projectFormat.routes(v) then return end
            elseif k=='project' and (data.op=='dawSave' or data.op=='pickup') then if not projectFormat.project(v) then return end
            else return end
        elseif type(v) == 'string' then
            if k=='bytes' and data.op=='scoreChunk' then if #v<11 or #v>sequenceFormat.chunkSize or #v%11~=0 then return end
            elseif not text(v,96) then return end
        elseif type(v) ~= 'number' and type(v) ~= 'boolean' then return end
    end
    local player = mp.getPlayer(data.pid)
    if not player then return end
    local now = mp.getUptime()
    if data.op=='clock' then
        if type(data.sent)=='number' and data.sent==data.sent and math.abs(data.sent)<1e12 and budget(data.pid,now,'clock',2,4) then
            emit(data.pid,{op='clock',sent=data.sent,serverTime=now})
        end
        return
    end
    if data.op == 'acquire' then
        if not integer(data.id,1,4294967295) or not budget(data.pid,now,'acquire',1,2) then return end
        local obj=objectFor(data.id)
        if near(player,obj) then acquire(player,obj)
        else emit(data.pid,{op='error',message='Move closer to the synthesizer.'}) end
        return
    end
    if data.op == 'sync' then
        if not budget(data.pid,now,'sync',0.5,2) then return end
        for _, s in pairs(sessions) do
            if relevant(player,s.cell) then
                emit(data.pid,{op='state',owner=s.owner,id=s.id,epoch=s.epoch,seq=s.outSeq,revision=s.revision,params=s.params,channels=s.channels,effects=s.effects,master=s.master,band=s.band,routes=s.routes,availability=s.availability,cell=s.cell})
                if s.transport then sendTransport(data.pid,s,s.transport) end
                s.recipients[data.pid]=true
            end
        end
        return
    end
    if not integer(data.id,1,4294967295) then return end
    local s = sessions[data.id]
    if not s or s.owner ~= data.pid or s.epoch ~= data.epoch or not integer(data.seq,1,2147483647)
        or data.seq <= s.inSeq then return end
    if now > s.expires or not near(player,objectFor(s.id)) then release(s); return end
    s.inSeq = data.seq
    local channel=data.channel or 0
    if not integer(channel,0,16) then return end
    local noteKey=channel*128+(integer(data.note,0,127) and data.note or 0)
    -- Safety operations bypass musical/edit rate limits.
    if data.op == 'close' then release(s); return end
    if data.op == 'heartbeat' then
        if budget(data.pid,now,'heartbeat',1,2) then s.expires=now+8 end
        return
    end
    if s.pickup then return end
    if data.op=='band' then
        local band,routes=projectFormat.band(data.band),projectFormat.routes(data.routes)
        if not band or not routes or not budget(data.pid,now,'band',4,8) then return end
        local structural=false
        for n,m in ipairs(band) do
            local a,b=m.target,s.band[n].target
            if (a and a.mpNum)~=(b and b.mpNum) or (a and a.cell)~=(b and b.cell) or (a and a.refId)~=(b and b.refId) then structural=true end
        end
        for n=1,16 do if (routes[n] or 0)~=(s.routes[n] or 0) then structural=true end end
        if structural and (s.transport or s.upload) then
            s.transport=nil;s.upload=nil;s.notes={};s.drumPending=false
            nextEvent(s);broadcast(s,{op='transportStop',playId=s.lastPlayId or 0})
        end
        s.band=band;s.routes=routes;resolveBand(s);sendBand(s);return
    end
    if data.op=='pickup' then
        local function failed(message) emit(data.pid,{op='error',message=message,pickupFailed=true,id=s.id,epoch=s.epoch}) end
        if not budget(data.pid,now,'projects',0.5,2) then failed('Please wait a moment before picking up the keyboard.');return end
        if not mp.pickupPlacedObject then failed('Keyboard pickup requires the updated server.');return end
        if data.project then
            if not integer(data.characterId,1,9007199254740991) then return end
            local existing=mp.getCharacterStorageForCharacter(data.characterId,'ProceduralSynth','project')
            local project=projectFormat.project(data.project)
            if existing~=nil and not projectFormat.project(existing) then failed('Existing saved project is unsupported; keyboard was left in place.');return end
            if not project or not mp.setCharacterStorageForCharacter(data.characterId,'ProceduralSynth','project',project) then
                failed('Project save failed; keyboard was left in place.');return
            end
        end
        s.pickup=true
        if not mp.pickupPlacedObject(s.owner,s.id,PORTABLE) then s.pickup=nil;failed('Keyboard pickup could not be queued. Please try again.') end
        return
    end
    if data.op=='transportStop' then
        if not integer(data.playId,1,2147483647) or not s.upload and not s.transport then return end
        if data.playId<math.max(s.upload and s.upload.playId or 0,s.transport and s.transport.playId or 0) then return end
        if s.upload and data.playId>=s.upload.playId then s.upload=nil end
        if s.transport and data.playId>=s.transport.playId then s.transport=nil end
        if data.playId>(s.lastStopped or 0) then
            s.lastStopped=data.playId;nextEvent(s);broadcast(s,{op='transportStop',playId=data.playId})
        end
        return
    end
    if data.op=='scoreBegin' then
        if not integer(data.playId,1,2147483647) or data.playId<=(s.lastPlayId or 0)
            or not integer(data.count,1,sequenceFormat.maxNotes) or not integer(data.length,1,sequenceFormat.maxFrames)
            or type(data.loop)~='boolean' or type(data.replace)~='boolean' or not integer(data.quantum,48000,288000)
            or not budget(data.pid,now,'scoreBegins',4,4) then return end
        s.lastPlayId=data.playId
        s.upload={playId=data.playId,count=data.count,length=data.length,loop=data.loop,quantum=data.quantum,
            replace=data.replace,chunks={},size=0,next=1,expires=now+20}
        return
    end
    if data.op=='scoreChunk' then
        local u=s.upload
        if not u or u.playId~=data.playId or now>u.expires or data.index~=u.next or type(data.bytes)~='string'
            or #data.bytes<11 or #data.bytes>sequenceFormat.chunkSize or #data.bytes%11~=0
            or u.size+#data.bytes>u.count*11 or not budget(data.pid,now,'scoreChunks',64,64) then return end
        u.chunks[#u.chunks+1]=data.bytes;u.size=u.size+#data.bytes;u.next=u.next+1;return
    end
    if data.op=='scoreCommit' then
        local u=s.upload
        if not u or u.playId~=data.playId or now>u.expires or u.size~=u.count*11 then
            emit(data.pid,{op='scoreError',id=s.id,epoch=s.epoch,playId=data.playId,message='Incomplete score upload.'});return
        end
        s.upload=nil;u.bytes=table.concat(u.chunks);u.chunks=nil
        local valid,err=sequenceFormat.validate(u.bytes,u.length,u.loop,s.channels)
        if not valid then emit(data.pid,{op='scoreError',id=s.id,epoch=s.epoch,playId=u.playId,message=err});return end
        local old=s.transport
        u.replace=u.replace and old~=nil
        u.started=u.replace and old.started or now+0.75
        u.activate=u.replace and (u.started+(math.floor((now-u.started+0.25)/(u.quantum/48000))+1)*u.quantum/48000) or u.started
        s.transport=u
        for _,p in ipairs(mp.getPlayers()) do if relevant(p,s.cell) then sendTransport(p.guid,s,u);s.recipients[p.guid]=true end end
        return
    end
    if data.op == 'off' or data.op == 'panic' then
        if data.op == 'off' and not integer(data.note,0,127) then return end
        if data.op == 'off' then
            if not s.notes[noteKey] then return end
            s.notes[noteKey] = nil
        else
            if next(s.notes) == nil and not s.drumPending then return end
            s.notes = {};s.drumPending=false
        end
        nextEvent(s); broadcast(s,{op=data.op,note=data.note,channel=channel},s.owner); return
    end
    if data.op == 'on' then
        if channel>0 and not s.channels[channel] then return end
        if not integer(data.note,0,127) or type(data.velocity) ~= 'number'
            or data.velocity ~= data.velocity or data.velocity < 0 or data.velocity > 1 then return end
        if not budget(data.pid,now,'notes',40,80) then
            -- End all remote voices when an optimistic local batch is rejected.
            if budget(data.pid,now,'errors',1,1) then
                s.notes = {};s.drumPending=false
                nextEvent(s); broadcast(s,{op='panic'})
            end
            return
        end
        s.notes[noteKey] = true
        local params=channel==0 and s.params or s.channels[channel].params
        if params.drumType>0 then s.drumPending=true end
        nextEvent(s); broadcast(s,{op='on',note=data.note,velocity=data.velocity,channel=channel},s.owner); return
    end
    if data.op=='channelRemove' then
        if channel==0 or not s.channels[channel] or not budget(data.pid,now,'channels',12,32) then return end
        s.channels[channel]=nil
        for key in pairs(s.notes) do if math.floor(key/128)==channel then s.notes[key]=nil end end
        nextEvent(s);broadcast(s,{op='channelRemove',channel=channel},s.owner);return
    end
    if data.op=='effects' then
        local effects=projectFormat.effects(data.effects)
        if not effects or type(data.master)~='number' or data.master~=data.master or data.master<0 or data.master>1
            or not budget(data.pid,now,'channels',12,32) then return end
        s.effects=effects;s.master=data.master;s.revision=s.revision+1
        nextEvent(s);broadcast(s,{op='effects',effects=effects,master=s.master},s.owner);return
    end
    if data.op=='channel' then
        local params=schema.state(data.params)
        local mixer=projectFormat.mixer(data.mixer)
        if not mixer or channel==0 or not params or type(data.level)~='number' or data.level~=data.level or data.level<0 or data.level>1
            or not budget(data.pid,now,'channels',12,32) then return end
        params.polyphony=math.min(8,params.polyphony)
        s.channels[channel]={params=params,level=data.level,mixer=mixer}
        s.revision=s.revision+1
        nextEvent(s);broadcast(s,{op='channel',channel=channel,params=params,level=data.level,mixer=mixer},s.owner);return
    end
    if data.op=='dawSave' or data.op=='dawLoad' then
        if not integer(data.characterId,1,9007199254740991) or not budget(data.pid,now,'projects',0.5,2) then return end
        if data.op=='dawSave' then
            if not integer(data.saveRevision or 0,0,2147483647) then return end
            local existing=mp.getCharacterStorageForCharacter(data.characterId,'ProceduralSynth','project')
            if existing~=nil and not projectFormat.project(existing) then
                emit(data.pid,{op='error',message='Unsupported saved project; existing data preserved.'});return
            end
            local project=projectFormat.project(data.project)
            if project and mp.setCharacterStorageForCharacter(data.characterId,'ProceduralSynth','project',project) then
                emit(data.pid,{op='dawSaved',id=s.id,epoch=s.epoch,saveRevision=data.saveRevision or 0})
            else emit(data.pid,{op='error',message='Project save failed; existing project preserved.'}) end
        else
            local saved=mp.getCharacterStorageForCharacter(data.characterId,'ProceduralSynth','project')
            local project=saved==nil and projectFormat.new() or projectFormat.project(saved)
            if project then emit(data.pid,{op='dawProject',project=project,id=s.id,epoch=s.epoch})
            else emit(data.pid,{op='error',message='Unsupported project; existing saved data preserved.'}) end
        end
        return
    end
    if data.op == 'edit' then
        local params = schema.state(data.params)
        if not params or not budget(data.pid,now,'edits',12,16) then return end
        if channel>0 then return end
        s.params = params; s.revision = s.revision + 1
        nextEvent(s); broadcast(s,{op='params',params=params},s.owner); return
    end
    if not budget(data.pid,now,'presets',2,4) or not integer(data.characterId,1,9007199254740991) then return end
    local library = catalog(data.pid,data.characterId)
    if not library then return end
    local selected
    for _, p in ipairs(library.entries) do if p.id == data.presetId then selected = p; break end end
    if data.op == 'list' then sendCatalog(data.pid,library); return end
    if data.op == 'load' or data.op == 'reset' then
        if channel~=0 then return end
        local params = data.op == 'reset' and schema.state({}) or selected and schema.state(selected.state)
        for _, p in ipairs(schema.builtins) do if data.op == 'load' and p.id == data.presetId then params=p.state end end
        if not params then return end
        s.params=params; s.revision=s.revision+1; s.notes={};s.drumPending=false
        nextEvent(s); broadcast(s,{op='preset',params=params,presetId=data.presetId}); return
    end
    if data.op == 'saveAs' or data.op == 'save' then
        local params = schema.state(data.params)
        if not params or not text(data.name,64) or not data.name:find('%S') then return end
        if data.op == 'save' and not selected then return end
        if data.op == 'saveAs' then
            if #library.entries >= 32 then emit(data.pid,{op='error',message='Personal preset limit: 32.'}); return end
            selected={id='p:'..library.nextId}; library.nextId=library.nextId+1
            library.entries[#library.entries+1]=selected
        end
        selected.name, selected.state = data.name, params
    elseif data.op == 'rename' then
        if not selected or not text(data.name,64) or not data.name:find('%S') then return end
        selected.name=data.name
    elseif data.op == 'delete' then
        if not selected then return end
        for i,p in ipairs(library.entries) do if p.id == selected.id then table.remove(library.entries,i); break end end
    else return end
    if not mp.setCharacterStorageForCharacter(data.characterId,'ProceduralSynth','presets',library) then
        emit(data.pid,{op='error',message='Preset storage failed. Please try again.'}); return
    end
    emit(data.pid,{op='saved',presetId=selected.id,deleted=data.op=='delete',channel=channel,action=data.op})
    sendCatalog(data.pid,library)
end
local function sweep()
    local now = mp.getUptime()
    if now-lastSweep < 0.5 then return end
    lastSweep=now
    for _, s in pairs(sessions) do
        if s.upload and now>s.upload.expires then s.upload=nil end
        if s.transport and not s.transport.loop and now>s.transport.started+s.transport.length/48000+8 then s.transport=nil end
        local owner=mp.getPlayer(s.owner)
        if not owner or now>s.expires or not near(owner,objectFor(s.id)) then release(s)
        elseif now-s.lastAlive >= 2 then
            s.lastAlive=now
            -- New listeners receive parameters only, never a historical note-on.
            for _, p in ipairs(mp.getPlayers()) do
                if relevant(p,s.cell) and p.guid ~= s.owner then
                    if not s.recipients[p.guid] then
                        emit(p.guid,{op='state',owner=s.owner,id=s.id,epoch=s.epoch,seq=s.outSeq,revision=s.revision,params=s.params,channels=s.channels,effects=s.effects,master=s.master,band=s.band,routes=s.routes,availability=s.availability,cell=s.cell})
                        if s.transport then sendTransport(p.guid,s,s.transport) end
                        s.recipients[p.guid]=true
                    end
                else s.recipients[p.guid]=nil end
            end
            if resolveBand(s) then sendBand(s) end
            nextEvent(s); broadcast(s,{op='alive'})
        end
    end
end
return {
    engineHandlers={onUpdate=sweep},
    eventHandlers={
        Synth_Request=request,
        OnPlacedObjectPickup=function(data)
            -- Client events always have an injected pid. Only a native server
            -- completion may finish this transfer; never trust a claimed guid.
            if data.pid~=nil or data.refId~=PORTABLE then return end
            local s=sessions[data.mpNum]
            if not s or not s.pickup or s.owner~=data.guid then return end
            s.pickup=nil
            if data.accepted then
                if config.PROCEDURAL_SYNTH_TEST_MP_NUM==s.id and mp.storage then
                    mp.storage.globalSection('ProceduralSynthFixture'):set('pickedUp',true)
                end
                emit(s.owner,{op='pickedUp',id=s.id,epoch=s.epoch})
                release(s)
            else emit(s.owner,{op='error',message='Keyboard pickup failed ('..tostring(data.error or 'unknown')..'); it was left in place. Please try again.',pickupFailed=true,id=s.id,epoch=s.epoch}) end
        end,
        OnServerInit=function()
            -- Runtime record with a vanilla mesh; no client ESP and no asset transfer.
            local ok=mp.upsertDynamicRecord('activator',RECORD,
                {name='OpenMW Synthesizer',model=config.PROCEDURAL_SYNTH_MODEL or 'meshes/m/Misc_de_lute_01.nif'},
                {scope='permanent',persistent=true})
            mp.log('[synth] runtime activator registered: '..tostring(ok))
            mp.upsertDynamicRecord('miscellaneous',PORTABLE,
                {name='Portable Synth Keyboard',model=config.PROCEDURAL_SYNTH_MODEL or 'meshes/m/Misc_de_lute_01.nif',
                    icon=config.PROCEDURAL_SYNTH_ICON or 'm/misc_de_lute_01.tga',weight=8,value=200},
                {scope='permanent',persistent=true})
            -- Placement is opt-in. The disposable fixture pins its expected mpNum
            -- so restarting it does not duplicate its persistent placed object.
            if config.PROCEDURAL_SYNTH_TEST_CELL then
                local existing=config.PROCEDURAL_SYNTH_TEST_MP_NUM and objectFor(config.PROCEDURAL_SYNTH_TEST_MP_NUM)
                local pickedUp=mp.storage and mp.storage.globalSection('ProceduralSynthFixture'):getCopy('pickedUp')
                if not existing and not pickedUp then mp.placeObject(RECORD,1,config.PROCEDURAL_SYNTH_TEST_CELL,
                    config.PROCEDURAL_SYNTH_TEST_POSITION or {x=0,y=0,z=0}) end
            end
        end,
        OnPlayerDisconnect=function(data)
            budgets[data.guid]=nil
            for _,s in pairs(sessions) do if s.owner==data.guid then release(s) end end
        end,
        OnPlayerCellChange=function(data)
            for _,s in pairs(sessions) do
                s.recipients[data.guid]=nil
                if s.owner==data.guid then release(s) end
            end
        end,
    },
}
