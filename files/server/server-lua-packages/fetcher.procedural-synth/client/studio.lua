local ui=require('openmw.ui')
local util=require('openmw.util')
local async=require('openmw.async')
local input=require('openmw.input')
local I=require('openmw.interfaces')
local Controls=require('scripts.multiplayer.fetcher.procedural-synth.client.controls')
local schema=require('scripts.multiplayer.fetcher.procedural-synth.shared.schema')
local Window=require('scripts.multiplayer.fetcher.procedural-synth.client.window')
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
    content[#content+1]=label('PART / TRACK',28,169,240,12,c.muted)
    content[#content+1]=label('SAVED SOUND',295,169,225,12,c.muted)
    content[#content+1]=label('LEVEL',554,169,90,12,c.muted)
    self.meters={}
    for row=1,8 do
        local index=page*8+row;local t=p.tracks[index]
        if t then
            local y=198+(row-1)*45
            content[#content+1]=self:button(index..'  '..t.name:sub(1,30),20,y,265,function() w.selected=index;w.revision=w.revision+1 end,index==w.selected)
            content[#content+1]=self:button('<',294,y,24,function() w:cyclePreset(index,-1) end)
            content[#content+1]=label(w:presetName(t),326,y+4,173)
            content[#content+1]=self:button('>',505,y,24,function() w:cyclePreset(index,1) end)
            content[#content+1]=self:slider(546,y+5,94,t.level,function(value) w:setTrack(index,'level',value) end)
            content[#content+1]=self:button('M',650,y,30,function() w:setTrack(index,'mute',not t.mute) end,t.mute)
            content[#content+1]=self:button('S',686,y,30,function() w:setTrack(index,'solo',not t.solo) end,t.solo)
            content[#content+1]=self:button('Sound',726,y,57,function() self.cb.sound(index) end)
            content[#content+1]=self:button('Roll',790,y,45,function() w.selected=index;w.page='piano';w.bar=0;w.revision=w.revision+1 end)
            content[#content+1]=self:button('Del',843,y,42,function() w:remove(index) end)
            local meter=self:element(rect(295,y+31,1,3,c.accent));content[#content+1]=meter;self.meters[index]=meter
        end
    end
    content[#content+1]=self:button('+ Piano clip',20,578,120,function() if w:add('piano') then w.page='piano' end end)
    content[#content+1]=self:button('+ Drum lane',150,578,120,function() if w:add('drum') then w.page='drums' end end)
    content[#content+1]=label('MASTER',302,582,72,12,c.muted)
    content[#content+1]=self:slider(380,584,190,p.master,function(value) p.master=value;w:configureAll();w:touch() end)
    content[#content+1]=self:button('Tracks 1-8',620,578,110,function() w.mixerPage=0;w.revision=w.revision+1 end,page==0)
    content[#content+1]=self:button('Tracks 9-16',740,578,120,function() w.mixerPage=1;w.revision=w.revision+1 end,page==1)
    local t=p.tracks[w.selected]
    content[#content+1]=label(t and ('Selected: '..t.name..'  |  Output: keyboard in the world') or 'Import MIDI to list its parts, or add piano and drum tracks.',20,623,870,14,c.accent)
    content[#content+1]=label('Each part has its own sound and level. M = mute, S = solo. Sound opens that track in the synth editor.',20,653,880,12,c.muted)
end
function S:piano(content)
    local w,p=self.w,self.w.project;local t=p.tracks[w.selected]
    content[#content+1]=self:button('< track',20,169,70,function() self:select(-1) end)
    content[#content+1]=label(t and t.name or 'No track selected',104,174,450,15,c.accent)
    content[#content+1]=self:button('track >',575,169,70,function() self:select(1) end)
    content[#content+1]=self:button('+ Piano',660,169,86,function() w:add('piano') end)
    if not t then return end
    local spans=t.kind=='midi' and w.spans and w.spans[t.part] or t.notes
    spans=spans or {}
    local bars=p.length/16
    if t.kind=='midi' then for _,n in ipairs(spans) do bars=math.max(bars,math.ceil((n.s+n.l)/16)) end end
    w.bar=math.min(w.bar,bars-1)
    local gx,gy,gw,rh=76,219,self.width-106,27;local cw=gw/16
    for beat=0,3 do content[#content+1]=label(tostring(beat+1),gx+beat*4*cw,gy-19,40,11,c.muted) end
    for row=0,11 do
        local note=w.base+11-row
        content[#content+1]=label(pitch(note),20,gy+row*rh+4,52,13,c.muted)
        for column=0,15 do
            local step=w.bar*16+column
            local color=names[note%12+1]:find('#') and c.bg or c.panel
            local cell=rect(gx+column*cw,gy+row*rh,cw-2,rh-2,color)
            if t.kind=='piano' then cell.events={mouseClick=cb(function() w:toggleNote(w.selected,note,step) end)} end
            content[#content+1]=cell
        end
    end
    local drawn=0
    for _,n in ipairs(spans) do
        if n.n>=w.base and n.n<w.base+12 and n.s<w.bar*16+16 and n.s+n.l>w.bar*16 then
            drawn=drawn+1;if drawn>512 then break end
            local left=math.max(n.s,w.bar*16);local right=math.min(n.s+n.l,w.bar*16+16)
            local r=rect(gx+(left-w.bar*16)*cw,gy+(w.base+11-n.n)*rh,math.max(2,(right-left)*cw-2),rh-3,c.note)
            if t.kind=='piano' then r.events={mouseClick=cb(function() w:toggleNote(w.selected,n.n,n.s) end)} end
            content[#content+1]=r
        end
    end
    self.playhead=self:element(rect(gx,gy-5,3,4,c.accent));self.grid={x=gx,y=gy-5,cw=cw};content[#content+1]=self.playhead
    self:barControls(content,558,bars)
    content[#content+1]=self:button('Oct -',20,599,62,function() w.base=math.max(0,w.base-12);w.revision=w.revision+1 end)
    content[#content+1]=self:button('Oct +',90,599,62,function() w.base=math.min(116,w.base+12);w.revision=w.revision+1 end)
    content[#content+1]=self:button('Len -',290,599,57,function() w.noteLength=math.max(1,w.noteLength-1);w.revision=w.revision+1 end)
    content[#content+1]=label(w.noteLength..' steps',356,604,88)
    content[#content+1]=self:button('Len +',450,599,57,function() w.noteLength=math.min(16,w.noteLength+1);w.revision=w.revision+1 end)
    content[#content+1]=label(t.kind=='midi' and 'Imported MIDI view. Add a Piano clip to write editable notes alongside it.' or 'Click a cell to add a note; click a note to remove it. Length and velocity apply to new notes.',20,650,875,12,c.muted)
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
    self.controlEpoch=(self.controlEpoch or 0)+1
    self.typing=false
    if self.root then self.root:destroy() end
    for _,e in ipairs(self.children) do e:destroy() end
    self.children={};self.meters={};self.playhead=nil;self.grid=nil
    local w,p=self.w,self.w.project;local content={label('OPENMW / STUDIO',20,13,275,21)}
    local tabs={{'Synth',self.cb.synth},{'Mixer',function() w.page='mixer';w.revision=w.revision+1 end},
        {'Piano',function() w.page='piano';w.revision=w.revision+1 end},{'Drums',function() w.page='drums';w.revision=w.revision+1 end}}
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
    content[#content+1]=label(self.cb.midiLabel(),374,117,347,12,c.muted)
    content[#content+1]=self:button('MIDI >',726,112,65,function() self.cb.cycleMidi(1);w.revision=w.revision+1 end)
    content[#content+1]=self:button('Import',803,112,92,self.cb.import)
    if w.page=='piano' then self:piano(content) elseif w.page=='drums' then self:drums(content) else self:mixer(content) end
    self.root=self.window:root(content);self.revision=w.revision
end
function S.new(workstation,callbacks)
    local self=setmetatable({kind='studio',w=workstation,cb=callbacks,window=Window.new(),children={},width=940,lastFrame=-1},S)
    self:render();I.UI.addMode('Interface',{windows={}});return self
end
function S:releaseAll() end
function S:key(code,on)
    if on and code==input.KEY.Escape then self.cb.hide() end
    if on and not self.typing and input.isCtrlPressed and input.isCtrlPressed() then
        if code==input.KEY.Z then if input.isShiftPressed() then self.w:redo() else self.w:undo() end
        elseif code==input.KEY.Y then self.w:redo() end
    end
end
function S:frame(now)
    self.window:frame(self)
    if I.UI.getMode()~='Interface' then self.cb.hide();return end
    if self.revision~=self.w.revision then self:render() end
    if now-self.lastFrame<0.1 then return end;self.lastFrame=now
    self.transport.layout.props.text=string.format('%s  |  %.1fs  |  Beat %.1f  |  %s%s',self.w.playing and 'PLAY' or 'STOP',self.w.time,self.w.beat+1,self.w.project.name,self.w.dirty and ' *' or '')
    self.window:update(self.transport)
    for lane,meter in pairs(self.meters) do meter.layout.props.size=v(math.max(1,220*self.w.cb.meter(lane)),3);self.window:update(meter) end
    if self.playhead then
        self.playhead.layout.props.position=v(self.grid.x+math.max(0,self.w.step%16)*self.grid.cw,self.grid.y);self.window:update(self.playhead)
    end
end
function S:destroy()
    self.uiClosed=true
    self.root:destroy();for _,e in ipairs(self.children) do e:destroy() end
    if I.UI.getMode()=='Interface' then I.UI.removeMode('Interface') end
end
return S
