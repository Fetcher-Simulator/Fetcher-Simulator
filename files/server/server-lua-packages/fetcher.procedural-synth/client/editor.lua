local ui=require('openmw.ui')
local util=require('openmw.util')
local async=require('openmw.async')
local input=require('openmw.input')
local I=require('openmw.interfaces')
local schema=require('scripts.multiplayer.fetcher.procedural-synth.shared.schema')
local v=util.vector2
local white=ui.texture{path='white'}
local E={}; E.__index=E
local colors={bg=util.color.rgb(0.035,0.045,0.063),panel=util.color.rgb(0.075,0.09,0.12),
    track=util.color.rgb(0.14,0.17,0.21),accent=util.color.rgb(0.2,0.85,0.72),
    text=util.color.rgb(0.85,0.89,0.94),muted=util.color.rgb(0.5,0.58,0.68)}
local waves={'Sine','Triangle','Saw','Square'}
local function rect(x,y,w,h,color)
    return {type=ui.TYPE.Image,props={position=v(x,y),size=v(w,h),resource=white,color=color}}
end
local function label(text,x,y,w,size,color)
    return {type=ui.TYPE.Text,props={text=text,position=v(x,y),size=v(w,22),textSize=size or 14,textColor=color or colors.text}}
end
local function callback(fn) return async:callback(function(...) fn(...); return false end) end
function E:element(layout)
    local element=ui.create(layout); self.children[#self.children+1]=element; return element
end
function E:button(text,x,y,w,fn)
    local r=rect(x,y,w,28,colors.track)
    r.content=ui.content{label(text,7,5,w-10,13)}
    r.events={mouseClick=callback(fn)}
    return r
end
function E:refreshName()
    local name=self.name or 'Untitled'
    self.nameElement.layout.props.text=name; self.nameElement:update()
    local selected='Choose a preset'
    for _,p in ipairs(self.entries) do if p.id==self.selected then selected=p.name end end
    self.selection.layout.props.text=selected; self.selection:update()
end
function E:catalog(entries)
    self.entries=entries or {}; self:refreshName()
end
function E:saved(id,deleted)
    self.selected=not deleted and id or nil
    self:refreshName()
end
function E:state(params,id)
    self:releaseAll(); self.params=params
    if id then
        self.selected=id
        for _,p in ipairs(self.entries) do if p.id==id then self.name=p.name end end
    end
    for name,control in pairs(self.controls) do control() end
    self:plots(); self:refreshName()
end
function E:releaseAll()
    for note in pairs(self.held) do self.cb.noteOff(note) end
    self.held={}; self.mouseNote=nil
end
function E:note(note,on,velocity)
    if on and not self.held[note] then self.held[note]=true; self.cb.noteOn(note,velocity or 0.8)
    elseif not on and self.held[note] then self.held[note]=nil; self.cb.noteOff(note) end
end
function E:key(code,on)
    if code==input.KEY.Escape and on then (self.cb.hide or self.cb.close)(); return end
    if on and (self.typing or I.UI.getMode()~='Interface') then return end
    local keys={input.KEY.Z,input.KEY.S,input.KEY.X,input.KEY.D,input.KEY.C,input.KEY.V,input.KEY.G,
        input.KEY.B,input.KEY.H,input.KEY.N,input.KEY.J,input.KEY.M,input.KEY.Comma}
    for n,k in ipairs(keys) do if code==k then self:note(59+n,on) end end
end
function E:slider(name,title,x,y,width)
    local spec=schema.specs[name]
    local element=self:element{type=ui.TYPE.Widget,props={position=v(x,y),size=v(width,43)},content=ui.content{}}
    local function normalized(value)
        if name=='cutoff' then return math.log(value/spec.min)/math.log(spec.max/spec.min) end
        return (value-spec.min)/(spec.max-spec.min)
    end
    local function refresh()
        local value=self.params[name]
        local display=string.format('%.2f',value)
        if spec.discrete then
            display=tostring(value)
            if name=='wave1' or name=='wave2' then display=waves[value+1]
            elseif name=='enabled2' then display=value==1 and 'On' or 'Off'
            elseif name=='lfoTarget' then display=({'Pitch','Filter','Amplitude'})[value+1] end
        elseif name=='cutoff' then display=string.format('%d Hz',value) end
        local track=rect(0,25,width,10,colors.track)
        track.content=ui.content{rect(0,0,math.max(2,width*normalized(value)),10,colors.accent)}
        local function set(e)
            local t=math.max(0,math.min(1,e.offset.x/width))
            local value=spec.min+(spec.max-spec.min)*t
            if name=='cutoff' then value=spec.min*(spec.max/spec.min)^t end
            if spec.discrete then value=math.floor(value+0.5) end
            self.params[name]=value; self.cb.change(name,value); refresh(); self:plots()
        end
        track.events={mousePress=callback(set),mouseMove=callback(function(e) if e.button==1 then set(e) end end)}
        -- Label and track are separate so changing a value never rebuilds the text input.
        element.layout.content=ui.content{label(title..'  '..display,0,0,width,13),track}
        element:update()
    end
    self.controls[name]=refresh; refresh(); return element
end
function E:plots()
    local content={label('AMP ENVELOPE',10,4,180,12,colors.muted),label('OSCILLATOR PREVIEW',350,4,220,12,colors.muted)}
    local p=self.params
    local total=p.attack+p.decay+0.7+p.release
    local function envelope(t)
        if t<p.attack then return t/p.attack end
        t=t-p.attack
        if t<p.decay then return 1-(1-p.sustain)*t/p.decay end
        t=t-p.decay
        if t<0.7 then return p.sustain end
        return p.sustain*math.max(0,1-(t-0.7)/p.release)
    end
    local previous
    for n=0,99 do
        local amplitude=envelope(n/99*total)
        local y=75-amplitude*44
        if previous then content[#content+1]=rect(12+n*3,math.min(y,previous),2,math.max(2,math.abs(previous-y)),colors.accent) end
        previous=y
        local phase=(n/99*3)%1
        local a=math.sin(phase*math.pi*2)
        if p.wave1==1 then a=2/math.pi*math.asin(a)
        elseif p.wave1==2 then a=phase*2-1 elseif p.wave1==3 then a=phase<0.5 and 1 or -1 end
        content[#content+1]=rect(352+n*3,54-a*20,2,2,colors.accent)
    end
    self.plot.layout.content=ui.content(content); self.plot:update()
end
function E.new(params,cb)
    local self=setmetatable({params=params,cb=cb,children={},controls={},entries={},held={},name='Untitled',lastMeter=0},E)
    local screen=ui.screenSize()
    -- Compact layout fits 1024x768 and common desktop resolutions.
    self.width=math.min(940,screen.x-24)
    local w=self.width
    local content={label('OPENMW  /  SYNTH',20,14,260,22),label('PROCEDURAL • POLYPHONIC • SPATIAL',20,42,350,12,colors.muted)}
    content[#content+1]=self:button('Hide [F8]',w-198,15,104,cb.hide or cb.close)
    content[#content+1]=self:button('Finish',w-84,15,64,cb.close)
    if cb.midiPlay then
        content[#content+1]=self:button('<',350,14,26,function() cb.midiCycle(-1) end)
        content[#content+1]=self:button('>',382,14,26,function() cb.midiCycle(1) end)
        content[#content+1]=self:button('Play MIDI',418,14,104,cb.midiPlay)
        content[#content+1]=self:button('Stop',530,14,64,cb.midiStop)
        self.midiLabel=self:element(label(cb.midiLabel(),360,46,w-390,12,colors.accent))
        content[#content+1]=self.midiLabel
    end
    self.nameElement=self:element{type=ui.TYPE.TextEdit,props={position=v(20,72),size=v(215,28),text='Untitled',textSize=15,textColor=colors.accent},
        events={textChanged=callback(function(text) self.name=text:sub(1,64) end),
            focusGain=callback(function() self.typing=true end),focusLoss=callback(function() self.typing=false end)}}
    content[#content+1]=self.nameElement
    local commands={{'New',function() self.name='Untitled'; self.selected=nil; cb.command('reset') end},
        {'Save',function() cb.command(self.selected and self.selected:sub(1,2)=='p:' and 'save' or 'saveAs',
            {presetId=self.selected,name=self.name,params=self.params}) end},
        {'Save As',function() cb.command('saveAs',{name=self.name,params=self.params}) end},
        {'Rename',function() cb.command('rename',{presetId=self.selected,name=self.name}) end},
        {'Delete',function() cb.command('delete',{presetId=self.selected}) end},
        {'Reset',function() cb.command('reset') end}}
    for n,c in ipairs(commands) do content[#content+1]=self:button(c[1],245+(n-1)*math.floor((w-270)/6),72,math.floor((w-285)/6),c[2]) end
    self.selection=self:element(label('Choose a preset',60,111,320,14,colors.accent)); content[#content+1]=self.selection
    local function cycle(step)
        local index=0
        for n,p in ipairs(self.entries) do if p.id==self.selected then index=n end end
        if #self.entries==0 then return end
        index=(index-1+step)%#self.entries+1
        self.selected=self.entries[index].id; self:refreshName()
    end
    content[#content+1]=self:button('<',20,107,28,function() cycle(-1) end)
    content[#content+1]=self:button('>',390,107,28,function() cycle(1) end)
    content[#content+1]=self:button('Load',428,107,65,function() cb.command('load',{presetId=self.selected}) end)
    content[#content+1]=label('Z S X D C V G B H N J M  /  C4–C5',515,113,390,12,colors.muted)
    local col=(w-56)/3
    for n,title in ipairs({'OSCILLATORS','FILTER + ENVELOPES','MODULATION + OUTPUT'}) do
        local x=20+(n-1)*(col+8)
        content[#content+1]=rect(x,150,col,380,colors.panel)
        content[#content+1]=label(title,x+10,160,col-15,13,colors.accent)
    end
    local x1,x2,x3=30,38+col,46+col*2
    local function slider(name,title,x,y,width) content[#content+1]=self:slider(name,title,x,y,width) end
    slider('wave1','OSC 1 Wave',x1,185,col-20)
    slider('volume1','Level',x1,230,col-20)
    slider('tune1','Tune',x1,275,col/2-18); slider('fine1','Fine (cents)',x1+col/2-5,275,col/2-18)
    slider('wave2','OSC 2 Wave',x1,330,col-20)
    slider('enabled2','Enable',x1,375,col/2-18); slider('volume2','Level',x1+col/2-5,375,col/2-18)
    slider('tune2','Tune',x1,420,col/2-18); slider('fine2','Fine (cents)',x1+col/2-5,420,col/2-18)
    for n,item in ipairs({{'cutoff','Low-pass cutoff'},{'resonance','Resonance'},{'filterAmount','Filter env (octaves)'}}) do slider(item[1],item[2],x2,185+(n-1)*43,col-20) end
    for n,name in ipairs({'attack','decay','sustain','release'}) do
        slider(name,({'AMP A','AMP D','AMP S','AMP R'})[n],x2+((n-1)%2)*(col/2-5),320+math.floor((n-1)/2)*45,col/2-18)
    end
    for n,name in ipairs({'filterAttack','filterDecay','filterSustain','filterRelease'}) do
        slider(name,({'FLT A','FLT D','FLT S','FLT R'})[n],x2+((n-1)%2)*(col/2-5),420+math.floor((n-1)/2)*45,col/2-18)
    end
    for n,item in ipairs({{'volume','Master volume'},{'gain','Output gain'},{'lfoRate','LFO rate (Hz)'},{'lfoDepth','LFO depth'},
        {'lfoTarget','LFO destination'},{'polyphony','Voice limit'}}) do slider(item[1],item[2],x3,185+(n-1)*43,col-20) end
    self.meter=self:element(label('',x3,455,col-20,12,colors.accent)); content[#content+1]=self.meter
    self.plot=self:element{type=ui.TYPE.Widget,props={position=v(20,537),size=v(w-40,84)},content=ui.content{}}
    content[#content+1]=self.plot; self:plots()
    local names={'C','C#','D','D#','E','F','F#','G','G#','A','A#','B','C'}
    local keyWidth=(w-40)/13
    for n,name in ipairs(names) do
        local black=name:find('#')~=nil
        local key=rect(20+(n-1)*keyWidth,625,keyWidth-3,black and 42 or 56,
            black and colors.track or util.color.rgb(0.72,0.76,0.8))
        key.content=ui.content{label(name,8,10,keyWidth-8,14,black and colors.text or colors.bg)}
        key.events={mousePress=callback(function(e)
                if e.button==1 then
                    self.mouseNote=59+n
                    self:note(59+n,true,math.max(0.2,math.min(1,0.2+0.8*e.offset.y/(black and 42 or 56))))
                end
            end),
            mouseRelease=callback(function() self:note(59+n,false); self.mouseNote=nil end)}
        content[#content+1]=key
    end
    local root=rect(0,0,w,695,colors.bg)
    root.layer='Windows'; root.props.position=v((screen.x-w)/2,math.max(4,(screen.y-695)/2)); root.content=ui.content(content)
    self.root=ui.create(root)
    I.UI.addMode('Interface',{windows={}})
    return self
end
function E:frame(now)
    if I.UI.getMode()~='Interface' then (self.cb.hide or self.cb.close)(); return end
    if self.mouseNote and not input.isMouseButtonPressed(1) then self:note(self.mouseNote,false); self.mouseNote=nil end
    if now-self.lastMeter>0.1 then
        self.lastMeter=now
        local peak,voices=self.cb.meter()
        self.meter.layout.props.text=string.format('OUTPUT  %s  %d voices',string.rep('|',math.floor(peak*25)),voices)
        self.meter:update()
        if self.midiLabel then self.midiLabel.layout.props.text=self.cb.midiLabel();self.midiLabel:update() end
    end
end
function E:destroy()
    self:releaseAll()
    self.root:destroy()
    for _,element in ipairs(self.children) do element:destroy() end
    if I.UI.getMode()=='Interface' then I.UI.removeMode('Interface') end
end
return E
