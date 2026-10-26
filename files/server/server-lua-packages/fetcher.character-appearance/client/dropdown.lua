-- Searchable, virtualized selectors: create eight rows regardless of mod count.
local ui=require('openmw.ui')
local util=require('openmw.util')
local async=require('openmw.async')
local Style=require('scripts.multiplayer.fetcher.character-appearance.client.style')
local D={};D.__index=D
local v=util.vector2
local row=util.color.rgb(0,0,0)
local function callback(fn) return async:callback(function(...) fn(...);return false end) end
local function label(caption,x,y,w)
    return Style.label(caption,x,y,w,15)
end
function D.filter(entries,query)
    local result={};query=query:lower()
    for _,entry in ipairs(entries) do if (entry.search or entry.label):lower():find(query,1,true) then result[#result+1]=entry end end
    return result
end
function D.close(owner)
    local d=owner.dropdown;if not d then return end
    owner.dropdown=nil;d.active=false;ui.setFocus(nil)
    d.root:destroy();for _,element in ipairs(d.children) do element:destroy() end
end
function D.open(owner,entries,selected,choose,y)
    D.close(owner)
    local self=setmetatable({owner=owner,entries=entries,selected=selected,choose=choose,
        query='',offset=0,children={},rows={},active=true},D)
    owner.dropdown=self
    local function element(layout)
        local e=owner:create(layout);self.children[#self.children+1]=e;return e
    end
    local width=owner.width-32
    local height=340
    self.y=math.max(12,math.min(y,owner.height-height-12))
    local content={Style.frame(0,0,width,height,true),Style.title('Search / scroll to select',4,4,width-8,20,15),
        Style.frame(12,35,width-24,28)}
    self.search=element{template=Style.templates.textEditLine,
        props={position=v(17,39),size=v(width-34,21),text='',textSize=15,autoSize=false},
        events={textChanged=callback(function(value)
            if not self.active then return end
            self.query=value;self.offset=0;self.matches=D.filter(entries,value);self:draw()
        end)}}
    content[#content+1]=self.search
    for i=1,8 do
        local layout=Style.image(12,72+(i-1)*28,width-52,26,row)
        layout.content=ui.content{label('',6,2,width-66)}
        local function highlight(hover)
            if not self.active then return end
            local entry=self.visible and self.visible[i]
            self.rows[i].layout.content[1].props.textColor=(hover or entry and entry.id==self.selected) and Style.header or Style.normal
            self.rows[i]:update()
        end
        layout.events={focusGain=callback(function() highlight(true) end),focusLoss=callback(function() highlight(false) end),
            mouseClick=callback(function()
            local entry=self.visible and self.visible[i]
            if self.active and entry then D.close(owner);choose(entry.id) end
        end)}
        self.rows[i]=element(layout);content[#content+1]=self.rows[i]
    end
    local function drag(event,press)
        if not self.active or self.maximum==0 then return end
        local at=event.position and (event.position.y-self.screenY)/owner.scale or event.offset.y/owner.scale
        if press then
            self.grab=at>=self.thumbTop and at<=self.thumbTop+self.thumbHeight and at-self.thumbTop or self.thumbHeight/2
        end
        self:scroll((at-(self.grab or self.thumbHeight/2))/(224-self.thumbHeight)*self.maximum)
    end
    local events={mousePress=callback(function(e) if e.button==1 then drag(e,true) end end),
        mouseMove=callback(function(e) if e.button==1 then drag(e,false) end end)}
    local thumb=Style.filled(0,0,15,30);thumb.events=events
    self.thumb=element(thumb)
    local bar=Style.image(width-29,72,15,224,row);bar.events=events;bar.content=ui.content{self.thumb}
    content[#content+1]=bar
    self.footer=element(label('',12,309,width-24));content[#content+1]=self.footer
    for _,item in ipairs(content) do if not item.layout then owner:scaleLayout(item) end end
    self.screenY=owner.y+(self.y+72)*owner.scale
    self.root=ui.create{type=ui.TYPE.Widget,layer='Windows',props={position=v(owner.x+16*owner.scale,owner.y+self.y*owner.scale),
        size=v(width*owner.scale,height*owner.scale)},content=ui.content(content),events={
            mouseMove=callback(function() owner:area('dropdown') end),
            mousePress=callback(function() owner:area('dropdown');owner:dragEnd() end),
            focusGain=callback(function() owner:area('dropdown') end)}}
    ui.setFocus(self.search)
    self.matches=D.filter(entries,'')
    for i,entry in ipairs(self.matches) do if entry.id==selected then self.offset=math.max(0,i-4);break end end
    self:draw();return self
end
function D:scroll(value)
    if not self.active then return end
    self.offset=math.max(0,math.min(math.max(0,#self.matches-8),math.floor(value+.5)));self:draw()
end
function D:wheel(delta) self:scroll(self.offset-delta*3) end
function D:draw()
    self.maximum=math.max(0,#self.matches-8);self.offset=math.max(0,math.min(self.offset,self.maximum));self.visible={}
    for i,element in ipairs(self.rows) do
        local entry=self.matches[self.offset+i];self.visible[i]=entry
        element.layout.props.visible=entry~=nil
        element.layout.content[1].props.textColor=entry and entry.id==self.selected and Style.header or Style.normal
        element.layout.content[1].props.text=entry and ((entry.id==self.selected and '> ' or '')..entry.label) or ''
        element:update()
    end
    self.thumbHeight=math.min(224,math.max(20,224*8/math.max(8,#self.matches)))
    self.thumbTop=self.maximum>0 and (224-self.thumbHeight)*self.offset/self.maximum or 0
    self.thumb.layout.props.position=v(0,self.thumbTop*self.owner.scale)
    self.thumb.layout.props.size=v(15*self.owner.scale,self.thumbHeight*self.owner.scale);self.thumb:update()
    self.footer.layout.props.text=#self.matches==0 and 'No matches' or string.format('%d-%d of %d',self.offset+1,math.min(#self.matches,self.offset+8),#self.matches)
    self.footer:update()
end
return D
