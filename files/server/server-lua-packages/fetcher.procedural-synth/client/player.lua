local mp = require('openmw.mp')
local core = require('openmw.core')
local nearby = require('openmw.nearby')
local self = require('openmw.self')
local ui = require('openmw.ui')
local supported, synth = pcall(require,'openmw.synth')
if not supported then
    return {eventHandlers={Synth_Activate=function()
        ui.showMessage('This synthesizer requires an OpenMW client with openmw.synth API version 1.')
    end}}
end
local schema = require('scripts.multiplayer.fetcher.procedural-synth.shared.schema')
local Editor = require('scripts.multiplayer.fetcher.procedural-synth.client.editor')
local instances, editor, session = {}, nil, nil
local connected, lastSync, lastHeartbeat, lastEdit = false, -10, 0, 0
local dirty
local function send(op, extra)
    if not session then return end
    session.inSeq=session.inSeq+1
    local out={version=1,op=op,id=session.id,epoch=session.epoch,seq=session.inSeq}
    for k,v in pairs(extra or {}) do out[k]=v end
    mp.sendToServer('Synth_Request',out)
end
local function closeEditor(notify)
    if editor then editor:destroy(); editor=nil end
    if session then
        local instance=instances[session.id]
        if instance then instance.audio:allNotesOff() end
        if notify then send('close') end
        session=nil; dirty=nil
    end
end
local function destroy(id)
    local i=instances[id]
    if i then i.audio:close(); instances[id]=nil end
    if session and session.id==id then closeEditor(false) end
end
local function object(id)
    for _, o in ipairs(nearby.activators) do
        if o.recordId=='fetcher_procedural_synth_v1' and mp.getObjectMpNum(o)==id then return o end
    end
end
local function apply(instance, params)
    for name in pairs(schema.specs) do instance.audio:set(name,params[name]) end
    instance.params=params
end
local function message(data)
    if not mp.isConnected() or type(data)~='table' or data.version~=1 then return end
    if data.op=='error' then ui.showMessage(data.message); return end
    if data.op=='catalog' then if editor then editor:catalog(data.entries) end; return end
    if data.op=='saved' then
        if editor then editor:saved(data.presetId,data.deleted) end
        ui.showMessage(data.deleted and 'Preset deleted.' or 'Preset saved.'); return
    end
    if type(data.id)~='number' or type(data.epoch)~='number' or type(data.seq)~='number' then return end
    local i=instances[data.id]
    if i and (data.epoch<i.epoch or (data.epoch==i.epoch and data.seq<=i.seq)) then return end
    if data.op=='close' then destroy(data.id); return end
    if data.op=='state' or data.op=='granted' then
        local params=schema.state(data.params)
        local source=object(data.id)
        if not source or not params then return end
        if i then destroy(data.id) end
        local ok,audio=pcall(synth.create,source)
        if not ok then ui.showMessage(tostring(audio)); return end
        i={audio=audio,source=source,cell=self.cell,epoch=data.epoch,seq=data.seq,seen=core.getRealTime()}
        instances[data.id]=i; apply(i,params)
        if data.op=='granted' then
            closeEditor(true)
            session={id=data.id,epoch=data.epoch,inSeq=0}
            editor=Editor.new(params,{
                noteOn=function(note,velocity)
                    velocity=velocity or 0.8
                    i.audio:noteOn(note,velocity); send('on',{note=note,velocity=velocity})
                end,
                noteOff=function(note) i.audio:noteOff(note); send('off',{note=note}) end,
                panic=function() i.audio:allNotesOff(); send('panic') end,
                change=function(name,value) i.audio:set(name,value); dirty=true end,
                command=function(op,args)
                    -- Flush pending edits before operations that depend on current sound.
                    if dirty then send('edit',{params=editor.params}); dirty=nil end
                    send(op,args)
                end,
                close=function() closeEditor(true) end,
                meter=function() return i.audio.peak,i.audio.voices end,
            })
            send('list'); lastHeartbeat=core.getRealTime()
        end
        return
    end
    if not i or i.epoch~=data.epoch then return end
    i.seq=data.seq; i.seen=core.getRealTime(); i.audio:keepAlive()
    if data.op=='on' then i.audio:noteOn(data.note,data.velocity)
    elseif data.op=='off' then i.audio:noteOff(data.note)
    elseif data.op=='panic' then i.audio:allNotesOff()
    elseif data.op=='params' or data.op=='preset' then
        local params=schema.state(data.params)
        if params then
            if data.op=='preset' then i.audio:allNotesOff() end
            apply(i,params)
            if editor and session.id==data.id then editor:state(params,data.presetId); dirty=nil end
        end
    end
end
local function frame()
    local now=core.getRealTime()
    if not mp.isConnected() then
        for id in pairs(instances) do destroy(id) end
        closeEditor(false); connected=false; return
    end
    if not connected or now-lastSync>5 then
        connected=true; lastSync=now
        -- Request only when there are no instances: a snapshot never restarts held notes.
        if next(instances)==nil then mp.sendToServer('Synth_Request',{version=1,op='sync'}) end
    end
    for id,i in pairs(instances) do
        if not i.source:isValid() or self.cell~=i.cell or now-i.seen>7 or i.audio.closed then destroy(id)
        elseif now-i.seen<5 then i.audio:keepAlive() end
    end
    if not editor then return end
    local i=instances[session.id]
    if not i then closeEditor(true); return end
    -- The owner receives server liveness too; disconnected transport cannot sustain notes indefinitely.
    if now-lastHeartbeat>=2 then send('heartbeat'); lastHeartbeat=now end
    if dirty and now-lastEdit>=0.1 then send('edit',{params=editor.params}); dirty=nil; lastEdit=now end
    editor:frame(now)
end
return {
    interfaceName='ProceduralSynth',
    interface={
        version=1,
        status=function()
            local out={}
            for id,i in pairs(instances) do out[#out+1]={id=id,voices=i.audio.voices,peak=i.audio.peak,
                editing=session~=nil and session.id==id,epoch=i.epoch,seq=i.seq,
                cutoff=i.params.cutoff,wave1=i.params.wave1} end
            return out
        end,
        noteOn=function(note,velocity) if editor then editor:note(note,true,velocity) end end,
        noteOff=function(note) if editor then editor:note(note,false) end end,
        setParameter=function(name,value)
            if editor and schema.valid(name,value) then
                editor.params[name]=value; dirty=true
                instances[session.id].audio:set(name,value)
                if editor.controls[name] then editor.controls[name]() end
                editor:plots()
                return true
            end
            return false
        end,
        presetCommand=function(op,args) if editor then editor.cb.command(op,args) end end,
        close=function() closeEditor(true) end,
    },
    engineHandlers={onFrame=frame,onKeyPress=function(e) if editor then editor:key(e.code,true) end end,
        onKeyRelease=function(e) if editor then editor:key(e.code,false) end end},
    eventHandlers={Synth_Message=message,Synth_Activate=function(data)
        mp.sendToServer('Synth_Request',{version=1,op='acquire',id=data.id})
    end},
}
