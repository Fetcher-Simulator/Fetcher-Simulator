-- A native Scripts settings page: no separate settings command/window required.
local ui=require('openmw.ui')
local input=require('openmw.input')
local util=require('openmw.util')
local async=require('openmw.async')
local storage=require('openmw.storage')
local S=require('scripts.multiplayer.fetcher.hotkeys.client.style')
local M={};M.__index=M
local v=util.vector2
local function cb(fn) return async:callback(function(...) fn(...);return false end) end
function M.new(changed,pages,setPages)
    local section=storage.playerSection('FetcherHotkeysSettings')
    local config=section:getCopy('config') or {}
    local defaults={enabled=true,up=input.KEY.PageUp,down=input.KEY.PageDown,
        oneA=input.KEY.LeftCtrl,oneB=input.KEY.LeftShift,twoA=input.KEY.LeftCtrl,twoB=input.KEY.LeftAlt,scale=1,offset=18}
    for key,value in pairs(defaults) do if type(config[key])~=type(value) then config[key]=value end end
    for i=1,10 do local key='slot'..i;if type(config[key])~='number' then config[key]=input.KEY['_'..(i%10)] end end
    local self=setmetatable({section=section,config=config,changed=changed,pages=pages,setPages=setPages},M)
    self.element=ui.create{type=ui.TYPE.Widget,props={size=v(690,620)},content=ui.content{}}
    self.page={name='Fetcher Hotkeys',searchHints='hotbar voice lines pages keybinds shortcuts',element=self.element}
    ui.registerSettingsPage(self.page);self:draw();return self
end
function M:save()
    self.section:set('config',self.config);self:draw();self.changed()
end
function M:draw()
    local c=self.config
    local content={S.title('Fetcher Hotkeys',10,6,660,25,20),
        S.label('Assignments are saved per character on this server.',10,37,660),
        S.label('Hold a two-key combination to show base page +1 or +2.',10,64,660),
        S.label('Release it to return. Pages wrap; +2 wins if both are held.',10,89,660)}
    local function button(text,x,y,w,fn)
        local b=S.button(text,x,y,w,30);b.events={mouseClick=cb(fn)};content[#content+1]=b
    end
    button(c.enabled and 'Hotbar: On' or 'Hotbar: Off',10,126,175,function() c.enabled=not c.enabled;self:save() end)
    button('Pages -',200,126,95,function() self.setPages(math.max(1,self.pages()-1)) end)
    content[#content+1]=S.label(tostring(self.pages())..' / 16',310,131,85)
    button('Pages +',404,126,95,function() self.setPages(math.min(16,self.pages()+1)) end)
    local labels={up='Page forward',down='Page backward',oneA='Hold +1: first key',oneB='Hold +1: second key',
        twoA='Hold +2: first key',twoB='Hold +2: second key'}
    for i,key in ipairs{'up','down','oneA','oneB','twoA','twoB'} do
        local x=(i%2==1) and 10 or 350;local y=176+math.floor((i-1)/2)*66
        content[#content+1]=S.label(labels[key],x,y,310)
        button(self.recording==key and 'Press a key...' or input.getKeyName(c[key]),x,y+25,305,function() self.recording=key;self:draw() end)
    end
    content[#content+1]=S.label('Slot keys (click to rebind; Escape cancels)',10,380,660)
    for i=1,10 do local key='slot'..i
        local x=10+(i-1)%5*134;local y=411+math.floor((i-1)/5)*39
        button(tostring(i%10)..': '..(self.recording==key and '...' or input.getKeyName(c[key])),x,y,126,function() self.recording=key;self:draw() end)
    end
    button('Size -',10,500,110,function() c.scale=math.max(.75,c.scale-.1);self:save() end)
    content[#content+1]=S.label(string.format('%.0f%%',c.scale*100),133,505,72)
    button('Size +',211,500,110,function() c.scale=math.min(2,c.scale+.1);self:save() end)
    button('Raise',350,500,110,function() c.offset=math.min(300,c.offset+12);self:save() end)
    button('Lower',472,500,110,function() c.offset=math.max(0,c.offset-12);self:save() end)
    content[#content+1]=S.label('Reducing page count keeps assignments on hidden pages.',10,547,660)
    content[#content+1]=S.label(self.notice or 'Use /hotbar for items; /voicelines for voice previews and assignment.',10,576,670,14)
    self.element.layout.content=ui.content(content)
    self.element.layout.events={keyPress=cb(function(e) self:key(e.code) end)}
    self.element:update()
end
function M:key(code)
    if not self.recording then return false end
    local key=self.recording;self.recording=nil
    if code~=input.KEY.Escape and type(code)=='number' then
        local other=({oneA='oneB',oneB='oneA',twoA='twoB',twoB='twoA'})[key]
        if other and self.config[other]==code then self.notice='Choose two different keys for a hold combination.'
        else self.config[key]=code;self.notice=nil end
    end
    self:save();return true
end
function M:destroy() ui.removeSettingsPage(self.page);self.element:destroy() end
return M
