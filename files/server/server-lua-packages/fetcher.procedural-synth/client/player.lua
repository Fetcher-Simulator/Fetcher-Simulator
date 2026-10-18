local mp = require('openmw.mp')
local core = require('openmw.core')
local nearby = require('openmw.nearby')
local self = require('openmw.self')
local ui = require('openmw.ui')
local input = require('openmw.input')
local supported, synth = pcall(require,'openmw.synth')
if not supported or (synth.version and synth.version<6) then
    return {eventHandlers={Synth_Activate=function()
        ui.showMessage('This synthesizer requires an OpenMW client with openmw.synth API version 6.')
    end}}
end
local schema = require('scripts.multiplayer.fetcher.procedural-synth.shared.schema')
local Editor = require('scripts.multiplayer.fetcher.procedural-synth.client.editor')
local MIDI = require('scripts.multiplayer.fetcher.procedural-synth.client.midi')
local Workstation = require('scripts.multiplayer.fetcher.procedural-synth.client.workstation')
local Studio = require('scripts.multiplayer.fetcher.procedural-synth.client.studio')
local Performance = require('scripts.multiplayer.fetcher.procedural-synth.client.performance')
local vfs = require('openmw.vfs')
local NativeTransport=require('scripts.multiplayer.fetcher.procedural-synth.client.transport')
local transport=NativeTransport.new(synth,mp,core,function(message) ui.showMessage(message);print('[SynthStudio] '..message) end)
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
local stopPlayback,playSong,showEditor,switchPage,makeWorkstation
local loadIntent
local function silence(i)
    i.audio:allNotesOff(); i.notes={}
    for _,channel in pairs(i.channels or {}) do if channel.audio then channel.audio:allNotesOff() end end
end
local function send(op, extra)
    if not session then return end
    session.inSeq=session.inSeq+1
    local out={version=1,op=op,id=session.id,epoch=session.epoch,seq=session.inSeq}
    for k,v in pairs(extra or {}) do out[k]=v end
    mp.sendToServer('Synth_Request',out)
end
local function captureEditor()
    if not editor or editor.kind=='studio' then return end
    local view={name=editor.name,selected=editor.selected,entries=editor.entries,page=editor.page,bank=editor.bank,browserPage=editor.browserPage}
    session.views[session.editTrack]=view
    if session.editTrack==0 then session.view=view end
end
local function hideEditor()
    if not editor then return end
    captureEditor()
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
        i.audio:close()
        for _,channel in pairs(i.channels or {}) do if channel.audio then channel.audio:close() end end
        if i.studio then i.studio:close() end
        transport.downloads[id]=nil
        instances[id]=nil
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
local function applyChannel(i,n,params,level)
    local channel=i.channels[n] or {};i.channels[n]=channel
    channel.params=params;channel.level=level
    if channel.audio then
        if i.studio then transport:configure(i,n);return end
        for name in pairs(schema.specs) do channel.audio:set(name,name=='polyphony' and math.min(8,params[name]) or params[name]) end
        channel.audio:set('volume',params.volume*level)
    end
end
local function channelAudio(i,n,create)
    if n==0 then return i.audio end
    local channel=i.channels[n];if not channel then return end
    if not channel.audio and create then
        if synth.createStudio then
            local ok,audio=pcall(transport.audio,transport,i,n)
            if not ok then ui.showMessage(tostring(audio));return end
            channel.audio=audio;return audio
        end
        local ok,audio=pcall(synth.create,i.source)
        if not ok then ui.showMessage('DAW audio source unavailable: '..tostring(audio));return end
        channel.audio=audio;applyChannel(i,n,channel.params,channel.level)
    end
    return channel.audio
end
local function playNote(i,channel,note,velocity)
    local params=channel==0 and i.params or i.channels[channel] and i.channels[channel].params
    if params and params.drumType==3 then
        -- Every listener applies the same kit choke locally from the note event.
        for n,c in pairs(i.channels) do if c.params.drumType==4 and c.audio then
            c.audio:allNotesOff()
            for key in pairs(i.notes) do if math.floor(key/128)==n then i.notes[key]=nil end end
        end end
        if i.params.drumType==4 then i.audio:allNotesOff();for key in pairs(i.notes) do if key<128 then i.notes[key]=nil end end end
    end
    local audio=channelAudio(i,channel,true);if not audio then return false end
    if audio:noteOn(note,velocity)==false then return false end
    i.notes[channel*128+note]=true;i.strikes=(i.strikes or 0)+1;return true
end
local function releaseNote(i,channel,note)
    local audio=channelAudio(i,channel,false);if audio then audio:noteOff(note) end
    i.notes[channel*128+note]=nil
end
local function flushEdit()
    if not dirty or not session then return end
    if session.editTrack>0 then session.workstation:configure(session.editTrack)
    else send('edit',{params=instances[session.id].params}) end
    dirty=nil
end
local function songLabel()
    local name=songs[selectedSong]
    if loading then return 'Loading MIDI…' end
    local active=playback or session and session.workstation and session.workstation.playing and session.workstation
    if active then return string.format('Playing %s  %.1fs',name and name:match('([^/]+)$') or 'MIDI',active.time) end
    return name and name:match('([^/]+)$'):gsub('%.mid[i]?$','') or 'No MIDI files installed'
end
stopPlayback=function(force)
    loading=nil
    if playback then local old=playback;playback=nil;old:stop() end
    if session and session.workstation then session.workstation:stop(force) end
end
local function startSong(song,intent)
    if not session then return false end
    if editor then editor:releaseAll() end
    local w=session.workstation
    if intent=='restore' then w.song=song;w:cacheSpans();w.revision=w.revision+1;return true end
    if not w:import(song,songs[selectedSong]) then return false end
    if intent=='import' then return true end
    return w:play('song')
end
playSong=function(path,intent)
    if not session then return false end
    path=path or songs[selectedSong]
    local allowed=false
    for n,p in ipairs(songs) do if p==path then allowed=true;selectedSong=n;break end end
    if not allowed then return false end
    stopPlayback();loadIntent=intent or 'play'
    loading=coroutine.create(function()
        local file,err=vfs.open(path)
        assert(file,err or 'Cannot open MIDI')
        local bytes=file:read(512*1024+1);file:close()
        return MIDI.parse(bytes,function() coroutine.yield() end)
    end)
    return true
end
makeWorkstation=function(i)
    local tokens,last=80,core.getRealTime()
    local w=Workstation.new{
        schedule=synth.createStudio and function(plan,replace) return transport:queue(i,plan,replace,send) end or nil,
        stopSequence=function() transport:stop(i,send) end,
        progress=function()
            local a,t=i.studio,i.transport
            return a and a.playing or false,a and a.audiblePosition or 0,a and a.fault or 0,
                a and t and not t.loop and not a.playing and a.position>=t.length/48000 or false
        end,
        configure=function(n,params,level)
            local values={level};for _,name in ipairs(schema.order) do values[#values+1]=params[name] end
            local signature=table.concat(values,',')
            if i.channels[n] and i.channels[n].signature==signature then return end
            applyChannel(i,n,params,level);i.channels[n].signature=signature
            send('channel',{channel=n,params=params,level=level})
        end,
        currentParams=function() return i.params end,
        prune=function(count)
            for n,channel in pairs(i.channels) do if n>count then if channel.audio then channel.audio:close() end;i.channels[n]=nil;send('channelRemove',{channel=n}) end end
        end,
        on=function(channel,note,velocity)
            local now=core.getRealTime();tokens=math.min(80,tokens+math.max(0,now-last)*40);last=now
            if tokens<1 then stopPlayback();ui.showMessage('DAW note density exceeds the multiplayer limit.');return false end
            tokens=tokens-1
            if not playNote(i,channel,note,velocity) then stopPlayback();return false end
            send('on',{channel=channel,note=note,velocity=velocity});return true
        end,
        off=function(channel,note) releaseNote(i,channel,note);send('off',{channel=channel,note=note}) end,
        panic=function() silence(i);send('panic') end,
        save=function(project,generation) send('dawSave',{project=project,saveRevision=generation}) end,
        importMidi=function(path) playSong(path,'restore') end,
        message=ui.showMessage,
        meter=function(n) local audio=channelAudio(i,n,false);return audio and audio.peak or 0 end,
    }
    i.workstation=w;return w
end
switchPage=function(page,track)
    if not session then return end
    flushEdit();captureEditor()
    if editor then editor:destroy();editor=nil end
    session.page=page
    if track~=nil then session.editTrack=track end
    showEditor()
end
showEditor=function()
    if editor or not session then return false end
    local i=instances[session.id];if not i then return false end
    local w=session.workstation
    if session.page=='studio' then
        editor=Studio.new(w,{
            hide=hideEditor,close=function() closeEditor(true) end,
            synth=function() switchPage('synth',w.project.tracks[w.selected] and w.selected or 0) end,
            sound=function(index) w.selected=index;switchPage('synth',index) end,
            playSong=function()
                stopPlayback()
                if w.song and w.project.midi==songs[selectedSong] then w:play('song') else playSong(nil,'play') end
            end,
            playClips=function() stopPlayback();w:play('clips') end,
            stop=function() stopPlayback(true) end,
            load=function() stopPlayback();send('dawLoad') end,
            import=function() playSong(nil,'import') end,
            cycleMidi=function(step) if #songs>0 then selectedSong=(selectedSong-1+step)%#songs+1 end end,
            midiLabel=songLabel,
        })
        return true
    end
    local track=session.editTrack>0 and w.project.tracks[session.editTrack]
    if session.editTrack>0 and not track then session.editTrack=0 end
    local params=track and track.params or i.params
    editor=Editor.new(params,{
        subtitle=track and ('TRACK '..session.editTrack..' / '..track.name) or 'MANUAL INSTRUMENT',
        noteOn=function(note,velocity)
            stopPlayback();velocity=velocity or 0.8
            if playNote(i,session.editTrack,note,velocity) then send('on',{channel=session.editTrack,note=note,velocity=velocity}) end
        end,
        noteOff=function(note) releaseNote(i,session.editTrack,note);send('off',{channel=session.editTrack,note=note}) end,
        panic=function() stopPlayback();silence(i);send('panic') end,
        change=function(name,value)
            if track and name=='polyphony' then value=math.min(8,value);track.params[name]=value end
            local channel=session.editTrack
            local audio=channelAudio(i,channel,false)
            if audio then
                local level=channel>0 and i.channels[channel].level or 1
                audio:set(name,name=='volume' and value*level or (name=='polyphony' and channel>0 and math.min(8,value) or value))
            end
            if track then track.presetId='';w:touch('sound:'..session.editTrack..':'..name) end
            dirty=true
        end,
        command=function(op,args)
            flushEdit()
            if track and (op=='load' or op=='reset') then
                editor:releaseAll()
                if op=='reset' then track.params=schema.state({polyphony=8});track.presetId='';w:configure(session.editTrack);w:touch()
                else w:preset(session.editTrack,args.presetId) end
                editor:state(track.params,args and args.presetId);return
            end
            if op=='load' or op=='reset' then stopPlayback() end
            args=args or {};args.channel=session.editTrack
            send(op,args)
        end,
        hide=hideEditor,close=function() closeEditor(true) end,
        studio=function() switchPage('studio') end,
        meter=function() local audio=channelAudio(i,session.editTrack,false);return audio and audio.peak or 0,audio and audio.voices or 0 end,
        midiLabel=songLabel,
        midiCycle=function(step) stopPlayback();if #songs>0 then selectedSong=(selectedSong-1+step)%#songs+1 end end,
        midiPlay=function() playSong() end,midiStop=function() stopPlayback(true) end,
    })
    local view=session.views[session.editTrack] or session.editTrack==0 and session.view
    if view then
        editor.name=view.name;editor.selected=view.selected;editor.page=view.page
        editor.bank=view.bank;editor.browserPage=view.browserPage;editor:render()
    end
    editor:catalog(w.catalog)
    if track and not view then editor.name=w:presetName(track);editor.selected=track.presetId;editor:refreshName() end
    return true
end
local function message(data)
    if not mp.isConnected() or type(data)~='table' or data.version~=1 then return end
    if transport:receive(data,instances) then return end
    if data.op=='error' then ui.showMessage(data.message); return end
    if data.op=='dawSaved' then
        if session and data.id==session.id and data.epoch==session.epoch then
            local w=session.workstation
            if data.saveRevision==w.generation then w.dirty=false end
            ui.showMessage(w.dirty and 'Project snapshot saved; newer edits remain unsaved.' or 'Project saved to your character.')
        end
        return
    end
    if data.op=='dawProject' then if session and data.id==session.id and data.epoch==session.epoch then session.editTrack=0;session.workstation:load(data.project) end;return end
    if data.op=='catalog' then
        if session then session.workstation:setCatalog(data.entries) end
        if session then
            session.view=session.view or {name='Untitled'}
            session.view.entries=data.entries or {}
        end
        if editor and editor.kind~='studio' then editor:catalog(data.entries) end
        return
    end
    if data.op=='saved' then
        local channel=data.channel or 0
        if session then
            if channel>0 then
                local t=session.workstation.project.tracks[channel]
                if t then
                    if data.action=='save' or data.action=='saveAs' then t.presetId=data.presetId
                    elseif data.deleted and t.presetId==data.presetId then t.presetId='' end
                    session.workstation:touch()
                end
            end
            local view=session.views[channel] or (channel==0 and session.view) or {name='Untitled',entries={}}
            view.selected=not data.deleted and data.presetId or nil;session.views[channel]=view
            if channel==0 then session.view=view end
        end
        if editor and editor.kind~='studio' and session and session.editTrack==channel then editor:saved(data.presetId,data.deleted) end
        ui.showMessage(data.deleted and 'Preset deleted.' or 'Preset saved.');return
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
        i={id=data.id,audio=audio,source=source,cell=self.cell,owner=data.owner,notes={},channels={},epoch=data.epoch,seq=data.seq,seen=core.getRealTime()}
        instances[data.id]=i;apply(i,params)
        for n,channel in pairs(data.channels or {}) do
            local state=schema.state(channel.params)
            if type(n)=='number' and n>=1 and n<=16 and n%1==0 and state then applyChannel(i,n,state,channel.level or 1) end
        end
        if data.op=='granted' then
            closeEditor(true)
            session={id=data.id,epoch=data.epoch,inSeq=0,editTrack=0,views={},page='synth',transportLast=core.getRealTime()}
            session.workstation=makeWorkstation(i)
            showEditor()
            send('list'); lastHeartbeat=core.getRealTime()
        end
        return
    end
    if not i or i.epoch~=data.epoch then return end
    i.seq=data.seq; i.seen=core.getRealTime(); i.audio:keepAlive()
    local channel=data.channel or 0
    if data.op=='channelRemove' then
        local old=i.channels[channel]
        if old and old.audio then old.audio:close() end;i.channels[channel]=nil
        for key in pairs(i.notes) do if math.floor(key/128)==channel then i.notes[key]=nil end end
    elseif data.op=='channel' then
        local params=schema.state(data.params)
        if params and channel>=1 and channel<=16 and channel%1==0 then applyChannel(i,channel,params,data.level) end
    elseif data.op=='on' then playNote(i,channel,data.note,data.velocity)
    elseif data.op=='off' then releaseNote(i,channel,data.note)
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
                if editor and editor.kind~='studio' and session.editTrack==0 then editor:state(params,data.presetId) end
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
        transport.offset=nil;transport.rtt=nil;transport.lastClock=-10;transport.downloads={};transport.pings={};transport.samples=0
        for id in pairs(instances) do destroy(id) end
        closeEditor(false); connected=false; return
    end
    if not connected or now-lastSync>5 then
        connected=true; lastSync=now
        -- Request only when there are no instances: a snapshot never restarts held notes.
        if next(instances)==nil then mp.sendToServer('Synth_Request',{version=1,op='sync'}) end
    end
    if synth.createStudio then transport:frame(instances) end
    for id,i in pairs(instances) do
        if not i.source:isValid() or self.cell~=i.cell or now-i.seen>7 or i.audio.closed then destroy(id)
        else
            if now-i.seen<5 then
                i.audio:keepAlive()
                for _,channel in pairs(i.channels) do if channel.audio then channel.audio:keepAlive() end end
            end
            if mp.getPlayerObject and i.owner then
                local actor=mp.getPlayerObject(i.owner)
                if i.pose and i.pose.actor~=actor then
                    if not i.pose.missing then pcall(i.pose.clear,i.pose) end
                    i.pose=nil
                end
                if actor and actor:isValid() then
                    if not i.pose then i.pose=Performance.new(actor) end
                    if i.pose and not i.pose.missing then
                        local notes=i.notes;local strikes=i.strikes or 0
                        if i.studio then notes=i.studio:notes();for key in pairs(i.notes) do notes[key]=true end;strikes=strikes+i.studio.strikes end
                        i.pose:frame(dt,i.source,notes,now,strikes)
                    end
                end
            end
        end
    end
    if not session then return end
    local i=instances[session.id]
    if not i then closeEditor(true); return end
    -- The owner receives server liveness too; disconnected transport cannot sustain notes indefinitely.
    if now-lastHeartbeat>=2 then send('heartbeat'); lastHeartbeat=now end
    if dirty and now-lastEdit>=0.1 then flushEdit();lastEdit=now end
    if loading then
        local ok,result=coroutine.resume(loading)
        if not ok then loading=nil;ui.showMessage('MIDI: '..tostring(result))
        elseif coroutine.status(loading)=='dead' then loading=nil;startSong(result,loadIntent) end
    end
    if playback then
        local current=playback;current:update(dt)
        if not current.playing and playback==current then playback=nil end
    end
    local transportDt=math.max(0,now-session.transportLast);session.transportLast=now
    session.workstation:frame(transportDt)
    if editor then editor:frame(now) end
end
return {
    interfaceName='ProceduralSynth',
    interface={
        version=6,
        status=function()
            local out={}
            for id,i in pairs(instances) do
                local voices,peak=i.audio.voices,i.audio.peak
                local channels={}
                for _,channel in pairs(i.channels) do if channel.audio then voices=voices+channel.audio.voices;peak=math.max(peak,channel.audio.peak) end end
                for n,channel in pairs(i.channels) do channels[#channels+1]={id=n,voices=channel.audio and channel.audio.voices or 0,peak=channel.audio and channel.audio.peak or 0,wave1=channel.params.wave1,cutoff=channel.params.cutoff,
                    instrumentType=channel.params.instrumentType,driveType=channel.params.driveType,drive=channel.params.drive,cabinet=channel.params.cabinet,stringMute=channel.params.stringMute} end
                local w=session and session.id==id and session.workstation
                local phaseError=0
                if i.studio and i.transport and transport.offset then
                    phaseError=i.studio.audiblePosition-math.max(0,core.getRealTime()+transport.offset-i.transport.started)
                    if i.transport.loop then local period=i.transport.length/48000;phaseError=(phaseError+period/2)%period-period/2 end
                end
                out[#out+1]={id=id,owner=i.owner,voices=voices,peak=peak,
                editing=session~=nil and session.id==id,editorVisible=editor~=nil and session.id==id,epoch=i.epoch,seq=i.seq,
                cutoff=i.params.cutoff,wave1=i.params.wave1,poseBones=i.pose and i.pose.applied or 0,
                poseError=i.pose and i.pose.error or 0,midi=(playback~=nil and playback.playing) or (w and w.playing and w.mode=='song') or false,
                channels=channels,tracks=w and #w.project.tracks or 0,transport=w and w.playing or false,transportTime=w and w.time or 0,
                nativePlaying=i.studio and i.studio.playing or false,nativePosition=i.studio and i.studio.position or 0,
                nativeStrikes=i.studio and i.studio.strikes or 0,nativeFault=i.studio and i.studio.fault or 0,
                phaseError=phaseError,clockRtt=transport.rtt or -1} end
            return out
        end,
        noteOn=function(note,velocity) if editor and editor.kind~='studio' then editor:note(note,true,velocity) end end,
        noteOff=function(note) if editor and editor.kind~='studio' then editor:note(note,false) end end,
        setParameter=function(name,value)
            if editor and editor.kind~='studio' and schema.valid(name,value) then
                editor.params[name]=value; dirty=true
                editor.cb.change(name,value)
                if editor.controls[name] then editor.controls[name]() end
                editor:plots()
                return true
            end
            return false
        end,
        presetCommand=function(op,args) if editor and editor.kind~='studio' then editor.cb.command(op,args) end end,
        close=function() closeEditor(true) end,
        hide=hideEditor,
        show=showEditor,
        midiSongs=function() local out={};for n,p in ipairs(songs) do out[n]=p end;return out end,
        saveProject=function() return session and session.workstation:save() end,
        loadProject=function() if session then stopPlayback();send('dawLoad') end end,
        studio=function() switchPage('studio') end,
        sound=function(index)
            if session and type(index)=='number' and index%1==0 and index>=0 and index<=#session.workstation.project.tracks then switchPage('synth',index);return true end
            return false
        end,
        workstation=function() return session and session.workstation end,
        importMidi=function(path) return playSong(path,'import') end,
        playMidi=playSong,
        stopMidi=function() stopPlayback(true) end,
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
