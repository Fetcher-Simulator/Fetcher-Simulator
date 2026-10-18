-- Virtualized selector: persistent widgets preserve mouse capture and text focus.
local ui=require('openmw.ui');local util=require('openmw.util');local async=require('openmw.async')
local D={};D.__index=D;local v=util.vector2;local white=ui.texture{path='white'}
local bg=util.color.rgb(0.07,0.09,0.12);local row=util.color.rgb(0.14,0.17,0.21)
local text=util.color.rgb(0.85,0.89,0.94);local accent=util.color.rgb(0.2,0.85,0.72)
local function cb(fn) return async:callback(function(...) fn(...);return false end) end
local function label(s,x,y,w) return {type=ui.TYPE.Text,props={text=s,position=v(x,y),size=v(w,22),textSize=13,textColor=text}} end
local function rect(x,y,w,h,color) return {type=ui.TYPE.Image,props={position=v(x,y),size=v(w,h),resource=white,color=color}} end
function D.filtered(entries,query)
    local result={};query=(query or ''):lower()
    for _,entry in ipairs(entries) do if (entry.search or entry.label):lower():find(query,1,true) then result[#result+1]=entry end end
    return result
end
function D:element(layout)
    local e=self.owner.window:create(layout);self.children[#self.children+1]=e;return e
end
function D.open(owner,entries,selected,choose,x,y,width)
    D.close(owner);if owner.releaseAll then owner:releaseAll() end
    local offset=0;for n,e in ipairs(entries) do if e.id==selected then offset=math.max(0,n-4) end end
    local self=setmetatable({owner=owner,entries=entries,selected=selected,choose=choose,offset=offset,query='',active=true,children={},rows={}},D)
    owner.dropdown=self;owner.typing=true;self.anchor=owner.root
    x=math.min(x,920-width);y=math.min(y,300);local w=width;local s=owner.window.scale;local p=owner.root.layout.props
    self.barScreenY=p.position.y+(38+y+72)*s
    local content={};local backdrop=rect(0,0,940,695,util.color.rgba(0,0,0,0.25))
    backdrop.events={mouseClick=cb(function() D.close(owner) end)};content[#content+1]=backdrop
    local panel=rect(x,y,w,360,bg);panel.events={mouseClick=cb(function() end)};content[#content+1]=panel
    content[#content+1]=label('Filter by name:',x+12,y+8,w-100)
    local close=rect(x+w-75,y+6,63,27,row);close.content=ui.content{label('Close',7,4,51)}
    close.events={mouseClick=cb(function() D.close(owner) end)};content[#content+1]=close
    self.field=self:element{type=ui.TYPE.TextEdit,template={content=ui.content{{type=ui.TYPE.Image,props={relativeSize=v(1,1),resource=white,color=row}}}},
        props={position=v(x+12,y+36),size=v(w-24,28),text='',textSize=14,textColor=accent},events={
        textChanged=cb(function(value) if self.active then self.query=value:sub(1,128);self.field.layout.props.text=self.query;self.offset=0;self:draw() end end),
        focusGain=cb(function() owner.typing=true end),focusLoss=cb(function() owner.typing=self.active end)}}
    content[#content+1]=self.field
    for n=1,8 do
        local layout=rect(x+12,y+72+(n-1)*31,w-48,27,row);layout.content=ui.content{label('',7,4,w-60)}
        layout.events={mouseClick=cb(function()
            local entry=self.visibleEntries[n];if self.active and entry then D.close(owner);choose(entry.id) end
        end)}
        self.rows[n]=self:element(layout);content[#content+1]=self.rows[n]
    end
    local function drag(e,press)
        if not self.active or self.maximum==0 then return end
        local at=e.position and (e.position.y-self.barScreenY)/s or e.offset.y/s
        if press then self.grab=at>=self.thumbTop and at<=self.thumbTop+self.thumbHeight and at-self.thumbTop or self.thumbHeight/2 end
        self:scrollTo((at-(self.grab or self.thumbHeight/2))/(244-self.thumbHeight)*self.maximum)
    end
    local events={mousePress=cb(function(e) if e.button==1 then drag(e,true) end end),mouseMove=cb(function(e) if e.button==1 then drag(e,false) end end)}
    local thumb=rect(0,0,15,22,accent);thumb.events=events;self.thumb=self:element(thumb)
    local bar=rect(x+w-27,y+72,15,244,row);bar.events=events;bar.content=ui.content{self.thumb}
    self.bar=self:element(bar);content[#content+1]=self.bar
    self.footer=self:element(label('',x+12,y+331,w-24));content[#content+1]=self.footer
    for _,item in ipairs(content) do if item.layout then owner.window:update(item) else owner.window:layout(item) end end
    self.element=ui.create{type=ui.TYPE.Widget,layer='Popup',props={position=v(p.position.x,p.position.y+38*s),size=v(940*s,695*s)},content=ui.content(content)}
    self:draw();return self
end
function D.close(owner)
    local d=owner.dropdown;if not d then return false end
    d.active=false;owner.dropdown=nil;owner.typing=false
    d.element:destroy();for _,e in ipairs(d.children) do e:destroy() end;return true
end
function D.frame(owner) if owner.dropdown and owner.dropdown.anchor~=owner.root then D.close(owner) end end
function D:scrollTo(offset)
    if not self.active then return end
    self.offset=math.max(0,math.min(math.max(0,#D.filtered(self.entries,self.query)-8),math.floor(offset+0.5)));self:draw()
end
function D:wheel(delta)
    if not self.active then return end
    self.wheelDelta=(self.wheelDelta or 0)-delta*3
    local rows=self.wheelDelta>=0 and math.floor(self.wheelDelta) or math.ceil(self.wheelDelta)
    if rows~=0 then self.wheelDelta=self.wheelDelta-rows;self:scrollTo(self.offset+rows) end
end
function D:draw()
    local entries=D.filtered(self.entries,self.query);local owner=self.owner
    self.maximum=math.max(0,#entries-8);self.offset=math.max(0,math.min(self.maximum,self.offset));self.visibleEntries={}
    for n,e in ipairs(self.rows) do
        local entry=entries[self.offset+n];self.visibleEntries[n]=entry
        e.layout.props.visible=entry~=nil
        e.layout.props.color=entry and entry.id==self.selected and accent or row
        e.layout.content[1].props.text=entry and ((entry.id==self.selected and '> ' or '')..entry.label) or ''
        owner.window:update(e)
    end
    self.thumbHeight=math.min(244,math.max(22,244*8/math.max(8,#entries)))
    self.thumbTop=self.maximum>0 and (244-self.thumbHeight)*self.offset/self.maximum or 0
    self.thumb.layout.props.position=v(0,self.thumbTop);self.thumb.layout.props.size=v(15,self.thumbHeight);owner.window:update(self.thumb)
    self.footer.layout.props.text=#entries==0 and 'No matches' or string.format('%d-%d of %d matches  |  Scroll or drag the bar',self.offset+1,math.min(#entries,self.offset+8),#entries)
    owner.window:update(self.footer)
end
return D
