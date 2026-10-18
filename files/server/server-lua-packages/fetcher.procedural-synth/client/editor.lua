local ui=require('openmw.ui')
local util=require('openmw.util')
local async=require('openmw.async')
local input=require('openmw.input')
local I=require('openmw.interfaces')
local Window=require('scripts.multiplayer.fetcher.procedural-synth.client.window')
local Controls=require('scripts.multiplayer.fetcher.procedural-synth.client.controls')
local schema=require('scripts.multiplayer.fetcher.procedural-synth.shared.schema')
local v=util.vector2
local white=ui.texture{path='white'}
local E={}; E.__index=E
local colors={bg=util.color.rgb(0.035,0.045,0.063),panel=util.color.rgb(0.075,0.09,0.12),
    track=util.color.rgb(0.14,0.17,0.21),accent=util.color.rgb(0.2,0.85,0.72),
    text=util.color.rgb(0.85,0.89,0.94),muted=util.color.rgb(0.5,0.58,0.68)}
local waves={'Sine','Triangle','Saw','Square','Noise'}
local function rect(x,y,w,h,color)
    return {type=ui.TYPE.Image,props={position=v(x,y),size=v(w,h),resource=white,color=color}}
end
local function label(text,x,y,w,size,color)
    return {type=ui.TYPE.Text,props={text=text,position=v(x,y),size=v(w,22),textSize=size or 14,textColor=color or colors.text}}
end
local function callback(fn) return async:callback(function(...) fn(...); return false end) end
function E:element(layout)
    local element=self.window:create(layout); self.children[#self.children+1]=element; return element
end
function E:button(text,x,y,w,fn)
    local r=rect(x,y,w,28,colors.track)
    r.content=ui.content{label(text,7,5,w-10,13)}
    r.events={mouseClick=callback(fn)}
    return r
end
function E:refreshName()
    local name=self.name or 'Untitled'
    self.nameElement.layout.props.text=name; self.window:update(self.nameElement)
    local selected='Choose a preset'
    for _,p in ipairs(self.entries) do if p.id==self.selected then selected=p.name end end
    self.selection.layout.props.text=selected; self.window:update(self.selection)
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
    if on and input.isCtrlPressed and input.isCtrlPressed() then return end
    if on and (self.typing or I.UI.getMode()~='Interface') then return end
    local keys={input.KEY.Z,input.KEY.S,input.KEY.X,input.KEY.D,input.KEY.C,input.KEY.V,input.KEY.G,
        input.KEY.B,input.KEY.H,input.KEY.N,input.KEY.J,input.KEY.M,input.KEY.Comma}
    for n,k in ipairs(keys) do if code==k then self:note(59+n,on) end end
end
function E:slider(name,title,x,y,width)
    local base=schema.specs[name];local spec={min=base.min,max=base.max,discrete=base.discrete}
    local times={attack=true,decay=true,release=true,filterAttack=true,filterDecay=true,filterRelease=true,pitchDecay=true,bodyDecay=true,noiseDecay=true,drumPitchDecay=true}
    local hz={cutoff=true,lfoRate=true,bodyFrequency=true,noiseHighpass=true,noiseLowpass=true}
    if times[name] then spec.unit='s';spec.log=true;spec.step=0.001
    elseif hz[name] then spec.unit='Hz';spec.log=true;spec.step=1
    elseif name:find('Level') or name:find('volume') or name=='sustain' or name=='filterSustain' then spec.unit='%';spec.step=0.01
    elseif name:find('tune') or name:find('fine') or name:find('Amount') then spec.step=1 end
    local labels={wave1=waves,wave2=waves,enabled2={'Off','On'},lfoTarget={'Pitch','Filter','Amplitude'},drumType={'Synth','Kick','Snare','Closed hat','Open hat'}}
    local caption=labels[name] and function() return title..' / '..(labels[name][self.params[name]+1] or '') end or title
    local element,refresh=Controls.slider(self,name,caption,spec,function() return self.params[name] end,function(value)
        self.params[name]=value;self.cb.change(name,value)
    end,x,y,width)
    self.controls[name]=refresh;return element
end
function E:plots()
    local content={label(self.drums and 'BODY / NOISE DECAY (-60 dB)' or 'AMP ENVELOPE',10,4,180,12,colors.muted),label(self.drums and 'BODY WAVE (ILLUSTRATION)' or 'OSCILLATOR PREVIEW',350,4,260,12,colors.muted)}
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
        if self.drums then amplitude=math.exp(-6.907755*(n/99*math.max(p.bodyDecay,p.noiseDecay))/p.bodyDecay) end
        local y=75-amplitude*44
        if previous then content[#content+1]=rect(12+n*3,math.min(y,previous),2,math.max(2,math.abs(previous-y)),colors.accent) end
        previous=y
        if self.drums then
            local noise=math.exp(-6.907755*(n/99*math.max(p.bodyDecay,p.noiseDecay))/p.noiseDecay)
            content[#content+1]=rect(12+n*3,75-noise*44,2,2,colors.text)
        end
        local phase=(n/99*3)%1
        local a=math.sin(phase*math.pi*2)
        if p.wave1==1 then a=2/math.pi*math.asin(a)
        elseif p.wave1==2 then a=phase*2-1 elseif p.wave1==3 then a=phase<0.5 and 1 or -1
        elseif p.wave1==4 then local noise=math.sin(n*127.1)*43758.5453;a=(noise-math.floor(noise))*2-1 end
        content[#content+1]=rect(352+n*3,54-a*20,2,2,colors.accent)
    end
    self.plot.layout.content=ui.content(content); self.window:update(self.plot)
end
function E.new(params,cb)
    local self=setmetatable({params=params,cb=cb,window=Window.new(),children={},controls={},entries={},held={},name='Untitled',lastMeter=0},E)
    self:render();I.UI.addMode('Interface',{windows={}});return self
end
function E:render()
    self:releaseAll()
    self.controlEpoch=(self.controlEpoch or 0)+1
    if self.root then self.root:destroy() end
    for _,element in ipairs(self.children) do element:destroy() end
    self.children={};self.controls={};self.typing=false;self.drums=self.params.drumType>0
    local params,cb=self.params,self.cb
    -- Window scales the logical canvas to the current viewport.
    self.width=940
    local w=self.width
    local content={label('OPENMW  /  SYNTH',20,14,260,22),label(cb.subtitle or 'PROCEDURAL / POLYPHONIC / SPATIAL',20,42,350,12,colors.muted)}
    if cb.studio then content[#content+1]=self:button('Studio',w-284,15,76,cb.studio) end
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
    self.nameElement=self:element{type=ui.TYPE.TextEdit,props={position=v(20,72),size=v(215,28),text=self.name,textSize=15,textColor=colors.accent},
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
    content[#content+1]=self:button(self.drums and 'Synth' or 'Drum',515,107,70,function()
        self.params.drumType=self.drums and 0 or 2;cb.change('drumType',self.params.drumType)
    end)
    content[#content+1]=label(self.drums and 'One-shot / fixed tuning' or 'Z S X D C V G B H N J M',600,113,315,12,colors.muted)
    local col=(w-56)/3
    for n,title in ipairs(self.drums and {'DRUM BODY','SNARES / NOISE','OUTPUT'} or {'OSCILLATORS','FILTER + ENVELOPES','MODULATION + OUTPUT'}) do
        local x=20+(n-1)*(col+8)
        content[#content+1]=rect(x,150,col,380,colors.panel)
        content[#content+1]=label(title,x+10,160,col-15,13,colors.accent)
    end
    local x1,x2,x3=30,38+col,46+col*2
    local function slider(name,title,x,y,width) content[#content+1]=self:slider(name,title,x,y,width) end
    if self.drums then
        for n,item in ipairs({{'drumType','Drum type'},{'bodyFrequency','Body tune (Hz)'},{'bodyDecay','Body decay'},{'bodyLevel','Body level'},{'drumPitchAmount','Pitch sweep (semitones)'},{'drumPitchDecay','Pitch decay'},{'drumClick','Attack click'}}) do slider(item[1],item[2],x1,185+(n-1)*45,col-20) end
        for n,item in ipairs({{'noiseLevel','Noise level'},{'noiseDecay','Noise decay'},{'noiseHighpass','Noise high-pass'},{'noiseLowpass','Noise low-pass'},{'drumDrive','Saturation'}}) do slider(item[1],item[2],x2,185+(n-1)*45,col-20) end
        for n,item in ipairs({{'volume','Instrument volume'},{'gain','Output gain'},{'polyphony','Voice limit'}}) do slider(item[1],item[2],x3,185+(n-1)*45,col-20) end
        content[#content+1]=label('Drum type: 1 Kick, 2 Snare',x2,430,col-20,12,colors.muted)
        content[#content+1]=label('3 Closed hat, 4 Open hat',x2,452,col-20,12,colors.muted)
        content[#content+1]=label('Closed hats choke open hats.',x2,478,col-20,12,colors.accent)
        content[#content+1]=label('Decay is time to -60 dB.',x3,350,col-20,12,colors.muted)
        content[#content+1]=label('MIDI note does not retune drums.',x3,374,col-20,12,colors.muted)
        content[#content+1]=label('Stop always releases all tails.',x3,398,col-20,12,colors.muted)
    else
        slider('wave1','OSC 1 Wave',x1,185,col-20)
        slider('volume1','Level',x1,230,col-20)
        slider('tune1','Tune',x1,275,col/2-18); slider('fine1','Fine cents',x1+col/2-5,275,col/2-18)
        slider('wave2','OSC 2 Wave',x1,330,col-20)
        slider('enabled2','Enable',x1,375,col/2-18); slider('volume2','Level',x1+col/2-5,375,col/2-18)
        slider('tune2','Tune',x1,420,col/2-18); slider('fine2','Fine cents',x1+col/2-5,420,col/2-18)
        slider('pitchAmount','Pitch env',x1,465,col/2-18); slider('pitchDecay','Pitch decay',x1+col/2-5,465,col/2-18)
        for n,item in ipairs({{'cutoff','Low-pass cutoff'},{'resonance','Resonance'},{'filterAmount','Filter env octaves'}}) do slider(item[1],item[2],x2,185+(n-1)*43,col-20) end
        for n,name in ipairs({'attack','decay','sustain','release'}) do slider(name,({'AMP A','AMP D','AMP S','AMP R'})[n],x2+((n-1)%2)*(col/2-5),320+math.floor((n-1)/2)*45,col/2-18) end
        slider('ampCurve','Amp curve',x3,477,col-20)
        for n,name in ipairs({'filterAttack','filterDecay','filterSustain','filterRelease'}) do slider(name,({'FLT A','FLT D','FLT S','FLT R'})[n],x2+((n-1)%2)*(col/2-5),420+math.floor((n-1)/2)*45,col/2-18) end
        for n,item in ipairs({{'volume','Instrument volume'},{'gain','Output gain'},{'lfoRate','LFO rate Hz'},{'lfoDepth','LFO depth'},{'lfoTarget','LFO destination'},{'polyphony','Voice limit'}}) do slider(item[1],item[2],x3,185+(n-1)*43,col-20) end
    end
    self.meter=self:element(label('',x3,455,col-20,12,colors.accent)); content[#content+1]=self.meter
    content[#content+1]=label('Click values to type. Enter applies. +/- adjusts. Shift = fine.',30,518,870,10,colors.muted)
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
                    self:note(59+n,true,math.max(0.2,math.min(1,0.2+0.8*e.offset.y/(self.window.scale*(black and 42 or 56)))))
                end
            end),
            mouseRelease=callback(function() self:note(59+n,false); self.mouseNote=nil end)}
        content[#content+1]=key
    end
    self.root=self.window:root(content)
    self:refreshName()
end
function E:frame(now)
    if self.drums~=(self.params.drumType>0) then self:render() end
    self.window:frame(self)
    if I.UI.getMode()~='Interface' then (self.cb.hide or self.cb.close)(); return end
    if self.mouseNote and not input.isMouseButtonPressed(1) then self:note(self.mouseNote,false); self.mouseNote=nil end
    if now-self.lastMeter>0.1 then
        self.lastMeter=now
        local peak,voices=self.cb.meter()
        self.meter.layout.props.text=string.format('OUTPUT  %s  %d voices',string.rep('|',math.floor(peak*25)),voices)
        self.window:update(self.meter)
        if self.midiLabel then self.midiLabel.layout.props.text=self.cb.midiLabel();self.window:update(self.midiLabel) end
    end
end
function E:destroy()
    self.uiClosed=true
    self:releaseAll()
    self.root:destroy()
    for _,element in ipairs(self.children) do element:destroy() end
    if I.UI.getMode()=='Interface' then I.UI.removeMode('Interface') end
end
return E
