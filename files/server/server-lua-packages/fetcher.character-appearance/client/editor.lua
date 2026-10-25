local ui=require('openmw.ui')
local util=require('openmw.util')
local async=require('openmw.async')
local input=require('openmw.input')
local I=require('openmw.interfaces')
local Catalogue=require('scripts.multiplayer.fetcher.character-appearance.shared.catalogue')
local Dropdown=require('scripts.multiplayer.fetcher.character-appearance.client.dropdown')
local Preview=require('scripts.multiplayer.fetcher.character-appearance.client.preview')
local Style=require('scripts.multiplayer.fetcher.character-appearance.client.style')
local E={};E.__index=E
local v=util.vector2
local function viewport()
    local index=ui.layers and ui.layers.indexOf('Windows')
    return index and ui.layers[index].size or ui.screenSize()
end
local function callback(fn) return async:callback(function(...) fn(...);return false end) end
function E:scaleLayout(layout)
    local p=layout.props
    if p then
        if p.position then p.position=p.position*self.scale end
        if p.size then p.size=p.size*self.scale end
        if p.textSize then p.textSize=math.max(11,math.floor(p.textSize*self.scale+.5)) end
    end
    if layout.content then for _,child in ipairs(layout.content) do if not child.layout then self:scaleLayout(child) end end end
end
function E:create(layout)
    self:scaleLayout(layout)
    return ui.create(layout)
end
function E:element(layout)
    local element=self:create(layout);self.children[#self.children+1]=element;return element
end
function E:button(caption,x,y,width,fn)
    local state={enabled=true}
    local layout=Style.button(caption,x,y,width,33)
    local function colour(hover)
        if self.closed then return end
        state.hover=hover
        state.element.layout.content[1].props.textColor=not state.enabled and Style.disabled or hover and Style.header or Style.normal
        state.element:update()
    end
    layout.events={mouseClick=callback(function() if not self.closed and state.enabled then fn() end end),
        focusGain=callback(function() colour(true) end),focusLoss=callback(function() colour(false) end)}
    state.element=self:element(layout);state.colour=colour;self.buttons[state.element]=state
    return state.element
end
function E:enable(element,enabled)
    local state=self.buttons[element];state.enabled=enabled;state.colour(state.hover)
end
function E:area(area)
    if not self.closed then self.pointerArea=area end
end
function E:dragStart(event)
    self:area('preview')
    if self.closed or event.button~=1 or not event.position then return end
    Dropdown.close(self);self.drag=event.position
end
function E:dragMove(event)
    self:area('preview')
    if self.closed or not self.drag or not event.position then return end
    if event.button~=1 then self.drag=nil;return end
    local delta=event.position-self.drag;self.drag=event.position
    self.view:rotate(delta.x/self.scale,delta.y/self.scale)
end
function E:dragEnd() self.drag=nil end
function E.new(catalogue,selection,save,cancel)
    local screen=viewport()
    local self=setmetatable({catalogue=catalogue,selection=Catalogue.copy(selection),save=save,cancel=cancel,
        children={},fields={},cycles={},choices={},buttons={},width=410,height=610,pointerArea='preview',
        scale=math.max(.65,math.min(1.5,screen.y/780)),view=Preview.new()},E)
    self.scale=math.min(self.scale,(screen.x-20)/self.width,(screen.y-20)/self.height)
    self.x=math.max(10,screen.x-self.width*self.scale-20);self.y=(screen.y-self.height*self.scale)/2
    local ok,problem=pcall(function()
        -- The transparent input surface sits behind the panel and dropdowns.
        -- Only a press in the model view can begin an orbit gesture.
        self.scene=ui.create{type=ui.TYPE.Widget,layer='Windows',props={relativeSize=v(1,1)},events={
            mousePress=callback(function(e) self:dragStart(e) end),
            mouseMove=callback(function(e) self:dragMove(e) end),
            mouseRelease=callback(function() self:dragEnd() end),
            focusGain=callback(function() self:area('preview') end)}}
        local content={Style.frame(0,0,self.width,self.height,true),
            Style.label('Character Appearance',16,16,self.width-32,19,true),
            Style.label('Preview your changes, then Apply to save.',16,49,self.width-32,14)}
        local function field(key,title,y,entries,choose)
            self.choices[key]={entries=entries,choose=choose}
            content[#content+1]=Style.label(title,16,y,self.width-32)
            local left=self:button('<',16,y+28,32,function() self:cycle(key,-1) end)
            local button=self:button('',54,y+28,self.width-108,function()
                if self.pending then return end
                self:dragEnd();Dropdown.open(self,entries(),self.selection[key],choose,y+65)
            end)
            local right=self:button('>',self.width-48,y+28,32,function() self:cycle(key,1) end)
            self.fields[key]=button;self.cycles[key]={left,right}
            content[#content+1]=left;content[#content+1]=button;content[#content+1]=right
        end
        field('race','Race',90,function() return catalogue.races end,function(value) self:choose(value,self.selection.isMale) end)
        field('isMale','Sex',168,function() return {{id=true,label='Male'},{id=false,label='Female'}} end,
            function(value) self:choose(self.selection.race,value) end)
        field('head','Head',246,function() local entries=catalogue:choices(self.selection.race,self.selection.isMale);return entries end,
            function(value) self.selection.head=value;self:preview() end)
        field('hair','Hair',324,function() local _,entries=catalogue:choices(self.selection.race,self.selection.isMale);return entries end,
            function(value) self.selection.hair=value;self:preview() end)
        self.partsMode=self:button('',16,405,self.width-32,function() self:toggleParts() end)
        content[#content+1]=self.partsMode
        content[#content+1]=Style.label('Left-drag the model view to orbit the character.',16,449,self.width-32,14)
        content[#content+1]=Style.label('Mouse wheel: zoom. Wheel over a list: scroll.',16,473,self.width-32,14)
        self.status=self:element(Style.label('Stats and inventory are preserved.',16,507,self.width-32,14))
        content[#content+1]=self.status
        self.apply=self:button('Apply',16,545,181,function() self:submit() end)
        content[#content+1]=self.apply
        content[#content+1]=self:button('Cancel / Esc',213,545,181,function() if not self.pending then self.cancel() end end)
        for _,item in ipairs(content) do if not item.layout then self:scaleLayout(item) end end
        self.root=ui.create{type=ui.TYPE.Widget,layer='Windows',props={position=v(self.x,self.y),
            size=v(self.width*self.scale,self.height*self.scale)},content=ui.content(content),events={
            mouseMove=callback(function() self:area('panel') end),
            mousePress=callback(function() self:area('panel');self:dragEnd() end),
            focusGain=callback(function() self:area('panel') end)}}
        I.UI.addMode('Interface',{windows={}});self.ownsMode=true;self:refresh()
    end)
    if not ok then self:close();error(problem) end
    return self
end
function E:cycle(key,direction)
    if self.closed or self.pending then return false end
    local field=self.choices[key];if not field then return false end
    local entries=field.entries();if #entries==0 then return false end
    local index=direction>0 and 0 or 1
    for i,entry in ipairs(entries) do
        local a,b=entry.id,self.selection[key]
        if a==b or type(a)=='string' and type(b)=='string' and a:lower()==b:lower() then index=i;break end
    end
    Dropdown.close(self);self:dragEnd()
    field.choose(entries[(index-1+direction)%#entries+1].id);return true
end
function E:setStatus(text,bad)
    self.status.layout.props.text=text;self.status.layout.props.textColor=bad and Style.bad or Style.normal;self.status:update()
end
function E:toggleParts()
    if self.closed or self.pending then return false end
    Dropdown.close(self);self:dragEnd()
    self.catalogue.allRaces=not self.catalogue.allRaces
    self:choose(self.selection.race,self.selection.isMale);return true
end
function E:refresh()
    self.fields.race.layout.content[1].props.text=(self.catalogue.index[self.selection.race:lower()] or {}).label or self.selection.race
    self.fields.isMale.layout.content[1].props.text=self.selection.isMale and 'Male' or 'Female'
    self.fields.head.layout.content[1].props.text=self.selection.head~='' and self.selection.head or 'No playable heads'
    self.fields.hair.layout.content[1].props.text=self.selection.hair~='' and self.selection.hair or 'No hair / bald'
    for key,element in pairs(self.fields) do
        local n=#self.choices[key].entries();self:enable(element,not self.pending and n>0)
        for _,button in ipairs(self.cycles[key]) do self:enable(button,not self.pending and n>1) end
        element:update()
    end
    self.apply.layout.content[1].props.text=self.pending and 'Saving...' or 'Apply'
    self.partsMode.layout.content[1].props.text='Heads / hair: '..(self.catalogue.allRaces and 'All races' or 'Matching race')
    self:enable(self.partsMode,not self.pending)
    self:enable(self.apply,not self.pending and self.catalogue:contains(self.selection))
end
function E:choose(race,isMale)
    self.selection=self.catalogue:choose(self.selection,race,isMale);self:preview()
end
function E:preview()
    if self.catalogue:contains(self.selection) then
        local ok,error=pcall(self.view.apply,self.view,self.selection)
        self:setStatus(ok and 'Local preview. Apply to save; Cancel to restore.' or tostring(error),not ok)
    else self:setStatus('No playable heads for this race and sex.',true) end
    self:refresh()
end
function E:result(data)
    self.pending=false;self:setStatus(data.error or 'The server could not save this appearance.',true);self:refresh()
end
function E:submit()
    if self.closed or self.pending or not self.catalogue:contains(self.selection) then return false end
    self.pending=true;Dropdown.close(self);self:setStatus('Saving on the server...')
    self.save(Catalogue.copy(self.selection));self:refresh();return true
end
function E:frame()
    if self.closed then return end
    if self.drag and not input.isMouseButtonPressed(1) then self:dragEnd() end
    self.view:frame()
end
function E:wheel(delta)
    if self.closed then return end
    if self.dropdown and self.pointerArea=='dropdown' then self.dropdown:wheel(delta)
    elseif self.pointerArea=='preview' then self.view:zoomBy(delta) end
end
function E:close()
    if self.closed then return end
    self.closed=true;self:dragEnd();Dropdown.close(self)
    self.view:close();if self.root then self.root:destroy() end
    if self.scene then self.scene:destroy() end
    for _,element in ipairs(self.children) do element:destroy() end
    if self.ownsMode and I.UI.getMode()=='Interface' then I.UI.removeMode('Interface') end
end
return E
