local ui=require('openmw.ui');local util=require('openmw.util');local async=require('openmw.async');local I=require('openmw.interfaces')
local Window=require('scripts.multiplayer.fetcher.procedural-synth.client.window');local Controls=require('scripts.multiplayer.fetcher.procedural-synth.client.controls')
local Dropdown=require('scripts.multiplayer.fetcher.procedural-synth.client.dropdown')
local P={};P.__index=P;local v=util.vector2;local white=ui.texture{path='white'}
local text=util.color.rgb(.85,.89,.94);local accent=util.color.rgb(.2,.85,.72);local track=util.color.rgb(.14,.17,.21)
local function label(s,x,y,w,size) return {type=ui.TYPE.Text,props={position=v(x,y),size=v(w,45),text=s,textSize=size or 14,textColor=text}} end
function P:element(layout) local e=self.window:create(layout);self.children[#self.children+1]=e;return e end
function P:button(s,x,y,w,fn,on)
    local caption=label(s,6,5,w-12,14);if on then caption.props.textColor=util.color.rgb(.035,.045,.063) end
    return {type=ui.TYPE.Image,props={position=v(x,y),size=v(w,30),resource=white,color=on and accent or track},content=ui.content{caption},events={mouseClick=async:callback(function() fn();return false end)}}
end
function P.new(service,close,back)
    local p=setmetatable({kind='listening',service=service,close=close,back=back,window=Window.new(),children={},controlEpoch=0,last=-1},P)
    I.UI.addMode('Interface',{windows={}});if ui.setFocus then ui.setFocus(nil) end;p:render();return p
end
function P:render()
    self.controlEpoch=self.controlEpoch+1;self.typing=false
    Dropdown.close(self)
    if self.root then self.root:destroy() end;for _,e in ipairs(self.children) do e:destroy() end;self.children={}
    local o=self.service.options;local content={label('LISTENING / LOCAL',24,20,640,22),self:button('Studio',650,16,125,self.back),self:button('Close',790,16,125,self.close),
        label('Change your listening perspective without restarting the song or changing its mix.',24,65,880),
        self:button('World: gentler distance falloff',24,112,420,function() self.service:set('balanced',false);self:render() end,not o.balanced),
        self:button('Balanced: selected band',460,112,455,function() self.service:set('balanced',true);self:render() end,o.balanced),
        label('Balanced centers your open Studio / accepted band. Other bands keep their world positions.',24,156,890,13),
        self:button(o.room and 'Room acoustics: Auto' or 'Room acoustics: Dry',24,202,420,function() self.service:set('room',not o.room);self:render() end,o.room),
        self:button('Monitor band: '..(self.service.selected or 'Auto')..' v',460,202,455,function()
            local entries={{id=0,label='Auto: open Studio / accepted band'}}
            for _,item in ipairs(self.service.bands) do entries[#entries+1]=item end
            Dropdown.open(self,entries,self.service.selected or 0,function(id) self.service.selected=id~=0 and id or nil;self:render() end,460,240,440)
        end)}
    for n,item in ipairs{{'reference','Full-volume radius (game units)'},{'rolloff','Distance falloff (lower = gentler)'},{'wet','Room reflection level'},{'absorption','Surface absorption (low = reflective stone)'}} do
        local key,title=item[1],item[2]
        content[#content+1]=Controls.slider(self,key,title,self.service.specs[key],function() return o[key] end,function(value) self.service:set(key,value) end,24,260+(n-1)*55,870)
    end
    local status=label(self.service:status(),24,500,880,14);status.props.multiline=true
    self.status=self:element(status);content[#content+1]=self.status
    content[#content+1]=label('Room size/enclosure come from bounded wall/ceiling probes; shape and materials are approximations.',24,566,880,12)
    content[#content+1]=label('World mode tests wall obstruction. Balanced bypasses direct obstruction; room reflections are optional.',24,596,880,12)
    content[#content+1]=label('Preferences save locally. Instrument effects, project faders, other players and game sound effects are unchanged.',24,626,880,12)
    self.root=self.window:root(content)
end
function P:frame(now)
    self.window:frame(self);Dropdown.frame(self);if I.UI.getMode()~='Interface' then self.close();return end
    if now-self.last>.5 then self.last=now;self.status.layout.props.text=self.service:status();self.window:update(self.status) end
end
function P:destroy()
    self.uiClosed=true;self.controlEpoch=self.controlEpoch+1;Dropdown.close(self);self.root:destroy();self.window:clearChrome();for _,e in ipairs(self.children) do e:destroy() end
    if ui.setFocus then ui.setFocus(nil) end;if I.UI.getMode()=='Interface' then I.UI.removeMode('Interface') end
end
return P
