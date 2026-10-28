local ui=require('openmw.ui');local util=require('openmw.util');local async=require('openmw.async')
local input=require('openmw.input');local camera=require('openmw.camera');local I=require('openmw.interfaces')
local Window=require('scripts.multiplayer.fetcher.procedural-synth.client.window')
local Controls=require('scripts.multiplayer.fetcher.procedural-synth.client.controls')
local Dropdown=require('scripts.multiplayer.fetcher.procedural-synth.client.dropdown')
local P={};P.__index=P;local v,V=util.vector2,util.vector3
local white=ui.texture{path='white'}
local colors={bg=util.color.rgb(.035,.045,.063),track=util.color.rgb(.14,.17,.21),text=util.color.rgb(.85,.89,.94),accent=util.color.rgb(.2,.85,.72)}
local function cb(fn) return async:callback(function(...) fn(...);return false end) end
local function rect(x,y,w,h,color) return {type=ui.TYPE.Image,props={position=v(x,y),size=v(w,h),resource=white,color=color}} end
local function label(text,x,y,w,size) return {type=ui.TYPE.Text,props={position=v(x,y),size=v(w,24),text=text,textSize=size or 13,textColor=colors.text}} end
local function viewport() local n=ui.layers and ui.layers.indexOf('Windows');return n and ui.layers[n].size or ui.screenSize() end
function P.new(model,actors,close,back,instrument)
    local screen=viewport();local s=math.min(1.2,screen.y/790,screen.x/560)
    local p=setmetatable({kind='pose',model=model,actors=actors,close=close,back=back,instrument=instrument or 'lute',page='Prop',bone='Bip01 L Hand',extra=1,
        scale=s,x=math.max(0,screen.x-550*s),y=10,children={},controlEpoch=0,liveNumeric=true,focus=true,orbit=0,elevation=10,distance=145,height=100,framing=.35,lastStatus=-10},P)
    p.window=setmetatable({scale=s,canvasWidth=540,canvasHeight=732,originals=setmetatable({},{__mode='k'})},Window)
    I.UI.addMode('Interface',{windows={}})
    if I.Controls and I.Controls.overrideUiControls then I.Controls.overrideUiControls(true) end
    if ui.setFocus then ui.setFocus(nil) end
    p:render();return p
end
function P:element(layout) local e=self.window:create(layout);self.children[#self.children+1]=e;return e end
function P:button(text,x,y,w,fn,on)
    local r=rect(x,y,w,27,on and colors.accent or colors.track);r.content=ui.content{label(text,5,4,w-8,12)};r.events={mouseClick=cb(fn)};return r
end
function P:choose(entries,selected,fn,y)
    Dropdown.open(self,entries,selected,fn,12,y or 86,510)
end
function P:cycle(delta)
    local index=1;for n,name in ipairs(self.model.names) do if name==self.instrument then index=n end end
    self.instrument=self.model.names[(index-1+delta)%#self.model.names+1];self.target=nil;self.extra=1;self:render()
end
function P:slider(content,title,path,y)
    local model=self.model;local name=self.instrument
    content[#content+1]=Controls.slider(self,title,title,model.spec(path),function() return model:get(name,path) end,function(value) model:set(name,path,value) end,14,y,510)
end
function P:vector(content,title,path,y)
    for n,axis in ipairs{'X','Y','Z'} do local p={};for _,key in ipairs(path) do p[#p+1]=key end;p[#p+1]=n;self:slider(content,title..' '..axis,p,y);y=y+44 end
    return y
end
function P:pair(content,left,leftPath,right,rightPath,y)
    for n,axis in ipairs{'X','Y','Z'} do
        for column,item in ipairs{{left,leftPath},{right,rightPath}} do
            local path={};for _,key in ipairs(item[2]) do path[#path+1]=key end;path[#path+1]=n
            local selected=path;local name=self.instrument
            content[#content+1]=Controls.slider(self,item[1]..axis,item[1]..' '..axis,self.model.spec(selected),function() return self.model:get(name,selected) end,function(value) self.model:set(name,selected,value) end,14+(column-1)*260,y,250)
        end;y=y+48
    end;return y
end
function P:cameraSlider(content,title,key,spec,y)
    content[#content+1]=Controls.slider(self,key,title,spec,function() return self[key] end,function(value) self[key]=value end,14,y,510)
end
function P:releaseAll() end -- Dropdown contract; this window never owns notes.
function P:render()
    self.controlEpoch=self.controlEpoch+1;self.typing=false;Dropdown.close(self)
    if self.root then self.root:destroy() end;for _,child in ipairs(self.children) do child:destroy() end;self.children={}
    self.window.originals=setmetatable({},{__mode='k'})
    local content={label('LOCAL LIVE POSE TUNER',14,8,350,18),self:button('Studio',350,5,80,self.back),self:button('Close',438,5,86,self.close),
        self:button('<',14,42,30,function() self:cycle(-1) end),self:button(self.instrument..' v',50,42,430,function()
            local entries={};for _,name in ipairs(self.model.names) do entries[#entries+1]={id=name,label=name} end
            self:choose(entries,self.instrument,function(name) self.instrument=name;self.extra=1;self.target=nil;self:render() end)
        end),self:button('>',486,42,38,function() self:cycle(1) end)}
    for n,page in ipairs{'Prop','Left','Right','Bones','Camera','Export'} do local selected=page
        content[#content+1]=self:button(page,14+(n-1)*86,79,80,function() self.page=selected;self:render() end,self.page==page)
    end
    local y=119;local page=self.page
    if page=='Prop' then
        y=self:pair(content,'Position',{'propPosition'},'Rotation deg',{'propRotation'},y)
        local profile=self.model:profile(self.instrument)
        if #(profile.extras or {})>0 then
            content[#content+1]=self:button('Attachment '..self.extra..' v',14,y,510,function()
                local entries={};for n,e in ipairs(profile.extras) do entries[#entries+1]={id=n,label=e.key} end
                self:choose(entries,self.extra,function(n) self.extra=n;self:render() end,140)
            end);y=y+34
            y=self:pair(content,'Extra position',{'extras',self.extra,'position'},'Extra rot deg',{'extras',self.extra,'rotation'},y)
        else content[#content+1]=label('Positions are in actor-facing anchor units; rotations add to the default.',14,y+12,510,11) end
        content[#content+1]=label('Prop moves independently of the hands. Placed keyboard stays fixed.',14,615,510,11)
        content[#content+1]=label('X/Y/Z rotation offsets apply to the existing orientation (degrees).',14,639,510,11)
    elseif page=='Left' or page=='Right' then
        local side=page=='Left' and 'L' or 'R'
        y=self:vector(content,'Wrist target',{side..'Target'},y)
        y=self:vector(content,'Palm rotation offset (deg)',{side..'Rotation'},y)
        y=self:vector(content,'Elbow direction',{side..'Pole'},y)
        self:slider(content,'Finger curl multiplier',{side..'CurlScale'},y);y=y+44
        self:slider(content,'Finger curl offset (radians)',{side..'CurlBias'},y);y=y+44
        self:slider(content,'Note motion multiplier',{side..'MotionScale'},y)
    elseif page=='Bones' then
        content[#content+1]=self:button(self.bone..' v',14,y,510,function()
            local entries={};for _,name in ipairs(self.model.boneNames) do entries[#entries+1]={id=name,label=name} end
            self:choose(entries,self.bone,function(name) self.bone=name;self:render() end,120)
        end);y=y+38
        y=self:vector(content,'Bone local position offset',{'bones',self.bone,'position'},y)
        y=self:vector(content,'Bone local rotation offset (deg)',{'bones',self.bone,'rotation'},y)
        content[#content+1]=label('Advanced offsets are added after IK. Start with the Left/Right pages.',14,y+8,510,11)
        content[#content+1]=label('Translations can disconnect joints; Reset instrument restores all bones.',14,y+34,510,11)
    elseif page=='Camera' then
        content[#content+1]=self:button('Performer: '..(self.target or 'choose / refresh')..' v',14,y,510,function()
            local entries={};for _,item in ipairs(self.actors:targets(self.instrument)) do entries[#entries+1]={id=item.key,label=self.instrument..' / '..item.key} end
            self:choose(entries,self.target,function(key) self.target=key;self.focus=true;self:render() end,120)
        end);y=y+36
        content[#content+1]=self:button(self.focus and 'Camera: focused' or 'Focus first performer',14,y,250,function() self.focus=not self.focus;if not self.focus then self:restoreCamera() end;self:render() end)
        content[#content+1]=self:button('Release camera',274,y,250,function() self.focus=false;self:restoreCamera();self:render() end);y=y+38
        for n,item in ipairs{{'Front',0,10},{'Left',-90,10},{'Right',90,10},{'Rear',180,10},{'Above',0,70}} do local view=item
            content[#content+1]=self:button(view[1],14+(n-1)*103,y,98,function() self.orbit=view[2];self.elevation=view[3];self.focus=true;self:render() end)
        end;y=y+44
        self:cameraSlider(content,'Orbit (deg)','orbit',{min=-180,max=180,step=1},y);y=y+48
        self:cameraSlider(content,'Elevation (deg)','elevation',{min=-75,max=75,step=1},y);y=y+48
        self:cameraSlider(content,'Distance','distance',{min=40,max=400,step=2},y);y=y+48
        self:cameraSlider(content,'Look-at height','height',{min=20,max=170,step=1},y)
        y=y+48;self:cameraSlider(content,'Room for panel','framing',{min=-.7,max=.7,step=.05},y)
        content[#content+1]=label('View follows the chosen performer; closing restores your camera.',14,y+60,510,11)
    elseif page=='Export' then
        content[#content+1]=self:button('Export '..self.instrument,14,y,250,function() self:export(false) end)
        content[#content+1]=self:button('Export all instruments',274,y,250,function() self:export(true) end)
        content[#content+1]=label('Click the text, Ctrl+A then Ctrl+C. Paste it for Codex to apply.',14,y+36,510,12)
        self.exportField=self:element{type=ui.TYPE.TextEdit,props={position=v(14,y+68),size=v(510,412),text=self.exportText or '',textSize=12,multiline=true,wordWrap=false,textColor=colors.text},
            template={content=ui.content{rect(0,0,510,412,colors.track)}},events={focusGain=cb(function() self.typing=true end),focusLoss=cb(function() self.typing=false end)}}
        content[#content+1]=self.exportField
        content[#content+1]=label('Export also goes to the client log and local pose storage. No code is loaded.',14,y+492,510,11)
    end
    self.status=self:element(label('',14,663,510,11));content[#content+1]=self.status
    content[#content+1]=self:button('Save locally',14,691,115,function() self.model:save();ui.showMessage('Local pose tuning saved. Export it to share the coordinates.') end)
    content[#content+1]=self:button('Reload saved',135,691,115,function() self.model:reload();self:render() end)
    content[#content+1]=self:button('Reset '..self.instrument,256,691,142,function() self.model:reset(self.instrument);self:render() end)
    content[#content+1]=self:button(self.model.enabled and 'Preview: On' or 'Preview: Off',404,691,120,function() self.model:toggle();self:render() end)
    local scale=self.scale;local screen=viewport();self.x=math.max(0,math.min(self.x,screen.x-540*scale));self.y=math.max(0,math.min(self.y,screen.y-770*scale))
    local title=rect(0,0,540,38,colors.track);title.external={action=true};title.content=ui.content{label('Drag pose window',14,9,250,13)}
    self.window:layout(title)
    for _,item in ipairs(content) do if item.layout then self.window:update(item) else self.window:layout(item) end end
    self.root=ui.create{type=ui.TYPE.Window,layer=Window.layer(),props={position=v(self.x,self.y),size=v(540*scale,770*scale)},
        template={content=ui.content{rect(0,0,540*scale,770*scale,colors.bg),title,{type=ui.TYPE.Widget,external={slot=true},props={position=v(0,38*scale),size=v(540*scale,732*scale)}}}},content=ui.content(content),
        events={windowDrag=cb(function(e) self.x=e.position.x;self.y=e.position.y;self.root.layout.props.position=e.position end)}}
end
function P:export(all)
    self.exportText=self.model:export(not all and self.instrument or nil)
    print('[StudioPoseExport BEGIN]\n'..self.exportText..'\n[StudioPoseExport END]');self:render()
end
function P:restoreCamera()
    local s=self.cameraSaved;if not s then return end;self.cameraSaved=nil
    camera.setMode(s.mode);if s.mode==camera.MODE.Static then camera.setStaticPosition(s.position) end
    camera.setYaw(s.yaw);camera.setPitch(s.pitch);camera.setExtraYaw(s.extraYaw);camera.setExtraPitch(s.extraPitch)
end
function P:frame(now)
    if self.uiClosed then return end
    Dropdown.frame(self)
    if I.UI.getMode()~='Interface' then self.close();return end
    if self.focus then
        local targets=self.actors:targets(self.instrument);local actor
        for _,item in ipairs(targets) do if item.key==self.target then actor=item.actor end end
        if not actor and targets[1] then self.target=targets[1].key;actor=targets[1].actor end
        if actor then
            if not self.cameraSaved then self.cameraSaved={mode=camera.getMode(),position=camera.getPosition(),yaw=camera.getYaw(),pitch=camera.getPitch(),extraYaw=camera.getExtraYaw(),extraPitch=camera.getExtraPitch()} end
            local a,b=math.rad(self.orbit),math.rad(self.elevation)
            local target=actor.position+V(0,0,self.height)
            local position=target+actor.rotation*V(math.sin(a)*math.cos(b)*self.distance,math.cos(a)*math.cos(b)*self.distance,math.sin(b)*self.distance)
            local d=target-position;local right=V(d.y,-d.x,0);if right:length()>.001 then d=d+right/right:length()*(self.distance*self.framing) end
            camera.setMode(camera.MODE.Static);camera.setStaticPosition(position)
            camera.setExtraYaw(0);camera.setExtraPitch(0);camera.setYaw(math.atan2(d.x,d.y));camera.setPitch(-math.atan2(d.z,math.sqrt(d.x*d.x+d.y*d.y)))
        else self:restoreCamera() end
    end
    if now-self.lastStatus>.25 then
        self.lastStatus=now;local targets=self.actors:targets(self.instrument);local reach=0
        for _,item in ipairs(targets) do reach=math.max(reach,item.pose and item.pose.reachError or 0) end
        self.status.layout.props.text=string.format('Local preview | %d %s performers | Unreachable target: %.2f units',#targets,self.instrument,reach);self.window:update(self.status)
    end
end
function P:destroy()
    self.uiClosed=true;self.controlEpoch=self.controlEpoch+1;Dropdown.close(self);self:restoreCamera()
    if ui.setFocus then ui.setFocus(nil) end
    self.root:destroy();for _,child in ipairs(self.children) do child:destroy() end
    if I.Controls and I.Controls.overrideUiControls then I.Controls.overrideUiControls(false) end
    if I.UI.getMode()=='Interface' then I.UI.removeMode('Interface') end
end
return P
