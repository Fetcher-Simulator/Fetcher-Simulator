-- Shared local window preferences. Neither geometry nor monitoring is a project edit.
local ui=require('openmw.ui')
local util=require('openmw.util')
local async=require('openmw.async')
local input=require('openmw.input')
local storage=require('openmw.storage').playerSection('ProceduralSynthWindow')
local synth=require('openmw.synth')
local v=util.vector2
local W={};W.__index=W
local width,height,chrome=940,695,38
local white=ui.texture{path='white'}
local bg=util.color.rgb(0.035,0.045,0.063)
local track=util.color.rgb(0.14,0.17,0.21)
local text=util.color.rgb(0.85,0.89,0.94)
local accent=util.color.rgb(0.2,0.85,0.72)
local function clamp(x,a,b) return math.max(a,math.min(b,x)) end
local function number(x,fallback) return type(x)=='number' and x==x and math.abs(x)<100000 and x or fallback end
local function viewport()
    -- Widget coordinates use the scaled MyGUI layer, while screenSize returns
    -- physical pixels. Mixing them clips fullscreen controls above 100% GUI scale.
    local layers=ui.layers
    local index=layers and layers.indexOf('Windows')
    if index then return layers[index].size end
    return ui.screenSize()
end
local function rect(x,y,w,h,color)
    return {type=ui.TYPE.Image,props={position=v(x,y),size=v(w,h),resource=white,color=color}}
end
local function cb(fn) return async:callback(function(...) fn(...);return false end) end
local function windowLayer()
    local layers=ui.layers
    if not layers or not layers.insertAfter or not layers.indexOf then return 'Windows' end
    if not layers.indexOf('FetcherStudio') then
        layers.insertAfter(layers.indexOf('FetcherHotbar') and 'FetcherHotbar' or 'Windows','FetcherStudio',{interactive=true})
    end
    return 'FetcherStudio'
end
W.layer=windowLayer
function W.new()
    local screen=viewport()
    local defaultScale=math.min(math.max(1,screen.y/900),screen.x/width,screen.y/(height+chrome))
    local scale=clamp(number(storage:get('scale'),defaultScale),0.75,2)
    local saved=storage:get('geometry') or {}
    local self=setmetatable({preferred=scale,full=storage:get('fullscreen')==true,
        x=number(saved.x,(screen.x-width*scale)/2),y=number(saved.y,(screen.y-(height+chrome)*scale)/2),
        width=number(saved.width,width*scale),height=number(saved.height,(height+chrome)*scale),
        originals=setmetatable({},{__mode='k'}),changed=true},W)
    self:bounds();return self
end
function W:isFullscreen() return self.full or self.expanded end
function W:bounds()
    local screen=viewport();self.screen=screen
    if not self:isFullscreen() then
        self.width=clamp(self.width,math.min(screen.x,width*0.75),screen.x)
        self.height=clamp(self.height,math.min(screen.y,(height+chrome)*0.75),screen.y)
        self.x=clamp(self.x,0,screen.x-self.width);self.y=clamp(self.y,0,screen.y-self.height)
    end
    local w,h=self.width,self.height
    if self:isFullscreen() then w,h=screen.x,screen.y end
    self.scale=math.min(self.preferred,w/width,h/(height+chrome))
    self.actualWidth,self.actualHeight=w,h
    self.canvasWidth,self.canvasHeight=w/self.scale,h/self.scale-chrome
end
function W:save()
    storage:set('geometry',{x=self.x,y=self.y,width=self.width,height=self.height})
    storage:set('scale',self.preferred);storage:set('fullscreen',self.full)
end
function W:zoom(delta)
    self.preferred=clamp(math.floor((self.preferred+delta)*100+0.5)/100,0.75,2)
    if not self:isFullscreen() then self.width=width*self.preferred;self.height=(height+chrome)*self.preferred end
    self:bounds();self:save();self.changed=true
end
function W:fullscreen()
    self.full=not self.full;self:bounds();self:save();self.changed=true
end
function W:drag(e)
    if self:isFullscreen() then return end
    local resized=math.abs(e.size.x-self.width)>1 or math.abs(e.size.y-self.height)>1
    self.x,self.y=e.position.x,e.position.y;self.width,self.height=e.size.x,e.size.y
    if resized then self.preferred=clamp(math.min(self.width/width,self.height/(height+chrome)),0.75,2) end
    self:bounds();self:save();self.changed=true
end
-- Keep logical coordinates so live meters, slider refreshes and repeated resizing
-- never compound the scale. Independently created child Elements are updated too.
function W:layout(layout)
    local p=layout.props
    if p then
        local old=self.originals[p] or {};self.originals[p]=old
        for _,key in ipairs({'position','size','textSize'}) do
            local value=p[key]
            if value~=nil then
                local entry=old[key]
                if not entry or value~=entry.scaled then entry={logical=value};old[key]=entry end
                local n=entry.logical
                entry.scaled=key=='textSize' and math.max(9,math.floor(n*self.scale+0.5)) or
                    (key~='textSize' and v(n.x*self.scale,n.y*self.scale))
                p[key]=entry.scaled
            end
        end
    end
    if layout.content then for _,child in ipairs(layout.content) do
        if child.layout then self:update(child) else self:layout(child) end
    end end
end
function W:create(layout) self:layout(layout);return ui.create(layout) end
function W:update(element) self:layout(element.layout);element:update() end
function W:button(caption,x,w,fn)
    local r=rect(x,5,w,28,track)
    r.content=ui.content{{type=ui.TYPE.Text,props={position=v(6,5),size=v(w-8,22),text=caption,textSize=13,textColor=text}}}
    r.events={mouseClick=cb(fn)};return r
end
function W:root(content)
    self:bounds()
    for _,child in ipairs(content) do if child.layout then self:update(child) else self:layout(child) end end
    local s=self.scale
    local bar=rect(0,0,self.actualWidth/s,chrome,track)
    local title={type=ui.TYPE.Text,external={action=true},props={position=v(10,9),size=v(160,24),
        text=self:isFullscreen() and 'FETCHER STUDIO' or 'Drag to move',textSize=13,textColor=accent}}
    local volume=synth.getMonitorVolume and synth.getMonitorVolume() or 1
    local monitor=rect(654,12,110,12,bg)
    monitor.content=ui.content{rect(0,0,110*volume,12,accent)}
    local function set(e) if synth.setMonitorVolume then synth.setMonitorVolume(clamp(e.offset.x/(110*s),0,1));self.changed=true end end
    monitor.events={mousePress=cb(set),mouseMove=cb(function(e) if e.button==1 then set(e) end end)}
    bar.content=ui.content{title,self:button('Scale -',178,62,function() self:zoom(-0.1) end),
        {type=ui.TYPE.Text,props={position=v(248,10),size=v(58,22),text=string.format('%.0f%%',s*100),textSize=13,textColor=text}},
        self:button('+',307,28,function() self:zoom(0.1) end),
        self:button(self.expanded and 'Back to Piano' or self.full and 'Restore' or 'Fullscreen',345,103,function() if self.expanded then self.restoreExpanded() else self:fullscreen() end end),
        {type=ui.TYPE.Text,props={position=v(470,10),size=v(181,22),text='Monitor Volume (local)',textSize=13,textColor=text}},monitor,
        {type=ui.TYPE.Text,props={position=v(775,9),size=v(62,24),text=string.format('%.0f%%',volume*100),textSize=13,textColor=accent}}}
    -- Action widgets must be in a Window template for native dragging/resizing.
    if self.pickup then bar.content[#bar.content+1]=self:button('Pick up',842,88,self.pickup) end
    if not self:isFullscreen() then title.external={action=true} else title.external={} end
    self:layout(bar)
    local slot={type=ui.TYPE.Widget,external={slot=true},props={position=v(0,chrome*s),size=v(self.actualWidth,self.actualHeight-chrome*s)}}
    local template={rect(0,0,self.actualWidth,self.actualHeight,bg),bar,slot}
    if not self:isFullscreen() then
        local grip=rect(self.actualWidth-20,self.actualHeight-20,20,20,accent)
        grip.external={action=true,move=v(0,0),resize=v(1,1)};template[#template+1]=grip
    end
    local root={type=ui.TYPE.Window,layer=windowLayer(),props={position=v(self:isFullscreen() and 0 or self.x,self:isFullscreen() and 0 or self.y),
        size=v(self.actualWidth,self.actualHeight)},template={content=ui.content(template)},content=ui.content(content),
        events={windowDrag=cb(function(e) self:drag(e) end),keyPress=self.keyPress}}
    self.changed=false;self.monitor=volume
    return ui.create(root)
end
function W:frame(owner)
    local screen=viewport()
    if screen.x~=self.screen.x or screen.y~=self.screen.y then self:bounds();self.changed=true end
    if synth.getMonitorVolume and synth.getMonitorVolume()~=self.monitor then self.changed=true end
    if not self.changed or input.isMouseButtonPressed(1) then return end
    -- Responsive pages must rebuild their logical geometry, not merely scale
    -- widgets from the old fixed-size canvas. Wait for mouse release above.
    if owner.resize and (math.abs(owner.width-self.canvasWidth)>0.1 or math.abs(owner.height-self.canvasHeight)>0.1) then
        owner:resize();return
    end
    local content=owner.root.layout.content
    owner.root:destroy();owner.root=self:root(content)
end
return W
