local ui=require('openmw.ui')
local util=require('openmw.util')
local async=require('openmw.async')
local I=require('openmw.interfaces')
local Catalogue=require('scripts.multiplayer.fetcher.character-appearance.shared.catalogue')
local Dropdown=require('scripts.multiplayer.fetcher.character-appearance.client.dropdown')
local Preview=require('scripts.multiplayer.fetcher.character-appearance.client.preview')
local E={};E.__index=E
local v=util.vector2
local white=ui.texture{path='white'}
local colors={bg=util.color.rgb(.055,.06,.07),row=util.color.rgb(.15,.16,.18),
    text=util.color.rgb(.9,.91,.93),accent=util.color.rgb(.18,.8,.69),bad=util.color.rgb(1,.5,.4)}
local function viewport()
    local index=ui.layers and ui.layers.indexOf('Windows')
    return index and ui.layers[index].size or ui.screenSize()
end
local function callback(fn) return async:callback(function(...) fn(...);return false end) end
local function rect(x,y,w,h,color)
    return {type=ui.TYPE.Image,props={position=v(x,y),size=v(w,h),resource=white,color=color}}
end
local function label(text,x,y,w,size)
    return {type=ui.TYPE.Text,props={position=v(x,y),size=v(w,26),text=text,textSize=size or 16,textColor=colors.text}}
end
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
    local layout=rect(x,y,width,33,colors.row)
    layout.content=ui.content{label(caption,8,6,width-16,15)}
    layout.events={mouseClick=callback(function() if not self.closed then fn() end end)}
    return self:element(layout)
end
function E.new(catalogue,selection,save,cancel)
    local screen=viewport()
    local self=setmetatable({catalogue=catalogue,selection=Catalogue.copy(selection),save=save,cancel=cancel,
        children={},fields={},width=390,height=585,scale=math.max(.65,math.min(1.5,screen.y/780)),view=Preview.new()},E)
    self.scale=math.min(self.scale,(screen.x-20)/self.width,(screen.y-20)/self.height)
    self.x=math.max(10,screen.x-self.width*self.scale-20);self.y=(screen.y-self.height*self.scale)/2
    local ok,problem=pcall(function()
    local content={rect(0,0,self.width,self.height,colors.bg),label('CHARACTER APPEARANCE',16,16,self.width-32,19),
        label('Preview locally. Apply saves to your character.',16,49,self.width-32,13)}
    local function field(key,title,y,entries,choose)
        content[#content+1]=label(title,16,y,self.width-32)
        local button=self:button('',16,y+28,self.width-32,function()
            if self.pending then return end
            Dropdown.open(self,entries(),self.selection[key],choose,y+65)
        end)
        self.fields[key]=button;content[#content+1]=button
    end
    field('race','Race',90,function() return catalogue.races end,function(value) self:choose(value,self.selection.isMale) end)
    field('isMale','Sex',168,function() return {{id=true,label='Male'},{id=false,label='Female'}} end,
        function(value) self:choose(self.selection.race,value) end)
    field('head','Head',246,function() local entries=catalogue:choices(self.selection.race,self.selection.isMale);return entries end,
        function(value) self.selection.head=value;self:preview() end)
    field('hair','Hair',324,function() local _,entries=catalogue:choices(self.selection.race,self.selection.isMale);return entries end,
        function(value) self.selection.hair=value;self:preview() end)
    content[#content+1]=self:button('Rotate left',16,409,112,function() self.view.orbit=self.view.orbit-.35 end)
    content[#content+1]=self:button('Rotate right',134,409,112,function() self.view.orbit=self.view.orbit+.35 end)
    content[#content+1]=self:button('Zoom +',252,409,58,function() self.view.zoom=math.max(.35,self.view.zoom-.1) end)
    content[#content+1]=self:button('Zoom -',316,409,58,function() self.view.zoom=math.min(2.5,self.view.zoom+.1) end)
    self.status=self:element(label('Stats, inventory and progression are preserved.',16,461,self.width-32,13));content[#content+1]=self.status
    self.apply=self:button('Apply',16,505,172,function()
        self:submit()
    end)
    content[#content+1]=self.apply
    content[#content+1]=self:button('Cancel / Esc',202,505,172,function() if not self.pending then self.cancel() end end)
    content[#content+1]=label('F6 opens this editor. Changes stay private until Apply.',16,551,self.width-32,12)
    for _,item in ipairs(content) do if not item.layout then self:scaleLayout(item) end end
    self.root=ui.create{type=ui.TYPE.Widget,layer='Windows',props={position=v(self.x,self.y),
        size=v(self.width*self.scale,self.height*self.scale)},content=ui.content(content)}
    I.UI.addMode('Interface',{windows={}})
    self.ownsMode=true
    self:refresh()
    end)
    if not ok then self:close();error(problem) end
    return self
end
function E:setStatus(text,bad)
    self.status.layout.props.text=text;self.status.layout.props.textColor=bad and colors.bad or colors.text;self.status:update()
end
function E:refresh()
    self.fields.race.layout.content[1].props.text=(self.catalogue.index[self.selection.race:lower()] or {}).label or self.selection.race
    self.fields.isMale.layout.content[1].props.text=self.selection.isMale and 'Male' or 'Female'
    self.fields.head.layout.content[1].props.text=self.selection.head~='' and self.selection.head or 'No playable heads for this sex'
    self.fields.hair.layout.content[1].props.text=self.selection.hair~='' and self.selection.hair or 'No hair / bald'
    for _,element in pairs(self.fields) do element:update() end
    self.apply.layout.content[1].props.text=self.pending and 'Saving...' or 'Apply'
    self.apply.layout.props.color=self.catalogue:contains(self.selection) and colors.accent or colors.row;self.apply:update()
end
function E:choose(race,isMale)
    self.selection=self.catalogue:choose(self.selection,race,isMale);self:preview()
end
function E:preview()
    if self.catalogue:contains(self.selection) then
        local ok,error=pcall(self.view.apply,self.view,self.selection)
        self:setStatus(ok and 'Local preview. Apply to save; Cancel to restore.' or tostring(error),not ok)
    else self:setStatus('This race has no playable heads for the selected sex.',true) end
    self:refresh()
end
function E:result(data)
    self.pending=false
    self:setStatus(data.error or 'The server could not save this appearance.',true);self:refresh()
end
function E:submit()
    if self.closed or self.pending or not self.catalogue:contains(self.selection) then return false end
    self.pending=true;self:setStatus('Saving on the server...')
    self.save(Catalogue.copy(self.selection));self:refresh();return true
end
function E:frame()
    if not self.closed then self.view:frame() end
end
function E:wheel(delta) if self.dropdown then self.dropdown:wheel(delta) end end
function E:close()
    if self.closed then return end
    self.closed=true;Dropdown.close(self)
    self.view:close();if self.root then self.root:destroy() end
    for _,element in ipairs(self.children) do element:destroy() end
    if self.ownsMode and I.UI.getMode()=='Interface' then I.UI.removeMode('Interface') end
end
return E
