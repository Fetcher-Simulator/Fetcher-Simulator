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
        local props=state.element.layout.content[1].props
        if caption=='<' or caption=='>' then
            props.color=util.color.rgb(1,1,1);props.alpha=state.enabled and (hover and 1 or .85) or .35
        else props.textColor=not state.enabled and Style.disabled or hover and Style.header or Style.normal end
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
function E:inPreview(position)
    return position and position.x>=0 and position.x<self.x and position.y>=0 and position.y<viewport().y
end
function E:dragStart(event)
    if self.closed then return end
    if not self:inPreview(event.position) then self:area('panel');self:dragEnd();return end
    self:area('preview')
    if event.button~=1 then return end
    Dropdown.close(self);self.drag=event.position
end
function E:dragMove(event)
    if self.closed then return end
    if not self:inPreview(event.position) then self:area('panel');self:dragEnd();return end
    self:area('preview')
    if not self.drag then return end
    if event.button~=1 then self.drag=nil;return end
    local delta=event.position-self.drag;self.drag=event.position
    self.view:rotate(delta.x/self.scale,delta.y/self.scale)
end
function E:dragEnd() self.drag=nil end
function E.new(catalogue,selection,save,cancel)
    local screen=viewport()
    local self=setmetatable({catalogue=catalogue,selection=Catalogue.copy(selection),save=save,cancel=cancel,
        children={},fields={},cycles={},choices={},buttons={},groups={character={},body={}},page='character',bodySlot='neck',width=410,height=610,pointerArea='preview',
        scale=math.max(.65,math.min(1.5,screen.y/780)),view=Preview.new()},E)
    self.scale=math.min(self.scale,(screen.x*.44-20)/self.width,(screen.y-20)/self.height)
    self.x=math.max(10,screen.x-self.width*self.scale-20);self.y=(screen.y-self.height*self.scale)/2
    local ok,problem=pcall(function()
        -- Never place a drag widget underneath the controls. MyGUI can raise
        -- a focused root above later windows; disjoint rectangles stay safe.
        self.scene=ui.create{type=ui.TYPE.Widget,layer='Windows',props={size=v(self.x,screen.y)},events={
            mousePress=callback(function(e) self:dragStart(e) end),
            mouseMove=callback(function(e) self:dragMove(e) end),
            mouseRelease=callback(function() self:dragEnd() end),
            focusGain=callback(function() self:area('preview') end)}}
        local content={Style.frame(0,0,self.width,self.height,true),
            Style.title('Character Appearance',4,4,self.width-8,20,19),
            self:button('Character / Identity    |    Body / Rig >',16,44,self.width-32,function() self:pageTo(self.page=='character' and 'body' or 'character') end)}
        local function field(key,title,y,entries,choose,group)
            local first=#content+1;group=group or self.groups.character
            self.choices[key]={entries=entries,choose=choose}
            content[#content+1]=Style.label(title,16,y,self.width-32)
            local left=self:button('<',16,y+28,32,function() self:cycle(key,-1) end)
            local button=self:button('',54,y+28,self.width-108,function()
                if self.pending then return end
                self:dragEnd();Dropdown.open(self,entries(),self:value(key),choose,math.min(y+65,360))
            end)
            local right=self:button('>',self.width-48,y+28,32,function() self:cycle(key,1) end)
            self.fields[key]=button;self.cycles[key]={left,right}
            content[#content+1]=left;content[#content+1]=button;content[#content+1]=right
            for i=first,#content do group[#group+1]=content[i] end
        end
        field('race','Identity race (voice / racial powers)',90,function() return catalogue.races end,function(value)
                if self.selection.bodyRace=='' then self.selection.bodyRace=self.selection.race end
                self.selection.race=value;self:preview()
            end)
        field('isMale','Sex',168,function() return {{id=true,label='Male'},{id=false,label='Female'}} end,
            function(value) self:choose(self.selection.race,value) end)
        field('head','Head',246,function() local entries=catalogue:choices(self:visualRace(),self.selection.isMale);return entries end,
            function(value) self.selection.head=value;self:preview() end)
        field('hair','Hair',324,function() local _,entries=catalogue:choices(self:visualRace(),self.selection.isMale);return entries end,
            function(value) self.selection.hair=value;self:preview() end)
        local body=self.groups.body
        field('bodyRace','Body race (skin / proportions)',90,function() return catalogue.races end,function(value)
            self.selection.bodyRace=value;self:choose(self.selection.race,self.selection.isMale)
        end,body)
        field('rig','Animation rig',168,function() return {{id=0,label='Automatic (body race)'},{id=1,label='Humanoid'},{id=2,label='Beast'}} end,
            function(value) self.selection.rig=value;self:preview() end,body)
        field('tail','Tail visibility',246,function() return {{id=true,label='Show tail (if available)'},{id=false,label='No tail'}} end,
            function(value) self.selection.tail=value;self:preview() end,body)
        field('bodySlot','Body part to edit',324,function()
            local result={};for _,slot in ipairs(Catalogue.bodySlots) do result[#result+1]={id=slot.key,label=slot.label} end;return result
        end,function(value) self.bodySlot=value;self:refresh() end,body)
        field('bodyPart','Skin mesh: any race / sex',402,function() return catalogue:bodyChoices(self.bodySlot) end,
            function(value) self.selection.parts[self.bodySlot]=value;self:preview() end,body)
        self.partsMode=self:button('',16,405,self.width-32,function() self:toggleParts() end)
        content[#content+1]=self.partsMode;self.groups.character[#self.groups.character+1]=self.partsMode
        content[#content+1]=Style.label('Drag: orbit. Wheel: zoom. Use Beast rig for tails.',16,477,self.width-32,12)
        self.status=self:element(Style.label('Stats and inventory are preserved.',16,513,self.width-32,14))
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
        I.UI.addMode('Interface',{windows={}});ui.setFocus(nil);self.ownsMode=true;self:pageTo(self.page);self:refresh()
    end)
    if not ok then self:close();error(problem) end
    return self
end
function E:visualRace() return self.selection.bodyRace~='' and self.selection.bodyRace or self.selection.race end
function E:value(key)
    if key=='bodySlot' then return self.bodySlot end
    if key=='bodyPart' then return self.selection.parts[self.bodySlot] or '' end
    if key=='bodyRace' then return self:visualRace() end
    return self.selection[key]
end
function E:pageTo(page)
    if self.closed or self.pending then return end
    Dropdown.close(self);self:dragEnd();self.page=page
    for name,items in pairs(self.groups) do for _,item in ipairs(items) do
        local layout=item.layout or item;layout.props.visible=name==page
        if item.layout then item:update() end
    end end
    if self.root then self.root:update() end
end
function E:cycle(key,direction)
    if self.closed or self.pending then return false end
    local field=self.choices[key];if not field then return false end
    local entries=field.entries();if #entries==0 then return false end
    local index=direction>0 and 0 or 1
    for i,entry in ipairs(entries) do
        local a,b=entry.id,self:value(key)
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
    for key,element in pairs(self.fields) do
        local current=self:value(key);local label=tostring(current)
        for _,entry in ipairs(self.choices[key].entries()) do
            if entry.id==current or type(entry.id)=='string' and type(current)=='string' and entry.id:lower()==current:lower() then
                label=entry.label;break
            end
        end
        -- Clip long names within the selector; keep complete UTF-8 text in its list.
        element.layout.content[1].props.autoSize=false
        element.layout.content[1].props.text=label
    end
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
    local identity=race
    self.selection=self.catalogue:choose(self.selection,self:visualRace(),isMale)
    self.selection.race=identity;self:preview()
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
    local screen=viewport()
    local x=math.max(10,screen.x-self.width*self.scale-20)
    local y=math.max(0,(screen.y-self.height*self.scale)/2)
    if x~=self.x or y~=self.y or self.scene.layout.props.size.y~=screen.y then
        self:dragEnd();Dropdown.close(self)
        self.x=x;self.y=y
        self.root.layout.props.position=v(x,y);self.root:update()
        self.scene.layout.props.size=v(x,screen.y);self.scene:update()
    end
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
