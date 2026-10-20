local ui=require('openmw.ui')
local util=require('openmw.util')
local async=require('openmw.async')
local input=require('openmw.input')
local I=require('openmw.interfaces')
local Controls=require('scripts.multiplayer.fetcher.procedural-synth.client.controls')
local schema=require('scripts.multiplayer.fetcher.procedural-synth.shared.schema')
local SoundSets=require('scripts.multiplayer.fetcher.procedural-synth.client.soundsets')
local Project=require('scripts.multiplayer.fetcher.procedural-synth.shared.project')(schema)
local Dropdown=require('scripts.multiplayer.fetcher.procedural-synth.client.dropdown')
local Window=require('scripts.multiplayer.fetcher.procedural-synth.client.window')
local PianoRoll=require('scripts.multiplayer.fetcher.procedural-synth.client.piano-roll')
local S={};S.__index=S
local v=util.vector2
local white=ui.texture{path='white'}
local c={bg=util.color.rgb(0.035,0.045,0.063),panel=util.color.rgb(0.075,0.09,0.12),track=util.color.rgb(0.14,0.17,0.21),
    accent=util.color.rgb(0.2,0.85,0.72),text=util.color.rgb(0.85,0.89,0.94),muted=util.color.rgb(0.5,0.58,0.68),note=util.color.rgb(0.35,0.65,0.95)}
local function rect(x,y,w,h,color) return {type=ui.TYPE.Image,props={position=v(x,y),size=v(w,h),resource=white,color=color}} end
local function label(text,x,y,w,size,color)
    return {type=ui.TYPE.Text,props={text=text,position=v(x,y),size=v(w,24),textSize=size or 13,textColor=color or c.text}}
end
local function cb(fn) return async:callback(function(...) fn(...);return false end) end
local names={'C','C#','D','D#','E','F','F#','G','G#','A','A#','B'}
local function pitch(n) return names[n%12+1]..(math.floor(n/12)-1) end
function S:button(text,x,y,w,fn,on)
    local r=rect(x,y,w,27,on and c.accent or c.track)
    r.content=ui.content{label(text,6,5,w-10,12,on and c.bg or c.text)};r.events={mouseClick=cb(fn)};return r
end
function S:element(layout)
    local e=self.window:create(layout);self.children[#self.children+1]=e;return e
end
function S:slider(x,y,width,value,fn)
    local r=rect(x,y,width,14,c.track);r.content=ui.content{rect(0,0,math.max(1,width*value),14,c.accent)}
    local function set(e) fn(math.max(0,math.min(1,e.offset.x/(width*self.window.scale)))) end
    r.events={mousePress=cb(set),mouseMove=cb(function(e) if e.button==1 then set(e) end end)};return r
end
function S:select(delta)
    self:releaseAll()
    local n=#self.w.project.tracks;if n>0 then self.w.selected=(self.w.selected-1+delta)%n+1;self.w.revision=self.w.revision+1 end
end
function S:barControls(content,y,maxBar)
    local w=self.w
    content[#content+1]=self:button('< bar',20,y,62,function() w.bar=math.max(0,w.bar-1);w.revision=w.revision+1 end)
    content[#content+1]=label('Bar '..(w.bar+1)..' / '..maxBar,90,y+4,120)
    content[#content+1]=self:button('bar >',205,y,62,function() w.bar=math.min(maxBar-1,w.bar+1);w.revision=w.revision+1 end)
    local velocity=Controls.slider(self,'velocity','Velocity',{min=0.1,max=1,unit='%',step=0.01},function() return w.velocity end,function(value) w.velocity=value end,292,y-6,180)
    content[#content+1]=velocity
end
function S:mixer(content)
    local w,p=self.w,self.w.project
    local page=w.mixerPage or 0
    content[#content+1]=self:button('<',20,150,28,function() w:cycleSoundSet(-1) end)
    content[#content+1]=self:button('Sound set: '..w:soundSetName()..' v',55,150,300,function()
        local choices={};for _,s in ipairs(SoundSets.entries) do choices[#choices+1]={id=s.id,label=s.name} end
        Dropdown.open(self,choices,p.soundSet,function(id) w:applySoundSet(id) end,55,181,590)
    end)
    content[#content+1]=self:button('>',362,150,28,function() w:cycleSoundSet(1) end)
    content[#content+1]=label('Changes sounds on all tracks. Undo restores the previous set.',410,155,480,11,c.muted)
    content[#content+1]=label('PART / TRACK',28,183,240,10,c.muted)
    content[#content+1]=label('SAVED SOUND',295,183,225,10,c.muted)
    content[#content+1]=label('LEVEL',554,183,90,10,c.muted)
    self.meters={}
    for row=1,8 do
        local index=page*8+row;local t=p.tracks[index]
        if t then
            local y=210+(row-1)*43
            content[#content+1]=self:button(index..'  '..t.name:sub(1,30),20,y,265,function() w.selected=index;w.revision=w.revision+1 end,index==w.selected)
            content[#content+1]=self:button('<',294,y,24,function() w:cyclePreset(index,-1) end)
            content[#content+1]=self:button(w:presetName(t)..' v',326,y,173,function()
                local choices={};for _,p in ipairs(w.catalog) do choices[#choices+1]={id=p.id,label=p.name,search=p.name..' '..(p.bank or '')..' '..(p.role or '')} end
                Dropdown.open(self,choices,t.presetId,function(id) w:preset(index,id) end,294,math.min(y+29,290),590)
            end)
            content[#content+1]=self:button('>',505,y,24,function() w:cyclePreset(index,1) end)
            content[#content+1]=self:slider(546,y+5,94,t.level,function(value) w:setTrack(index,'level',value) end)
            content[#content+1]=self:button('M',650,y,30,function() w:setTrack(index,'mute',not t.mute) end,t.mute)
            content[#content+1]=self:button('S',686,y,30,function() w:setTrack(index,'solo',not t.solo) end,t.solo)
            content[#content+1]=self:button('Sound',726,y,57,function() self.cb.sound(index) end)
            content[#content+1]=self:button('Mix',790,y,45,function() w.selected=index;w.page='channel';w.revision=w.revision+1 end)
            content[#content+1]=self:button('Del',843,y,42,function() w:remove(index) end)
            local meter=self:element(rect(295,y+31,1,3,c.accent));content[#content+1]=meter;self.meters[index]=meter
            local text=self:element(label('',546,y+29,340,10,c.muted));self.meterLabels[index]=text;content[#content+1]=text
        end
    end
    content[#content+1]=self:button('+ Piano clip',20,578,120,function() if w:add('piano') then w.page='piano' end end)
    content[#content+1]=self:button('+ Drum lane',150,578,120,function() if w:add('drum') then w.page='drums' end end)
    content[#content+1]=label('MASTER',302,582,72,12,c.muted)
    content[#content+1]=self:slider(380,584,190,p.master,function(value) p.master=value;w:configureAll();w:touch() end)
    content[#content+1]=self:button('Tracks 1-8',620,578,110,function() w.mixerPage=0;w.revision=w.revision+1 end,page==0)
    content[#content+1]=self:button('Tracks 9-16',740,578,120,function() w.mixerPage=1;w.revision=w.revision+1 end,page==1)
    local t=p.tracks[w.selected]
    content[#content+1]=label(t and ('Selected: '..t.name..'  |  Output: '..((t.output.slot or 0)==0 and 'This keyboard' or p.band[t.output.slot].name)) or 'Import MIDI to list its parts, or add piano and drum tracks.',20,623,870,14,c.accent)
    content[#content+1]=self:button('Delay / Reverb',705,614,180,function() w.page='returns';w.revision=w.revision+1 end)
    self:addMasterMeter(content,650)
end
function S:band(content)
    local w,p=self.w,self.w.project;local page=w.bandPage or 0
    content[#content+1]=label('BAND MEMBERS / INSTRUMENTS & PERFORMERS',20,158,500,13,c.accent)
    content[#content+1]=label('TRACK OUTPUTS',550,158,340,13,c.accent)
    content[#content+1]=label('Name                 Instrument                     Level        Mute',20,181,510,10,c.muted)
    local function update(slot,key,value)
        local old=p.band[slot];local m={name=old.name,target=old.target,level=old.level,mute=old.mute};m[key]=value
        w:setMember(slot,m)
    end
    for slot,m in ipairs(p.band) do
        local y=205+(slot-1)*42
        local name=self:element{type=ui.TYPE.TextEdit,props={position=v(20,y),size=v(98,27),text=m.name,textSize=11,textColor=c.text},
            events={textChanged=cb(function(text) update(slot,'name',text:gsub('%c',''):sub(1,48)) end),
                focusGain=cb(function() self.typing=true end),focusLoss=cb(function() self.typing=false end)}}
        content[#content+1]=name
        content[#content+1]=self:button(m.target and ((m.target.kind=='player' and 'Player #'..m.target.characterId or m.target.kind=='npc' and 'NPC #'..m.target.mpNum or 'Keyboard #'..m.target.mpNum)..(m.target.instrument and ' / '..m.target.instrument or '')..' v') or 'Bind performer v',128,y,180,function()
            local choices={{id=0,label='Unbound (silent)'},{id=-1,label='Create owned NPC performer (up to 8)'}};local targets={}
            if m.target and m.target.kind=='npc' then choices[#choices+1]={id=-2,label='Dismiss this owned NPC'} end
            for _,entry in ipairs(w.cb.instruments and w.cb.instruments() or {}) do
                choices[#choices+1]={id=entry.id,label=entry.label};targets[entry.id]=entry.target
            end
            Dropdown.open(self,choices,m.target and Project.targetKey(m.target) or 0,function(id)
                if id==-1 then if w.cb.hire then w.cb.hire(slot) end;return end
                if id==-2 then if w.cb.dismiss then w.cb.dismiss(slot) end;return end
                update(slot,'target',targets[id]);w.revision=w.revision+1
            end,128,math.min(y+29,290),575)
        end)
        content[#content+1]=self:slider(320,y+6,110,m.level,function(value) update(slot,'level',value) end)
        content[#content+1]=self:button('M',440,y,32,function() update(slot,'mute',not m.mute);w.revision=w.revision+1 end,m.mute)
        if m.target and m.target.kind=='player' then content[#content+1]=self:button('Invite',478,y,47,function() if w.cb.invite then w.cb.invite(slot) end end) end
        local status=w.cb.bandStatus and w.cb.bandStatus(slot) or 'Unbound'
        content[#content+1]=label(status,130,y+27,370,10,status=='Ready' and c.accent or c.muted)
        local index=page*8+slot;local track=p.tracks[index]
        if track then
            content[#content+1]=label(index..'  '..track.name:sub(1,21),550,y+5,170,11)
            local output=track.output.slot or 0
            local title=output==0 and 'This keyboard' or p.band[output].name
            content[#content+1]=self:button(title..' v',725,y,168,function()
                local choices={{id=0,label='This keyboard'}}
                for n,member in ipairs(p.band) do choices[#choices+1]={id=n,label=n..': '..member.name} end
                Dropdown.open(self,choices,output,function(id) w:setOutput(index,id) end,520,math.min(y+29,290),380)
            end)
        end
    end
    content[#content+1]=self:button('Refresh instruments',20,566,190,function() if w.cb.refreshBand then w.cb.refreshBand() end;w.revision=w.revision+1 end)
    content[#content+1]=self:button('Tracks 1-8',550,566,140,function() w.bandPage=0;w.revision=w.revision+1 end,page==0)
    content[#content+1]=self:button('Tracks 9-16',705,566,150,function() w.bandPage=1;w.revision=w.revision+1 end,page==1)
    content[#content+1]=label('Bind keyboards, invite players, or create owned NPCs. Players use F9 to ready an instrument and accept.',20,609,880,12,c.muted)
    content[#content+1]=label('Changing a binding or track output stops playback. Missing or busy members stay silent.',20,638,880,12,c.muted)
    content[#content+1]=label('Save stores routing. Consent is session-only. Leave / Not ready in F9 immediately silences your part.',20,667,880,12,c.accent)
end
local function db(value) return value>0.00001 and string.format('%.1f',20*math.log(value)/math.log(10)) or '-inf' end
function S:addMasterMeter(content,y)
    local text=self:element(label('',20,y,890,12,c.accent));self.meterLabels[0]=text;content[#content+1]=text
end
function S:channel(content)
    local w=self.w;local t=w.project.tracks[w.selected]
    content[#content+1]=self:button('< track',20,169,75,function() self:select(-1) end)
    content[#content+1]=label(t and ('MIX / '..t.name) or 'Select a track in Mixer',110,174,470,16,c.accent)
    content[#content+1]=self:button('track >',610,169,75,function() self:select(1) end)
    content[#content+1]=self:button('Back to mixer',710,169,175,function() w.page='mixer';w.revision=w.revision+1 end)
    if not t then return end
    local m=t.mixer or Project.mixer();t.mixer=m
    for n,entry in ipairs{{'EQ','eqEnabled',20},{'Compressor','compEnabled',475}} do
        local title,key,x=entry[1],entry[2],entry[3]
        content[#content+1]=label(title,x,218,260,18,c.accent)
        content[#content+1]=self:button(m[key]==1 and 'On' or 'Bypassed',x+290,212,120,function()
            w:setMixer(w.selected,key,1-m[key]);w.revision=w.revision+1
        end,m[key]==1)
    end
    local controls={
        {'highpass','High-pass (Hz)',20,260,'Hz',true,1}, {'bass','Bass shelf / 120 Hz (dB)',20,321,'dB',false,0.1},
        {'mid','Mid bell (dB)',20,382,'dB',false,0.1}, {'midFrequency','Mid frequency (Hz)',20,443,'Hz',true,10},
        {'treble','Treble shelf / 6 kHz (dB)',20,504,'dB',false,0.1},
        {'threshold','Threshold (dB)',475,260,'dB',false,0.1}, {'ratio','Ratio',475,321,'',true,0.1},
        {'attack','Attack (ms)',475,382,'ms',true,0.1}, {'release','Release (ms)',475,443,'ms',true,1},
        {'makeup','Makeup gain (dB)',475,504,'dB',false,0.1}}
    for _,entry in ipairs(controls) do
        local key,title,x,y,unit,log,step=entry[1],entry[2],entry[3],entry[4],entry[5],entry[6],entry[7];local spec=Project.mixerSpecs[key]
        content[#content+1]=Controls.slider(self,'mix:'..key,title,{min=spec[1],max=spec[2],unit=unit,log=log,step=step},
            function() return t.mixer[key] end,function(value) w:setMixer(w.selected,key,value) end,x,y,410)
    end
    local text=self:element(label('',20,552,865,14,c.accent));self.meterLabels[w.selected]=text;content[#content+1]=text
    for n,key in ipairs{'delaySend','reverbSend'} do
        content[#content+1]=Controls.slider(self,'send:'..key,n==1 and 'Delay send' or 'Reverb send',{min=0,max=1,unit='%',step=0.01},
            function() return t.mixer[key] end,function(value) w:setMixer(w.selected,key,value) end,20+(n-1)*455,603,265)
    end
    content[#content+1]=self:button('Returns',775,609,110,function() w.page='returns';w.revision=w.revision+1 end)
    content[#content+1]=label('Bypass preserves settings. Peak / RMS are dBFS; GR shows compressor reduction in dB.',20,658,890,10,c.muted)
    self:addMasterMeter(content,576)
end
function S:returns(content)
    local w=self.w;local e=w.project.effects
    content[#content+1]=label('SHARED DELAY / REVERB',20,174,570,18,c.accent)
    content[#content+1]=self:button('Back to mixer',710,169,175,function() w.page='mixer';w.revision=w.revision+1 end)
    for n,key in ipairs{'delayEnabled','reverbEnabled'} do
        local x=20+(n-1)*455
        content[#content+1]=label(n==1 and 'DELAY' or 'REVERB',x,220,265,18,c.accent)
        content[#content+1]=self:button(e[key]==1 and 'On' or 'Bypassed',x+290,214,120,function() w:setEffect(key,1-e[key]);w.revision=w.revision+1 end)
    end
    local controls={{'delayTime','Time (ms)',20,270,'ms',true,1},{'delayFeedback','Feedback',20,350,'%',false,0.01},
        {'delayReturn','Return level',20,430,'%',false,0.01},{'reverbDecay','Decay (seconds)',475,270,'s',true,0.05},
        {'reverbDamping','Damping (Hz)',475,350,'Hz',true,10},{'reverbReturn','Return level',475,430,'%',false,0.01}}
    for _,entry in ipairs(controls) do
        local key,title,x,y,unit,log,step=entry[1],entry[2],entry[3],entry[4],entry[5],entry[6],entry[7];local spec=Project.effectsSpecs[key]
        -- The shared numeric control displays seconds as milliseconds.
        if key=='reverbDecay' then title='Decay (ms)' end
        content[#content+1]=Controls.slider(self,'return:'..key,title,{min=spec[1],max=spec[2],unit=unit,log=log,step=step},
            function() return w.project.effects[key] end,function(value) w:setEffect(key,value) end,x,y,410)
    end
    content[#content+1]=label('Set sends with Mix on each track. Each physical output has its own delay and reverb tails.',20,535,890,13,c.muted)
    content[#content+1]=label('Each physical output: dry tracks + returns > master volume > member level > limiter',20,567,890,13,c.muted)
    content[#content+1]=label('Stop fades and clears effect tails. Delay time changes glide in pitch.',20,600,890,12,c.muted)
    self.returnMeter=self:element(label('',20,626,890,13,c.accent));content[#content+1]=self.returnMeter
    self:addMasterMeter(content,650)
end
function S:piano(content)
    local w,p=self.w,self.w.project;local t=p.tracks[w.selected]
    content[#content+1]=self:button('< track',20,169,70,function() self:select(-1) end)
    content[#content+1]=label(t and t.name or 'No track selected',104,174,450,15,c.accent)
    content[#content+1]=self:button('track >',575,169,70,function() self:select(1) end)
    content[#content+1]=self:button('+ Piano',660,169,86,function() w:add('piano') end)
    if not t then return end
    if t.kind=='drum' then content[#content+1]=label('Choose a piano or MIDI track, or use the Drums page for this lane.',20,250,880,15,c.muted);return end
    local snapNames={[0]='Off',[0.25]='1/64',[0.5]='1/32',[1]='1/16',[2]='1/8',[4]='1/4'}
    content[#content+1]=self:button('Snap: '..snapNames[w.snap],20,207,110,function()
        local values={0.25,0.5,1,2,4,0};local at=1;for n,q in ipairs(values) do if q==w.snap then at=n end end
        w.snap=values[at%#values+1];w.revision=w.revision+1
    end)
    content[#content+1]=self:button('Zoom -',140,207,78,function() self:zoomRoll(2) end)
    content[#content+1]=self:button('Zoom +',228,207,78,function() self:zoomRoll(0.5) end)
    content[#content+1]=label(string.format('%.1f bars in view',w.rollSteps/16),320,212,180,12,c.muted)
    if t.kind=='midi' then content[#content+1]=self:button(t.edited and 'Edited MIDI' or 'Edit MIDI',745,207,150,function() w:convertPart(w.selected) end,t.edited~=nil) end
    for row=0,11 do content[#content+1]=label(pitch(w.base+11-row),20,260+row*22+3,52,12,c.muted) end
    self.roll=PianoRoll.new(self,content)
    self.playhead=self:element(rect(76,255,3,4,c.accent));content[#content+1]=self.playhead
    content[#content+1]=self:button('Oct -',20,558,62,function() w.base=math.max(0,w.base-12);w.revision=w.revision+1 end)
    content[#content+1]=self:button('Oct +',90,558,62,function() w.base=math.min(116,w.base+12);w.revision=w.revision+1 end)
    content[#content+1]=self:button('< time',175,558,70,function() self.roll:pan(-w.rollSteps) end)
    content[#content+1]=self:button('time >',255,558,70,function() self.roll:pan(w.rollSteps) end)
    content[#content+1]=self:button('Len -',347,558,57,function() w.noteLength=math.max(0.25,w.noteLength-(w.snap>0 and w.snap or 0.25));w.revision=w.revision+1 end)
    content[#content+1]=label(string.format('%.2g steps',w.noteLength),412,563,95)
    content[#content+1]=self:button('Len +',510,558,57,function() w.noteLength=math.min(256,w.noteLength+(w.snap>0 and w.snap or 0.25));w.revision=w.revision+1 end)
    local function velocity()
        local id=w:selectedNotes(w.selected)[1];return id and w:noteData(w.selected)[id].v or w.velocity
    end
    local velocityControl,refreshVelocity=Controls.slider(self,'noteVelocity','Velocity',{min=0.01,max=1,unit='%',step=0.01},velocity,function(value)
        w.velocity=value;if #w:selectedNotes(w.selected)>0 then w:editSelected(w.selected,{velocity=value},'note:velocity:'..w.selected,true) end
    end,610,550,285)
    self.noteVelocityRefresh=refreshVelocity;content[#content+1]=velocityControl
    for n,entry in ipairs{{'Select all',function() w:selectAllNotes(w.selected);self.roll:draw() end},
        {'Copy',function() w:copyNotes(w.selected) end},{'Paste',function() w:pasteNotes(w.selected,self.roll.start) end},
        {'Duplicate',function() w:duplicateNotes(w.selected) end},{'Delete',function() w:deleteNotes(w.selected) end}} do
        content[#content+1]=self:button(entry[1],20+(n-1)*116,602,106,entry[2])
    end
    content[#content+1]=self:button('Record keyboard >',610,602,285,function() w.page='record';w.revision=w.revision+1 end)
    content[#content+1]=label(t.kind=='midi' and not t.edited and 'Read only: Edit MIDI creates saved notes; the original file stays intact.' or 'Click adds. Shift-click selects. Drag moves; drag the right edge resizes. Edits keep routing.',20,644,895,12,c.muted)
    content[#content+1]=label('Ctrl+A/C/V/D, Delete, arrow keys. Wheel: pitch; Shift-wheel: time; Ctrl-wheel: zoom.',20,670,895,12,c.muted)
end
function S:recordKey(note,on,velocity)
    self.recordHeld=self.recordHeld or {}
    if on then
        if self.recordHeld[note] or not self.w:recordOn(note,velocity) then return end
        self.recordHeld[note]=true
    else self.w:recordOff(note);self.recordHeld[note]=nil end
end
function S:record(content)
    local w=self.w;local t=w.project.tracks[w.selected];local base=w.recordBase or 60
    content[#content+1]=self:button('< track',20,169,70,function() self:select(-1) end)
    content[#content+1]=label(t and t.name or 'Add a piano track first',104,174,450,15,c.accent)
    content[#content+1]=self:button('track >',575,169,70,function() self:select(1) end)
    content[#content+1]=self:button('+ Piano',660,169,86,function() self:releaseAll();w:add('piano') end)
    content[#content+1]=self:button('Piano roll',755,169,140,function() w.page='piano';w.revision=w.revision+1 end)
    content[#content+1]=label('QUANTIZED KEYBOARD RECORDING',20,222,850,18,c.accent)
    content[#content+1]=self:button('Start take',20,265,150,function() w:startTake(w.selected) end)
    content[#content+1]=self:button('Keep take',185,265,150,function() w:endTake(false) end)
    content[#content+1]=self:button('Cancel take',350,265,150,function() w:endTake(true) end)
    if w.failedTake then content[#content+1]=self:button('Retry take',515,265,150,function() w:recoverTake() end) end
    self.recordText=self:element(label(w:recordStatus(),20,320,875,17,c.accent));content[#content+1]=self.recordText
    content[#content+1]=label('Stopped: one-bar visual count-in from the current piano-roll view. Playing: overdub the audible clock.',20,365,880,12,c.muted)
    content[#content+1]=label('Piano clips record to the next loop boundary. Edited MIDI records on the full song timeline.',20,393,880,12,c.muted)
    content[#content+1]=self:button('Oct -',20,430,80,function() w.recordBase=math.max(0,base-12);w.revision=w.revision+1 end)
    content[#content+1]=self:button('Oct +',110,430,80,function() w.recordBase=math.min(103,base+12);w.revision=w.revision+1 end)
    content[#content+1]=label('Grid: '..(w.snap>0 and (w.snap..' sixteenth steps') or 'Off')..'  |  Notes use the selected track sound and band destination.',210,436,680,12,c.muted)
    self.recordKeyboard={}
    for n=0,24 do
        local note=base+n;local black=names[note%12+1]:find('#')~=nil
        local key=rect(20+n*35,480,33,black and 85 or 120,black and c.track or c.text)
        key.content=ui.content{label(pitch(note),2,black and 59 or 94,32,10,black and c.text or c.bg)}
        key.events={mousePress=cb(function(e) if e.button==1 then self.mouseRecord=note;self:recordKey(note,true,math.max(0.2,math.min(1,e.offset.y/(120*self.window.scale)))) end end),
            mouseRelease=cb(function() self:recordKey(note,false);self.mouseRecord=nil end)}
        local element=self:element(key);self.recordKeyboard[note]={element=element,color=black and c.track or c.text};content[#content+1]=element
    end
    content[#content+1]=label('Play after count-in: Z S X D C V G B H N J M ,  |  mouse keys span two octaves.',20,628,880,12,c.muted)
    content[#content+1]=label('Keep commits one undo action. Cancel discards the take. Stop/Hide keep notes; Save before Finish.',20,660,880,12,c.muted)
end
function S:zoomRoll(factor)
    self.w.rollSteps=math.max(8,math.min(64,self.w.rollSteps*factor));self.w.revision=self.w.revision+1
end
function S:wheel(delta)
    if not self.roll or self.w.page~='piano' then return end
    if input.isCtrlPressed and input.isCtrlPressed() then self:zoomRoll(delta>0 and 0.5 or 2)
    elseif input.isShiftPressed and input.isShiftPressed() then self.roll:pan(-delta*self.w.rollSteps/4)
    else self.w.base=math.max(0,math.min(116,self.w.base+math.floor(delta*3)));self.w.revision=self.w.revision+1 end
end
function S:drums(content)
    local w,p=self.w,self.w.project
    local drums={};for n,t in ipairs(p.tracks) do if t.kind=='drum' then drums[#drums+1]={index=n,track=t} end end
    local page=w.drumPage or 0
    self:barControls(content,169,p.length/16)
    local gx,gy,cw=304,225,(self.width-334)/16
    for step=0,15 do content[#content+1]=label(tostring(step+1),gx+step*cw+6,204,cw,11,c.muted) end
    for row=1,8 do
        local entry=drums[page*8+row]
        if entry then
            local index,t=entry.index,entry.track;local y=gy+(row-1)*41
            content[#content+1]=self:button(t.name:sub(1,18),20,y,160,function() w.selected=index;w:audition(index);w.revision=w.revision+1 end,index==w.selected)
            content[#content+1]=self:button('-',190,y,25,function() w:setTrack(index,'note',math.max(0,t.note-1)) end)
            content[#content+1]=label(pitch(t.note),220,y+5,47)
            content[#content+1]=self:button('+',269,y,25,function() w:setTrack(index,'note',math.min(127,t.note+1)) end)
            for column=0,15 do
                local step=w.bar*16+column;local digit=t.steps:sub(step+1,step+1)
                local r=rect(gx+column*cw,y,cw-3,27,digit~='0' and c.accent or (column%4==0 and c.track or c.panel))
                r.content=ui.content{label(digit~='0' and digit or '',8,5,cw-8,12,c.bg)}
                r.events={mouseClick=cb(function() w:toggleStep(index,step) end)};content[#content+1]=r
            end
        end
    end
    self.playhead=self:element(rect(gx,gy-5,3,4,c.accent));self.grid={x=gx,y=gy-5,cw=cw};content[#content+1]=self.playhead
    local function add(preset,name,note)
        w:add('drum',preset,name,note)
    end
    content[#content+1]=self:button('+ Kick',20,578,90,function() add('b:kick','Kick',36) end)
    content[#content+1]=self:button('+ Snare',120,578,90,function() add('b:snare','Snare',38) end)
    content[#content+1]=self:button('+ Hat',220,578,90,function() add('b:hat','Hat',42) end)
    content[#content+1]=self:button('+ Open hat',320,578,110,function() add('b:openhat','Open hat',46) end)
    content[#content+1]=self:button('+ Kit',440,578,80,function() w:addKit(w.kitBank) end)
    content[#content+1]=self:button('Sound',530,578,110,function() if p.tracks[w.selected] then self.cb.sound(w.selected) end end)
    content[#content+1]=self:button('Lanes 1-8',650,578,100,function() w.drumPage=0;w.revision=w.revision+1 end,page==0)
    content[#content+1]=self:button('Lanes 9-16',760,578,110,function() w.drumPage=1;w.revision=w.revision+1 end,page==1)
    content[#content+1]=self:button('Kit: '..schema.kits[w.kitBank or 1].name,20,610,220,function()
        w.kitBank=(w.kitBank or 1)%#schema.kits+1;w.revision=w.revision+1
    end)
    content[#content+1]=label('Choose a kit, then + Kit. Sound opens presets and per-instrument effects.',260,616,640,12,c.accent)
    content[#content+1]=label('Play clips loops the piano and drum clips together. Play song also follows the imported MIDI tempo map.',20,657,880,12,c.muted)
end
function S:render()
    if self.lastPage=='record' and self.w.page~='record' then self:releaseAll() end;self.lastPage=self.w.page
    self.recordText=nil;self.recordKeyboard=nil
    self.controlEpoch=(self.controlEpoch or 0)+1;self.noteVelocityRefresh=nil
    if self.roll then self.roll:finish(true);self.roll=nil end
    Dropdown.close(self)
    self.controlEpoch=(self.controlEpoch or 0)+1
    self.typing=false
    if self.root then self.root:destroy() end
    for _,e in ipairs(self.children) do e:destroy() end
    self.children={};self.meters={};self.meterLabels={};self.returnMeter=nil;self.playhead=nil;self.grid=nil
    local w,p=self.w,self.w.project;local content={label('OPENMW / STUDIO',20,13,275,21)}
    local tabs={{'Synth',self.cb.synth},{'Mixer',function() w.page='mixer';w.revision=w.revision+1 end},
        {'Piano',function() w.page='piano';w.revision=w.revision+1 end},{'Drums',function() w.page='drums';w.revision=w.revision+1 end},{'Band',function() w.page='band';if w.cb.refreshTargets then w.cb.refreshTargets() end;w.revision=w.revision+1 end}}
    for n,tab in ipairs(tabs) do content[#content+1]=self:button(tab[1],330+(n-1)*77,12,70,tab[2],w.page==tab[1]:lower()) end
    content[#content+1]=self:button('Hide [F8]',self.width-198,12,104,self.cb.hide)
    content[#content+1]=self:button('Finish',self.width-84,12,64,self.cb.close)
    self.transport=self:element(label('',20,43,660,12,c.accent));content[#content+1]=self.transport
    content[#content+1]=self:button('Undo',714,41,80,function() w:undo() end,#w.undoStack>0)
    content[#content+1]=self:button('Redo',807,41,80,function() w:redo() end,#w.redoStack>0)
    content[#content+1]=self:button('Play song',20,70,96,function() self.cb.playSong() end)
    content[#content+1]=self:button('Play clips',125,70,96,function() self.cb.playClips() end)
    content[#content+1]=self:button('Stop',230,70,58,self.cb.stop)
    local tempo=Controls.slider(self,'bpm','BPM',{min=40,max=240,step=1},function() return p.bpm end,function(value) if not w.playing then p.bpm=value;w:touch('bpm',true) end end,309,64,180)
    content[#content+1]=tempo
    local swing=Controls.slider(self,'swing','Swing',{min=0,max=0.6,unit='%',step=0.01},function() return p.swing end,function(value) p.swing=value;w:touch('swing',true) end,504,64,113)
    content[#content+1]=swing
    content[#content+1]=self:button(p.length/16 ..' bars',630,70,70,function() w:length(p.length%256+16) end)
    content[#content+1]=self:button('Loop',712,70,61,function() p.loop=not p.loop;w:touch() end,p.loop)
    content[#content+1]=self:button('Save',784,70,54,function() w:save() end)
    content[#content+1]=self:button('Load',848,70,54,self.cb.load)
    local name=self:element{type=ui.TYPE.TextEdit,props={position=v(20,112),size=v(260,28),text=p.name,textSize=14,textColor=c.text},
        events={textChanged=cb(function(text) p.name=text:gsub('%c',''):sub(1,64);w:touch('name',true) end),focusGain=cb(function() self.typing=true end),focusLoss=cb(function() self.typing=false end)}}
    content[#content+1]=name
    content[#content+1]=self:button('< MIDI',299,112,65,function() self.cb.cycleMidi(-1);w.revision=w.revision+1 end)
    content[#content+1]=self:button(self.cb.midiLabel()..'  v',374,112,347,function()
        if self.cb.midiChoices then Dropdown.open(self,self.cb.midiChoices(),self.cb.midiSelected(),self.cb.midiSelect,30,145,850) end
    end)
    content[#content+1]=self:button('MIDI >',726,112,65,function() self.cb.cycleMidi(1);w.revision=w.revision+1 end)
    content[#content+1]=self:button('Import',803,112,92,self.cb.import)
    if w.page=='record' then self:record(content) elseif w.page=='piano' then self:piano(content) elseif w.page=='drums' then self:drums(content) elseif w.page=='channel' then self:channel(content) elseif w.page=='returns' then self:returns(content) elseif w.page=='band' then self:band(content) else self:mixer(content) end
    self.root=self.window:root(content);self.revision=w.revision
end
function S.new(workstation,callbacks)
    local self=setmetatable({kind='studio',w=workstation,cb=callbacks,window=Window.new(),children={},width=940,lastFrame=-1},S)
    self.window.pickup=callbacks.pickup;self:render();I.UI.addMode('Interface',{windows={}});return self
end
function S:releaseAll()
    if self.roll then self.roll:finish(true) end
    for note in pairs(self.recordHeld or {}) do self.w:recordOff(note) end
    self.recordHeld={};self.recordKeys={};self.mouseRecord=nil;self.w:endTake(false)
end
function S:key(code,on)
    self.recordKeys=self.recordKeys or {}
    if not on and self.recordKeys[code] then self:recordKey(self.recordKeys[code],false);self.recordKeys[code]=nil;return end
    if self.dropdown then if on and code==input.KEY.Escape then Dropdown.close(self) end;return end
    if on and code==input.KEY.Escape then self.cb.hide();return end
    if not on or self.typing then return end
    local ctrl=input.isCtrlPressed and input.isCtrlPressed()
    if self.w.page=='record' and not ctrl then
        for n,key in ipairs{input.KEY.Z,input.KEY.S,input.KEY.X,input.KEY.D,input.KEY.C,input.KEY.V,input.KEY.G,input.KEY.B,input.KEY.H,input.KEY.N,input.KEY.J,input.KEY.M,input.KEY.Comma} do
            if key==code and not self.recordKeys[code] then local note=(self.w.recordBase or 60)+n-1;self:recordKey(note,true);self.recordKeys[code]=note end
        end
        return
    end
    if ctrl then
        if self.roll then self.roll:finish(true) end
        if code==input.KEY.Z then if input.isShiftPressed and input.isShiftPressed() then self.w:redo() else self.w:undo() end
        elseif code==input.KEY.Y then self.w:redo()
        elseif self.roll then
            local w=self.w;local n=w.selected
            if code==input.KEY.A then w:selectAllNotes(n);self.roll:draw()
            elseif code==input.KEY.C then w:copyNotes(n)
            elseif code==input.KEY.V then w:pasteNotes(n,self.roll.start)
            elseif code==input.KEY.D then w:duplicateNotes(n) end
        end
    elseif self.roll then
        local w=self.w
        if code==input.KEY.Delete then w:deleteNotes(w.selected)
        elseif code==input.KEY.LeftArrow then w:editSelected(w.selected,{step=-(w.snap>0 and w.snap or 0.25)})
        elseif code==input.KEY.RightArrow then w:editSelected(w.selected,{step=w.snap>0 and w.snap or 0.25})
        elseif code==input.KEY.UpArrow then w:editSelected(w.selected,{pitch=1})
        elseif code==input.KEY.DownArrow then w:editSelected(w.selected,{pitch=-1}) end
    end
end
function S:frame(now)
    self.window:frame(self)
    Dropdown.frame(self)
    if I.UI.getMode()~='Interface' then self.cb.hide();return end
    if self.roll then self.roll:frame() end
    if self.mouseRecord and not input.isMouseButtonPressed(1) then self:recordKey(self.mouseRecord,false);self.mouseRecord=nil end
    if self.revision~=self.w.revision then self:render() end
    if now-self.lastFrame<0.1 then return end;self.lastFrame=now
    self.transport.layout.props.text=string.format('%s  |  %.1fs  |  Beat %.1f  |  %s%s',self.w.awaiting and 'BUFFER' or (self.w.playing and 'PLAY' or 'STOP'),self.w.time,self.w.beat+1,self.w.project.name,self.w.dirty and ' *' or '')
    self.window:update(self.transport)
    if self.recordText then self.recordText.layout.props.text=self.w:recordStatus();self.window:update(self.recordText) end
    for note,key in pairs(self.recordKeyboard or {}) do key.element.layout.props.color=self.recordHeld and self.recordHeld[note] and c.accent or key.color;self.window:update(key.element) end
    for lane,meter in pairs(self.meters) do meter.layout.props.size=v(math.max(1,220*math.max(0,math.min(1,(20*math.log(math.max(0.001,self.w.cb.meter(lane)))/math.log(10)+60)/60))),3);self.window:update(meter) end
    for n,text in pairs(self.meterLabels) do
        local peak,rms,reduction=0,0,0
        if self.w.cb.meters then peak,rms,reduction=self.w.cb.meters(n) end
        text.layout.props.text=n==0 and string.format('OUTPUT MAX  pre-limiter peak %s dBFS  |  RMS %s dBFS%s',db(peak),db(rms),peak>1 and '  |  LIMITING - lower levels' or '')
            or string.format('Peak %s / RMS %s dBFS  |  GR %.1f dB',db(peak),db(rms),reduction)
        self.window:update(text)
    end
    if self.returnMeter and self.w.cb.returnMeters then
        local delay,reverb=self.w.cb.returnMeters()
        self.returnMeter.layout.props.text='Delay return '..db(delay)..' dBFS  |  Reverb return '..db(reverb)..' dBFS'
        self.window:update(self.returnMeter)
    end
    if self.playhead and self.roll then
        local step=self.w.mode=='song' and self.w.beat*4 or self.w.beat*4%self.w.project.length
        self.playhead.layout.props.visible=step>=self.roll.start and step<self.roll.start+self.roll.steps
        self.playhead.layout.props.position=v(self.roll.x+(step-self.roll.start)*self.roll.cw,self.roll.y-5);self.window:update(self.playhead)
    elseif self.playhead then
        self.playhead.layout.props.position=v(self.grid.x+math.max(0,self.w.step%16)*self.grid.cw,self.grid.y);self.window:update(self.playhead)
    end
end
function S:destroy()
    self:releaseAll()
    Dropdown.close(self)
    self.uiClosed=true
    self.root:destroy();for _,e in ipairs(self.children) do e:destroy() end
    if I.UI.getMode()=='Interface' then I.UI.removeMode('Interface') end
end
return S
