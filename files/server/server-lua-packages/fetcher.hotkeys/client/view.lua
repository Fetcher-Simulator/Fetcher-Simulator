local ui=require('openmw.ui')
local util=require('openmw.util')
local async=require('openmw.async')
local S=require('scripts.multiplayer.fetcher.hotkeys.client.style')
local Model=require('scripts.multiplayer.fetcher.hotkeys.shared.model')
local V={};V.__index=V
local v=util.vector2
local function cb(fn) return async:callback(function(...) fn(...);return false end) end
local function size() return ui.layers[ui.layers.indexOf('Windows')].size end
local function hit(p,x,y,w,h) return p and p.x>=x and p.y>=y and p.x<x+w and p.y<y+h end
function V.new(api)
    if not ui.layers.indexOf('FetcherHotbar') then ui.layers.insertAfter('Windows','FetcherHotbar',{interactive=true}) end
    return setmetatable({api=api,offset=0,query='',selected=1,children={},autoPreview=true,clock=0},V)
end
function V:destroyBrowser()
    if self.browser then self.browser:destroy();self.browser=nil end
    for _,child in ipairs(self.children) do child:destroy() end
    self.children={};self.press=nil;self.drag=nil;self.pendingPreview=nil;self.api.stopPreview()
end
function V:close()
    self:destroyBrowser();self.mode=nil
    if self.ownsMode then self.api.closeMode();self.ownsMode=nil end
end
function V:open(mode)
    self:destroyBrowser();self.mode=mode;self.query='';self.offset=0;self.selected=1
    if not self.ownsMode then self.ownsMode=self.api.openMode() end
    self:buildBrowser()
end
function V:filtered()
    local list={};local q=self.query:lower()
    for _,entry in ipairs(self.api.entries(self.mode)) do
        if (entry.label..' '..(entry.path or '')..' '..(entry.category or '')):lower():find(q,1,true) then list[#list+1]=entry end
    end
    return list
end
function V:entriesChanged()
    if self.mode then
        self.matches=self:filtered();self.selected=math.max(1,math.min(self.selected,#self.matches));self:drawRows()
    end
end
function V:select(index,preview)
    self.selected=math.max(1,math.min(index,#self.matches))
    if self.selected<=self.offset then self.offset=self.selected-1 end
    if self.selected>self.offset+self.rowCount then self.offset=self.selected-self.rowCount end
    if preview and self.mode=='voices' and self.autoPreview then self.pendingPreview=self.clock+.15 end
    self:drawRows()
end
function V:wheel(delta)
    if not self.browser or not self.overList then return end
    self:select(self.selected-(delta>0 and 1 or -1),true)
end
function V:drawRows()
    if not self.browser then return end
    self.matches=self.matches or self:filtered()
    self.offset=math.max(0,math.min(self.offset,math.max(0,#self.matches-self.rowCount)))
    for i,element in ipairs(self.rows) do
        local index=self.offset+i;local entry=self.matches[index]
        element.layout.props.visible=entry~=nil
        element.layout.content[1].props.text=entry and entry.label or ''
        element.layout.content[1].props.textColor=index==self.selected and S.header or S.normal
        element:update()
    end
    local entry=self.matches[self.selected]
    self.detail.layout.props.text=entry and (entry.command or entry.label)..'\n'..(entry.path or entry.id) or 'No matching entries.'
    self.detail:update()
    self.counter.layout.props.text=tostring(#self.matches)..(self.mode=='voices' and ' installed voice lines' or ' inventory items');self.counter:update()
    local height=self.rowCount*30;local max=math.max(0,#self.matches-self.rowCount)
    self.thumbHeight=math.max(22,height*self.rowCount/math.max(self.rowCount,#self.matches))
    self.thumbTop=max>0 and self.offset/max*(height-self.thumbHeight) or 0
    self.thumb.layout.props.position=v(0,self.thumbTop);self.thumb.layout.props.size=v(15,self.thumbHeight);self.thumb:update()
end
function V:buildBrowser()
    self:destroyBrowser()
    local screen=size();self.screen=screen
    local width=math.min(820,screen.x-24)
    local height=math.min(640,screen.y-145)
    self.rowCount=math.max(3,math.floor((height-205)/30))
    self.bx=math.floor((screen.x-width)/2);self.by=math.max(10,math.floor((screen.y-height-105)/2))
    local content={S.frame(0,0,width,height,true),S.title(self.mode=='voices' and 'Voice lines' or 'Hotbar items',4,4,width-8,20,20),
        S.label('Drag an entry to a slot. Click its x to clear it.',18,46,width-36),S.frame(18,75,width-36,29)}
    local function element(layout)
        local e=ui.create(layout);self.children[#self.children+1]=e;return e
    end
    local function button(text,x,y,w,fn)
        local b=S.button(text,x,y,w,29);b.events={mouseClick=cb(fn)};content[#content+1]=b
    end
    self.search=element{template=S.templates.textEditLine,props={position=v(24,79),size=v(width-48,22),text=self.query,textSize=16,autoSize=false},
        events={textChanged=cb(function(text) self.query=text;self.offset=0;self.selected=1;self.matches=self:filtered();self:drawRows() end)}}
    content[#content+1]=self.search;self.rows={}
    for i=1,self.rowCount do
        local layout=S.image(18,116+(i-1)*30,width-62,28,util.color.rgb(0,0,0))
        layout.content=ui.content{S.label('',6,3,width-77,16)}
        layout.events={mousePress=cb(function(e)
            if e.button~=1 then return end
            local entry=self.matches[self.offset+i];if not entry then return end
            self:select(self.offset+i,false);self.press={entry=entry,position=e.position};self.ignoreClick=false
        end),mouseMove=cb(function(e)
            self.overList=true
            if self.press and e.button==1 and (e.position-self.press.position):length()>4 then
                self.drag={kind=self.mode=='voices' and 'voice' or 'item',id=self.press.entry.id};self.ignoreClick=true
            end
        end),mouseRelease=cb(function(e) if e.button==1 then self:release(e.position) end end),
        mouseClick=cb(function()
            if not self.ignoreClick and self.mode=='voices' then local entry=self.matches[self.selected];if entry then self.api.preview(entry) end end
        end),focusGain=cb(function() self.overList=true end),focusLoss=cb(function() self.overList=false end)}
        self.rows[i]=element(layout);content[#content+1]=self.rows[i]
    end
    local trackHeight=self.rowCount*30
    local function scroll(e,press)
        if e.button~=1 then return end
        local at=e.position.y-self.by-116
        if press then self.grab=at>=self.thumbTop and at<=self.thumbTop+self.thumbHeight and at-self.thumbTop or self.thumbHeight/2 end
        local max=math.max(0,#self.matches-self.rowCount)
        self.offset=math.floor(math.max(0,math.min(1,(at-(self.grab or 0))/math.max(1,trackHeight-self.thumbHeight)))*max+.5)
        self.selected=self.offset+1;self:drawRows()
    end
    local events={mousePress=cb(function(e) scroll(e,true) end),mouseMove=cb(function(e) scroll(e,false) end),
        focusGain=cb(function() self.overList=true end),focusLoss=cb(function() self.overList=false end)}
    local thumb=S.filled(0,0,15,22);thumb.events=events;self.thumb=element(thumb)
    local track=S.image(width-35,116,15,trackHeight,util.color.rgb(0,0,0));track.events=events;track.content=ui.content{self.thumb};content[#content+1]=track
    local y=116+trackHeight+5
    self.detail=element(S.label('',18,y,width-36,14));self.detail.layout.props.size=v(width-36,44);content[#content+1]=self.detail
    self.counter=element(S.label('',18,height-37,240,14));content[#content+1]=self.counter
    if self.mode=='voices' then
        button('Preview',width-420,height-42,92,function() local e=self.matches[self.selected];if e then self.api.preview(e) end end)
        button(self.autoPreview and 'Auto: On' or 'Auto: Off',width-321,height-42,103,function()
            self.autoPreview=not self.autoPreview;self:buildBrowser()
        end)
        button('Stop',width-211,height-42,79,function() self.pendingPreview=nil;self.api.stopPreview() end)
    else button('Refresh',width-211,height-42,100,function() self:entriesChanged() end) end
    button('Close',width-123,height-42,105,function() self:close() end)
    self.browser=ui.create{type=ui.TYPE.Widget,layer='Windows',props={position=v(self.bx,self.by),size=v(width,height)},content=ui.content(content)}
    self.matches=self:filtered();self:drawRows()
end
function V:drawBar()
    if self.bar then self.bar:destroy();self.bar=nil end
    local screen=size();self.barViewport=screen
    self.rects={}
    if not self.api.visible() then return end
    local config=self.api.config();local page,pages=self.api.page()
    local cell=math.min(56*config.scale,(screen.x-156)/10)
    local width=cell*10+144;local height=cell+36
    local x=math.floor((screen.x-width)/2);local y=screen.y-height-config.offset
    self.barPosition=v(x,y)
    local content={S.frame(0,0,width,height),S.label('Page '..page..' / '..pages,10,4,width-20,14,true)}
    for slot=1,10 do
        local left=70+(slot-1)*cell;local top=25
        local index=Model.index(page,slot);local binding=self.api.binding(index)
        local info=self.api.describe(binding)
        local box=S.frame(left,top,cell-3,cell-3)
        local caption=info and (binding.kind=='voice' and 'Voice' or info.label:sub(1,7)) or ''
        box.content[#box.content+1]=S.label(self.api.keyLabel(slot),4,1,cell-10,12,true)
        local clear=S.label('x',cell-17,1,13,12)
        clear.props.propagateEvents=false
        clear.events={mouseClick=cb(function() if not self.api.dropItem(index) then self.api.bind(index,nil) end end)}
        box.content[#box.content+1]=clear
        if info and info.icon then box.content[#box.content+1]={type=ui.TYPE.Image,props={position=v(12,16),size=v(cell-28,cell-28),resource=ui.texture{path=info.icon}}}
        else box.content[#box.content+1]=S.label(caption,5,21,cell-10,12) end
        if info and info.count then box.content[#box.content+1]=S.label(tostring(info.count),cell-27,cell-22,24,12) end
        box.events={mousePress=cb(function(e)
            if e.button~=1 then return end
            if not self.api.dropItem(index) then self.slotPress={binding=binding,position=e.position,index=index} end
        end),mouseMove=cb(function(e)
            if self.slotPress and self.slotPress.binding and e.button==1 and (e.position-self.slotPress.position):length()>4 then self.drag=self.slotPress.binding end
        end),mouseRelease=cb(function(e) if e.button==1 then self:release(e.position) end end),
        focusGain=cb(function() self.api.hint(info and info.label or 'Empty slot') end)}
        content[#content+1]=box
        self.rects[slot]={x=x+left,y=y+top,w=cell-3,h=cell-3,index=index}
    end
    for _,entry in ipairs{{'<',8,-1},{'>',width-62,1}} do
        local b=S.button(entry[1],entry[2],29,52,cell-10);b.events={mouseClick=cb(function() self.api.cycle(entry[3]) end)};content[#content+1]=b
    end
    self.bar=ui.create{type=ui.TYPE.Widget,layer='FetcherHotbar',props={position=v(x,y),size=v(width,height)},content=ui.content(content)}
end
function V:setHint(text)
    if not self.bar then return end
    local page,pages=self.api.page()
    self.bar.layout.content[2].props.text=('Page '..page..' / '..pages..'  |  '..text):sub(1,100)
    self.bar:update()
end
function V:slotAt(position)
    for _,r in ipairs(self.rects or {}) do if hit(position,r.x,r.y,r.w,r.h) then return r.index end end
end
function V:release(position)
    local index=self:slotAt(position)
    if index then
        if self.drag then self.api.bind(index,self.drag)
        else self.api.dropItem(index) end
    end
    self.press=nil;self.slotPress=nil;self.drag=nil
    if self.ghost then self.ghost:destroy();self.ghost=nil end
end
function V:frame(dt)
    self.clock=self.clock+dt
    if self.barViewport~=size() then self:drawBar() end
    if self.pendingPreview and self.clock>=self.pendingPreview then
        self.pendingPreview=nil;local entry=self.matches and self.matches[self.selected]
        if self.mode=='voices' and entry then self.api.preview(entry) end
    end
    if self.browser and self.screen~=size() then self:buildBrowser() end
    if self.drag then
        local p=ui.getMousePosition();local info=self.api.describe(self.drag)
        if not self.ghost then
            self.ghost=ui.create{type=ui.TYPE.Widget,layer='Notification',props={position=p+v(18,18),size=v(250,30)},
                content=ui.content{S.frame(0,0,250,30),S.label(info and info.label or self.drag.id,6,4,238,14)}}
        else self.ghost.layout.props.position=p+v(18,18);self.ghost:update() end
    elseif self.ghost then self.ghost:destroy();self.ghost=nil end
end
function V:destroy() self:close();if self.bar then self.bar:destroy();self.bar=nil end;if self.ghost then self.ghost:destroy();self.ghost=nil end end
return V
