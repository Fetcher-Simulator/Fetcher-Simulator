-- View state is local and non-destructive. Only Apply trim/loop, Use sample,
-- and Create slices change playback references or the project.
local ui=require('openmw.ui');local util=require('openmw.util');local async=require('openmw.async');local input=require('openmw.input')
local Controls=require('scripts.multiplayer.fetcher.procedural-synth.client.controls')
local E={};E.__index=E;local v=util.vector2;local white=ui.texture{path='white'}
local c={bg=util.color.rgb(.075,.09,.12),line=util.color.rgb(.14,.17,.21),wave=util.color.rgb(.2,.85,.72),blue=util.color.rgb(.35,.65,.95),text=util.color.rgb(.85,.89,.94)}
local function clamp(n,a,b) return math.max(a,math.min(b,n)) end
local function rect(x,y,w,h,color) return {type=ui.TYPE.Image,props={position=v(x,y),size=v(w,h),resource=white,color=color}} end
local function label(text,x,y,w) return {type=ui.TYPE.Text,props={position=v(x,y),size=v(w,24),text=text,textSize=12,textColor=c.text}} end
local function cb(fn) return async:callback(function(e) fn(e);return false end) end
function E.new(owner,content)
    local b=owner.samples;local info=b:info();local length=math.floor((info.duration or 0)*1000)/1000
    if info.status~='Ready' or length<.01 or not b.synth.sampleWaveformRange then
        owner.sampleEditorWaiting=info.status=='Loading'
        content[#content+1]=label(info.status=='Loading' and 'Loading the edited excerpt...' or info.error or 'Choose a Ready sample. Expanded editing requires the updated client (API 39).',20,100,850)
        content[#content+1]=owner:button('Back [Esc]',20,55,150,function() owner:setSampleFocus(false) end);return
    end
    owner.sampleEditorWaiting=nil
    local key=table.concat({b.path,b.offset,b.duration,tostring(b.reverse)},'|')
    if not b.editView or b.editView.key~=key then b.editView={key=key,first=0,span=length,caret=0,trimFirst=0,trimLast=length,points={},snap=true} end
    local self=setmetatable({owner=owner,b=b,s=b.editView,length=length,x=25,y=280,width=owner.width-50,height=math.max(160,owner.height-405),handles={}},E)
    self.s.loopFirst=b.loop and b.loop.start or 0;self.s.loopLast=b.loop and b.loop.finish or length
    local function button(text,x,y,width,fn,on) content[#content+1]=owner:button(text,x,y,width,fn,on) end
    button('Back [Esc]',20,55,115,function() owner:setSampleFocus(false) end)
    button('Fit',145,55,65,function() self.s.first=0;self.s.span=length;self:draw() end)
    button('Zoom -',220,55,85,function() self:zoom(2) end)
    button('Zoom +',315,55,85,function() self:zoom(.5) end)
    button('Zero snap: '..(self.s.snap and 'On' or 'Off'),410,55,160,function() self.s.snap=not self.s.snap;owner.w.revision=owner.w.revision+1 end,self.s.snap)
    button('Audition',580,55,100,function() b:audition() end)
    button('Release',690,55,100,function() b:release() end)
    button('Stop',800,55,90,function() b:stop() end)
    button('Apply trim',20,95,140,function()
        if b:trimView(self.s.trimFirst,self.s.trimLast) then owner.w.revision=owner.w.revision+1 else owner.w.cb.message('Trim needs at least 10ms within this excerpt; source start must be within ten minutes.') end
    end)
    button(b.loop and 'Loop: On' or 'Loop: Off',170,95,140,function() if b:toggleLoop() then owner.w.revision=owner.w.revision+1 end end,b.loop~=nil)
    button('Use sample on part',320,95,190,function() local ref=b:reference();if ref then b:stop();owner.w:setSample(owner.w.selected,ref) end end)
    button('Add slice at cursor',20,135,180,function() self:addMarker() end)
    button('Delete marker',210,135,140,function() if self.s.marker then table.remove(self.s.points,self.s.marker);self.s.marker=nil;self:draw() end end)
    button('Audition slice',360,135,140,function()
        local ref=self:slice()
        if not ref or not b:auditionReference(ref) then owner.w.cb.message('The slice is loading. Try Audition slice again when Ready.') end
    end)
    button('Create slice lanes',510,135,190,function()
        local refs,err=b:slices(self.s.points)
        if not refs then owner.w.cb.message(err or 'Add 1-15 ordered markers, with at least 10ms per slice.');return end
        b:stop();owner.w:addSampleSlices(refs,(b.path:match('([^/]+)$') or 'Sample'):gsub('%.[^.]+$',''))
    end)
    button('Create phrase',710,135,180,function()
        local refs,err=b:slices(self.s.points)
        if not refs then owner.w.cb.message(err or 'Add 1-15 markers before creating a slice phrase.');return end
        b:stop();owner.w:addSampleSlices(refs,(b.path:match('([^/]+)$') or 'Sample'):gsub('%.[^.]+$',''),true)
    end)
    button('Detect slices',20,183,140,function() self:detect() end)
    self.s.sensitivity=self.s.sensitivity or .5;self.s.spacing=self.s.spacing or .08
    local function setting(key,value) self.s[key]=value;self.s.notice='Settings changed. Detect slices replaces the current marker suggestions.';self:draw() end
    content[#content+1]=Controls.slider(owner,'sliceSensitivity','Sensitivity',{min=0,max=1,step=.01,unit='%'},function() return self.s.sensitivity end,function(value) setting('sensitivity',value) end,175,175,250)
    content[#content+1]=Controls.slider(owner,'sliceSpacing','Minimum gap',{min=.01,max=2,step=.001,unit='s'},function() return self.s.spacing end,function(value) setting('spacing',value) end,445,175,250)
    button('Undo detect',715,183,175,function()
        if self.s.previousPoints then self.s.points=self.s.previousPoints;self.s.previousPoints=nil;self.s.marker=nil;self.s.notice='Previous markers restored.';self:draw() end
    end)
    self.status=owner:element(label('',20,224,owner.width-40));content[#content+1]=self.status
    self.notice=owner:element(label('',20,247,owner.width-40));content[#content+1]=self.notice
    self.wave=owner:element(rect(self.x,self.y,self.width,self.height,c.bg))
    self.wave.layout.events={mousePress=cb(function(e) if e.button==1 then self.s.caret=self:time(e);self.s.marker=nil;self:draw() end end)}
    content[#content+1]=self.wave
    for _,kind in ipairs{'trimFirst','trimLast','loopFirst','loopLast','caret'} do self:handle(content,kind) end
    for n=1,15 do self:handle(content,n) end
    local bar=owner:slider(self.x,self.y+self.height+15,self.width,length>self.s.span and self.s.first/(length-self.s.span) or 0,function(value)
        self.s.first=value*(length-self.s.span);self:draw()
    end,function() return length>self.s.span and self.s.first/(length-self.s.span) or 0 end)
    content[#content+1]=bar
    content[#content+1]=label('Drag blue trim handles at the top; loop handles at the bottom. Orange markers split slices.',20,self.y+self.height+43,owner.width-40)
    content[#content+1]=label('Click to place the cursor; wheel zooms, Shift+wheel pans. Changes use millisecond precision. Esc returns.',20,self.y+self.height+65,owner.width-40)
    self:draw();return self
end
function E:detect()
    local result,err=self.b:detect(self.s.sensitivity,self.s.spacing,self.s.snap)
    if not result then self.s.notice=err;self:draw();return false end
    if #result.points==0 then self.s.notice='No distinct attacks found. Existing markers kept; raise sensitivity or add markers manually.';self:draw();return false end
    self.s.previousPoints={};for n,point in ipairs(self.s.points) do self.s.previousPoints[n]=point end
    self.s.points=result.points;self.s.marker=nil
    self.s.notice=result.truncated and ('Showing the 15 strongest of '..result.found..' spaced attacks. Raise minimum gap or trim to get fewer.')
        or ('Suggested '..#result.points..' markers. Drag/delete them, audition a slice, then create lanes or an arranged phrase.')
    self:draw();return true
end
function E:time(e)
    local root=self.owner.root and self.owner.root.layout.props.position or v(0,0);local scale=self.owner.window.scale
    local x=e.position and (e.position.x-root.x)/scale-self.x or e.offset.x/scale
    return clamp(self.s.first+clamp(x,0,self.width)/self.width*self.s.span,0,self.length)
end
function E:handle(content,kind)
    local h=self.owner:element(rect(0,0,10,15,c.blue));self.handles[kind]=h
    h.layout.events={mousePress=cb(function(e)
        if e.button~=1 then return end
        self.drag=kind;if type(kind)=='number' then self.s.marker=kind end
        self:move(e)
    end),mouseMove=cb(function(e) if self.drag==kind then self:move(e) end end)}
    content[#content+1]=h
end
function E:move(e)
    local s=self.s;local value=self:time(e);local kind=self.drag
    if type(kind)=='number' then
        s.points[kind]=clamp(value,(s.points[kind-1] or 0)+.01,(s.points[kind+1] or self.length)-.01);s.caret=s.points[kind]
    elseif kind=='trimFirst' then s.trimFirst=math.min(value,s.trimLast-.01)
    elseif kind=='trimLast' then s.trimLast=math.max(value,s.trimFirst+.01)
    elseif kind=='loopFirst' then s.loopFirst=math.min(value,s.loopLast-.01)
    elseif kind=='loopLast' then s.loopLast=math.max(value,s.loopFirst+.01)
    elseif kind=='caret' then s.caret=value end
    self:draw()
end
function E:finish(cancel)
    local kind=self.drag;if not kind then return end;self.drag=nil
    if cancel then return end
    local s=self.s;local value=type(kind)=='number' and s.points[kind] or s[kind]
    value=self.b:snapPoint(value,s.snap)
    if type(kind)=='number' then s.points[kind]=clamp(value,(s.points[kind-1] or 0)+.01,(s.points[kind+1] or self.length)-.01)
    elseif kind=='trimFirst' then s[kind]=math.min(value,s.trimLast-.01)
    elseif kind=='trimLast' then s[kind]=math.max(value,s.trimFirst+.01)
    elseif kind=='loopFirst' then s[kind]=math.min(value,s.loopLast-.01)
    elseif kind=='loopLast' then s[kind]=math.max(value,s.loopFirst+.01)
    else s[kind]=value end
    if kind=='loopFirst' or kind=='loopLast' then
        local fade=math.min(self.b.loop and self.b.loop.crossfade or .02,(s.loopLast-s.loopFirst)/2)
        if not self.b:setLoop(s.loopFirst,s.loopLast,fade) then self.owner.w.cb.message('Loop points must leave at least 10ms of audio.')
        else self.owner.w.revision=self.owner.w.revision+1 end
    end
    self:draw()
end
function E:frame()
    if self.drag and not input.isMouseButtonPressed(1) then self:finish() end
    if not self.waveKey then self:draw() end
end
function E:zoom(factor)
    local s=self.s;local center=clamp(s.caret,s.first,s.first+s.span)
    s.span=clamp(s.span*factor,math.min(.01,self.length),self.length)
    s.first=clamp(center-s.span/2,0,self.length-s.span);self:draw()
end
function E:wheel(delta)
    if input.isShiftPressed and input.isShiftPressed() then self.s.first=clamp(self.s.first-delta*self.s.span/4,0,self.length-self.s.span);self:draw()
    else self:zoom(delta>0 and .5 or 2) end
end
function E:addMarker()
    local s=self.s;local point=self.b:snapPoint(s.caret,s.snap)
    if #s.points>=15 or point<.01 or point>self.length-.01 then return false end
    for _,p in ipairs(s.points) do if math.abs(p-point)<.01-1e-7 then return false end end
    s.points[#s.points+1]=point;table.sort(s.points)
    s.caret=point
    for n,p in ipairs(s.points) do if p==point then s.marker=n end end
    self:draw();return true
end
function E:slice()
    local s=self.s;local first,last=0,self.length
    for _,point in ipairs(s.points) do if point<=s.caret then first=point else last=point;break end end
    local ref=self.b:reference();if not ref or last-first<.01 then return nil end
    ref.offset=math.floor((ref.offset+(ref.reverse and self.length-last or first))*1000+.5)/1000
    ref.duration=math.floor((last-first)*1000+.5)/1000;ref.loop=nil;ref.oneShot=true
    return require('scripts.multiplayer.fetcher.procedural-synth.shared.schema').sample(ref)
end
function E:draw()
    local s=self.s;local key=table.concat({s.first,s.span},'|')
    if key~=self.waveKey then
        local wave=self.b:waveRange(s.first,math.min(self.length,s.first+s.span))
        if wave then
            local data={rect(0,self.height/2,self.width,1,c.line)}
            for n=1,#wave.minimum do
                local low,high=wave.minimum[n],wave.maximum[n]
                data[#data+1]=rect((n-1)*self.width/#wave.minimum,self.height/2-high*(self.height/2-20),math.max(1,self.width/#wave.minimum),math.max(1,(high-low)*(self.height/2-20)),c.wave)
            end
            self.wave.layout.content=ui.content(data);self.owner.window:update(self.wave);self.waveKey=key
        end
    end
    for kind,h in pairs(self.handles) do
        local point=type(kind)=='number' and s.points[kind] or s[kind]
        local x=point and (point-s.first)/s.span*self.width
        h.layout.props.visible=x~=nil and x>=0 and x<=self.width
        local loop=kind=='loopFirst' or kind=='loopLast'
        h.layout.props.position=v(self.x+clamp((x or 0)-5,0,self.width-10),self.y+(loop and self.height-15 or type(kind)=='number' and 24 or kind=='caret' and 46 or 0))
        h.layout.props.color=type(kind)=='number' and util.color.rgb(1,.65,.25) or kind=='caret' and c.wave or c.blue
        self.owner.window:update(h)
    end
    self.status.layout.props.text=string.format('View %.3f-%.3fs | Cursor %.3fs | Trim %.3f-%.3fs | Loop %.3f-%.3fs | %d slices',s.first,s.first+s.span,s.caret,s.trimFirst,s.trimLast,s.loopFirst,s.loopLast,#s.points+1)
    self.owner.window:update(self.status)
    self.notice.layout.props.text=self.s.notice or 'Detect slices suggests attack points. Create phrase keeps source timing at the edit playhead; original part is kept.'
    self.owner.window:update(self.notice)
end
return E
