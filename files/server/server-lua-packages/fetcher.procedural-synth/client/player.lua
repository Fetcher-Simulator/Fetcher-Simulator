local mp = require('openmw.mp')
local core = require('openmw.core')
local nearby = require('openmw.nearby')
local self = require('openmw.self')
local ui = require('openmw.ui')
local input = require('openmw.input')
local supported, synth = pcall(require,'openmw.synth')
if not supported then
    return {eventHandlers={Synth_Activate=function()
        ui.showMessage('This synthesizer requires an OpenMW client with openmw.synth API version 1.')
    end}}
end
local schema = require('scripts.multiplayer.fetcher.procedural-synth.shared.schema')
local Editor = require('scripts.multiplayer.fetcher.procedural-synth.client.editor')
local MIDI = require('scripts.multiplayer.fetcher.procedural-synth.client.midi')
local Performance = require('scripts.multiplayer.fetcher.procedural-synth.client.performance')
local vfs = require('openmw.vfs')
local instances, editor, session = {}, nil, nil
local connected, lastSync, lastHeartbeat, lastEdit = false, -10, 0, 0
local dirty
local songs,selectedSong,playback,loading,scanned={},1,nil,nil,false
local function scanSongs()
    songs={};selectedSong=1;scanned=true
    for path in vfs.pathsWithPrefix('midi/') do
        if path:lower():match('%.mid[i]?$') then songs[#songs+1]=path;if #songs>=512 then break end end
    end
    table.sort(songs)
end
local stopPlayback,playSong,showEditor
local function silence(i)
    i.audio:allNotesOff(); i.notes={}
end
local function send(op, extra)
    if not session then return end
    session.inSeq=session.inSeq+1
    local out={version=1,op=op,id=session.id,epoch=session.epoch,seq=session.inSeq}
    for k,v in pairs(extra or {}) do out[k]=v end
    mp.sendToServer('Synth_Request',out)
end
local function hideEditor()
    if not editor then return end
    session.view={name=editor.name,selected=editor.selected,entries=editor.entries}
    editor:destroy(); editor=nil
    ui.showMessage('Synth performance continues. Press F8 or activate the synth to reopen. Use Finish to end the session.')
end
local function closeEditor(notify)
    if stopPlayback then stopPlayback() end
    if editor then editor:destroy(); editor=nil end
    if session then
        local instance=instances[session.id]
        if instance then silence(instance) end
        if notify then send('close') end
        session=nil; dirty=nil
    end
end
local function destroy(id)
    local i=instances[id]
    if session and session.id==id then closeEditor(false) end
    if i then
        if i.pose and not i.pose.missing then pcall(i.pose.clear,i.pose) end
        i.audio:close(); instances[id]=nil
    end
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
local function songLabel()
    local name=songs[selectedSong]
    if loading then return 'Loading MIDI…' end
    if playback then return string.format('Playing %s  %.1fs',name and name:match('([^/]+)$') or 'MIDI',playback.time) end
    return name and name:match('([^/]+)$'):gsub('%.mid[i]?$','') or 'No MIDI files installed'
end
stopPlayback=function()
    loading=nil
    if playback then local old=playback;playback=nil;old:stop() end
end
local function startSong(song)
    if not session then return false end
    stopPlayback()
    local i=instances[session.id]
    if editor then editor:releaseAll() end
    silence(i);send('panic')
    local tokens,last=80,core.getRealTime()
    playback=MIDI.player(song,{
        on=function(note,velocity)
            local now=core.getRealTime()
            tokens=math.min(80,tokens+math.max(0,now-last)*40);last=now
            if tokens<1 then
                stopPlayback();ui.showMessage('MIDI note density exceeds the multiplayer playback limit.');return
            end
            tokens=tokens-1;i.audio:noteOn(note,velocity);i.notes[note]=true;i.strikes=(i.strikes or 0)+1
            send('on',{note=note,velocity=velocity})
        end,
        off=function(note) i.audio:noteOff(note);i.notes[note]=nil;send('off',{note=note}) end,
        panic=function() silence(i);send('panic') end,
        finished=function(reason) if reason then ui.showMessage(reason) end end,
    })
    return true
end
playSong=function(path)
    if not session then return false end
    path=path or songs[selectedSong]
    local allowed=false
    for n,p in ipairs(songs) do if p==path then allowed=true;selectedSong=n;break end end
    if not allowed then return false end
    stopPlayback()
    loading=coroutine.create(function()
        local file,err=vfs.open(path)
        assert(file,err or 'Cannot open MIDI')
        local bytes=file:read(512*1024+1);file:close()
        return MIDI.parse(bytes,function() coroutine.yield() end)
    end)
    return true
end
showEditor=function()
    if editor or not session then return false end
    local i=instances[session.id]
    if not i then return false end
    editor=Editor.new(i.params,{
        noteOn=function(note,velocity)
            stopPlayback()
            velocity=velocity or 0.8
            i.audio:noteOn(note,velocity); i.notes[note]=true;i.strikes=(i.strikes or 0)+1
            send('on',{note=note,velocity=velocity})
        end,
        noteOff=function(note) i.audio:noteOff(note); i.notes[note]=nil;send('off',{note=note}) end,
        panic=function() stopPlayback();silence(i); send('panic') end,
        change=function(name,value) i.audio:set(name,value); dirty=true end,
        command=function(op,args)
            if op=='load' or op=='reset' then stopPlayback() end
            -- Flush pending edits before operations that depend on current sound.
            if dirty then send('edit',{params=i.params}); dirty=nil end
            send(op,args)
        end,
        hide=hideEditor,
        close=function() closeEditor(true) end,
        meter=function() return i.audio.peak,i.audio.voices end,
        midiLabel=songLabel,
        midiCycle=function(step) stopPlayback();if #songs>0 then selectedSong=(selectedSong-1+step)%#songs+1 end end,
        midiPlay=function() playSong() end,
        midiStop=stopPlayback,
    })
    local view=session.view
    if view then
        editor.name=view.name;editor.selected=view.selected;editor:catalog(view.entries)
    end
    return true
end
local function message(data)
    if not mp.isConnected() or type(data)~='table' or data.version~=1 then return end
    if data.op=='error' then ui.showMessage(data.message); return end
    if data.op=='catalog' then
        if session then
            session.view=session.view or {name='Untitled'}
            session.view.entries=data.entries or {}
        end
        if editor then editor:catalog(data.entries) end
        return
    end
    if data.op=='saved' then
        if session then
            session.view=session.view or {name='Untitled',entries={}}
            session.view.selected=not data.deleted and data.presetId or nil
        end
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
        i={audio=audio,source=source,cell=self.cell,owner=data.owner,notes={},epoch=data.epoch,seq=data.seq,seen=core.getRealTime()}
        instances[data.id]=i; apply(i,params)
        if data.op=='granted' then
            closeEditor(true)
            session={id=data.id,epoch=data.epoch,inSeq=0}
            showEditor()
            send('list'); lastHeartbeat=core.getRealTime()
        end
        return
    end
    if not i or i.epoch~=data.epoch then return end
    i.seq=data.seq; i.seen=core.getRealTime(); i.audio:keepAlive()
    if data.op=='on' then i.audio:noteOn(data.note,data.velocity);i.notes[data.note]=true;i.strikes=(i.strikes or 0)+1
    elseif data.op=='off' then i.audio:noteOff(data.note);i.notes[data.note]=nil
    elseif data.op=='panic' then if session and session.id==data.id then stopPlayback() end;silence(i)
    elseif data.op=='params' or data.op=='preset' then
        local params=schema.state(data.params)
        if params then
            if data.op=='preset' then if session and session.id==data.id then stopPlayback() end;silence(i) end
            apply(i,params)
            if session and session.id==data.id then
                if data.presetId then
                    session.view=session.view or {name='Untitled',entries={}}
                    session.view.selected=data.presetId
                    for _,p in ipairs(session.view.entries or {}) do
                        if p.id==data.presetId then session.view.name=p.name end
                    end
                end
                if editor then editor:state(params,data.presetId) end
                dirty=nil
            end
        end
    end
end
local function frame(dt)
    dt=dt or 0
    if not scanned then scanSongs() end
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
        else
            if now-i.seen<5 then i.audio:keepAlive() end
            if mp.getPlayerObject and i.owner then
                local actor=mp.getPlayerObject(i.owner)
                if i.pose and i.pose.actor~=actor then
                    if not i.pose.missing then pcall(i.pose.clear,i.pose) end
                    i.pose=nil
                end
                if actor and actor:isValid() then
                    if not i.pose then i.pose=Performance.new(actor) end
                    if i.pose and not i.pose.missing then i.pose:frame(dt,i.source,i.notes,now,i.strikes) end
                end
            end
        end
    end
    if not session then return end
    local i=instances[session.id]
    if not i then closeEditor(true); return end
    -- The owner receives server liveness too; disconnected transport cannot sustain notes indefinitely.
    if now-lastHeartbeat>=2 then send('heartbeat'); lastHeartbeat=now end
    if dirty and now-lastEdit>=0.1 then send('edit',{params=i.params}); dirty=nil; lastEdit=now end
    if loading then
        local ok,result=coroutine.resume(loading)
        if not ok then loading=nil;ui.showMessage('MIDI: '..tostring(result))
        elseif coroutine.status(loading)=='dead' then loading=nil;startSong(result) end
    end
    if playback then
        local current=playback;current:update(dt)
        if not current.playing and playback==current then playback=nil end
    end
    if editor then editor:frame(now) end
end
return {
    interfaceName='ProceduralSynth',
    interface={
        version=3,
        status=function()
            local out={}
            for id,i in pairs(instances) do out[#out+1]={id=id,owner=i.owner,voices=i.audio.voices,peak=i.audio.peak,
                editing=session~=nil and session.id==id,editorVisible=editor~=nil and session.id==id,epoch=i.epoch,seq=i.seq,
                cutoff=i.params.cutoff,wave1=i.params.wave1,poseBones=i.pose and i.pose.applied or 0,
                poseError=i.pose and i.pose.error or 0,midi=playback~=nil and playback.playing or false} end
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
        hide=hideEditor,
        show=showEditor,
        midiSongs=function() local out={};for n,p in ipairs(songs) do out[n]=p end;return out end,
        playMidi=playSong,
        stopMidi=stopPlayback,
    },
    engineHandlers={onFrame=frame,onInit=scanSongs,onKeyPress=function(e)
        if session and e.code==input.KEY.F8 then
            if editor then hideEditor() else showEditor() end
        elseif editor then editor:key(e.code,true) end
    end,
        onKeyRelease=function(e) if editor then editor:key(e.code,false) end end},
    eventHandlers={Synth_Message=message,Synth_Activate=function(data)
        if session and session.id==data.id then showEditor();return end
        mp.sendToServer('Synth_Request',{version=1,op='acquire',id=data.id})
    end},
}
