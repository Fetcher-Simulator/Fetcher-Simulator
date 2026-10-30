local ui=require('openmw.ui');local util=require('openmw.util');local async=require('openmw.async')
local I=require('openmw.interfaces');local Window=require('scripts.multiplayer.fetcher.procedural-synth.client.window')
local P={};P.__index=P;local v=util.vector2
local white=ui.texture{path='white'}
local function label(text,x,y,width,size)
    return {type=ui.TYPE.Text,props={text=text,position=v(x,y),size=v(width,32),textSize=size or 16,textColor=util.color.rgb(0.85,0.89,0.94)}}
end
function P.new(state,command,close,tune,listen)
    local p=setmetatable({kind='performer',state=state,command=command,close=close,tune=tune,listen=listen,window=Window.new()},P)
    I.UI.addMode('Interface',{windows={}});p:render();return p
end
function P:button(text,x,y,width,fn)
    return {type=ui.TYPE.Image,props={position=v(x,y),size=v(width,34),resource=white,color=util.color.rgb(0.14,0.17,0.21)},content=ui.content{label(text,6,4,width-12,16)},
        events={mouseClick=async:callback(function() fn();return false end)}}
end
function P:render()
    if self.root then self.root:destroy() end
    local s=self.state;local content={label('BAND / PERFORMER',24,20,650,23),
        self:button('Close [F9]',754,16,160,self.close),
        label('Ready with: '..(s.held~='' and s.held or 'No instrument'),24,88,850,19),
        label('Carry the instrument in your inventory. The band holds and plays it while your part runs.',24,126,880,14)}
    local n=0
    for _,instrument in ipairs{'keyboard','lute','drum','violin','cello','trumpet','flute','clarinet','timpani','guitar','bass','drumkit'} do
        local selected=instrument
        content[#content+1]=self:button((s.available and s.available[selected] and 'Ready: ' or 'Need item: ')..selected,24+(n%4)*224,185+math.floor(n/4)*43,210,function() self.command('hold',{instrument=selected}) end);n=n+1
    end
    content[#content+1]=self:button('Not ready / holster',24,325,270,function() self.command('hold',{instrument='none'}) end)
    content[#content+1]=self:button('Leave band',325,325,230,function() self.command('leaveBand') end)
    local invitation=s.invitation
    if invitation then
        content[#content+1]=label(invitation.leader..' invites you to play '..invitation.instrument..' in slot '..invitation.slot..'.',24,379,870,18)
        content[#content+1]=self:button('Accept',24,432,210,function() self.command('respond',{token=invitation.token,accept=true}) end)
        content[#content+1]=self:button('Decline',260,432,210,function() self.command('respond',{token=invitation.token,accept=false}) end)
    elseif s.membership then content[#content+1]=label('Band leader: '..s.membership.leader..'  /  Member slot '..s.membership.slot,24,379,870,18)
    else content[#content+1]=label('No invitation or active band membership.',24,379,870,18) end
    content[#content+1]=label('Invitations expire after 30 seconds. Saved songs never save your consent.',24,540,880,14)
    content[#content+1]=label('You can leave or stop being ready at any time. F8 returns to your own Studio.',24,588,880,14)
    if self.tune then content[#content+1]=self:button('Local pose tuner',24,630,270,function() self.tune(s.held~='' and s.held or 'lute') end) end
    if self.listen then content[#content+1]=self:button('Listening',325,630,230,self.listen) end
    self.root=self.window:root(content)
end
function P:update(state) self.state=state;self:render() end
function P:frame() self.window:frame(self);if I.UI.getMode()~='Interface' then self.close() end end
function P:destroy() self.root:destroy();if I.UI.getMode()=='Interface' then I.UI.removeMode('Interface') end end
return P
