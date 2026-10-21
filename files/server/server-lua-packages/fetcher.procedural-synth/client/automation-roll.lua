-- Bounded automation graph; drag previews commit once on release.
local ui=require('openmw.ui');local util=require('openmw.util');local async=require('openmw.async');local input=require('openmw.input')
local Project=require('scripts.multiplayer.fetcher.procedural-synth.shared.project')(require('scripts.multiplayer.fetcher.procedural-synth.shared.schema'))
local R={};R.__index=R;local v=util.vector2;local white=ui.texture{path='white'}
local bg=util.color.rgb(.035,.045,.063);local line=util.color.rgb(.14,.17,.21);local accent=util.color.rgb(.2,.85,.72);local selected=util.color.rgb(.8,.9,1)
local function rect(x,y,w,h,color) return {type=ui.TYPE.Image,props={position=v(x,y),size=v(w,h),resource=white,color=color}} end
local function cb(fn) return async:callback(function(e) fn(e);return false end) end
local function clamp(n,a,b) return math.max(a,math.min(b,n)) end
function R.new(owner,content)
    local w=owner.w;local name=w.automationName or 'cutoff';local spec=Project.automationSpecs[name]
    local self=setmetatable({owner=owner,w=w,index=w.selected,name=name,spec=spec,x=76,y=304,width=owner.width-106,
        height=math.max(180,owner.height-478),start=w.automationStart or 0,steps=w.automationSteps or 64},R)
    self.element=owner:element(rect(self.x,self.y,self.width,self.height,bg));content[#content+1]=self.element
    self.element.layout.events={mousePress=cb(function(e)
        if e.button~=1 or w.automationWriting then return end
        local p=self:point(e);local step,value=self:values(p.x,p.y)
        if w:setAutomation(self.index,name,step,value,w.automationLinear~=false) then w.automationPoint=step;w.automationStep=step;w.automationDraft=value;w.revision=w.revision+1;self:draw() end
    end)}
    self:draw();return self
end
function R:point(e,x,y)
    local pos=self.owner.root and self.owner.root.layout.props.position or v(0,0);local scale=self.owner.window.scale
    if e.position then return v((e.position.x-pos.x)/scale-self.x,(e.position.y-pos.y)/scale-38-self.y) end
    return v((x or 0)+e.offset.x/scale,(y or 0)+e.offset.y/scale)
end
function R:values(x,y)
    local step=math.max(0,math.floor((self.start+clamp(x,0,self.width)/self.width*self.steps)*4+0.5)/4)
    local value=self.spec.min+(self.spec.max-self.spec.min)*(1-clamp(y,0,self.height)/self.height)
    if self.spec.discrete then value=math.floor(value+0.5) end
    return step,value
end
function R:draw()
    local cells={};local w=self.w;local points=w.project.tracks[self.index].automation or {};points=points[self.name] or {}
    local function y(value) return (1-(value-self.spec.min)/(self.spec.max-self.spec.min))*self.height end
    for i=0,8 do cells[#cells+1]=rect(0,i*self.height/8,self.width,1,line) end
    for i=0,16 do cells[#cells+1]=rect(i*self.width/16,0,1,self.height,line) end
    local previous
    for column=0,255 do
        local value=w:automationValue(self.index,self.name,self.start+self.steps*column/255);local py=y(value)
        if previous then cells[#cells+1]=rect((column-1)*self.width/255,math.min(py,previous),math.max(2,self.width/255),math.max(2,math.abs(py-previous)),accent) end
        previous=py
    end
    for _,point in ipairs(points) do if point.s>=self.start and point.s<=self.start+self.steps then
        local x=(point.s-self.start)/self.steps*self.width;local py=y(point.v)
        local handle=rect(clamp(x-5,0,self.width-10),clamp(py-5,0,self.height-10),10,10,w.automationPoint==point.s and selected or accent)
        local function press(e)
            if w.automationWriting then return end
            if e.button==3 then w:setAutomation(self.index,self.name,point.s,nil,false,true);w.automationPoint=nil;w.revision=w.revision+1;return end
            if e.button~=1 then return end
            w.automationPoint=point.s;w.automationStep=point.s;w.automationDraft=point.v;w.automationLinear=point.linear
            self.drag={from=point.s,s=point.s,v=point.v,linear=point.linear,handle=handle};self:move(e,x-5,py-5)
        end
        handle.events={mousePress=cb(press),mouseMove=cb(function(e) if self.drag then self:move(e,x-5,py-5) end end)}
        cells[#cells+1]=handle
    end end
    self.element.layout.content=ui.content(cells);self.owner.window:update(self.element);self.generation=w.generation
end
function R:move(e,x,y)
    if not self.drag then return end
    local p=self:point(e,x,y);local d=self.drag;d.s,d.v=self:values(p.x,p.y)
    d.handle.props.position=v(clamp((d.s-self.start)/self.steps*self.width-5,0,self.width-10),clamp((1-(d.v-self.spec.min)/(self.spec.max-self.spec.min))*self.height-5,0,self.height-10))
    -- Refresh the graph's existing children without replacing mouse capture.
    self.owner.window:update(self.element)
end
function R:finish(commit)
    local d=self.drag;if not d then return end;self.drag=nil
    if commit then
        if self.w:setAutomation(self.index,self.name,d.s,d.v,d.linear,false,d.from) then self.w.automationPoint=d.s;self.w.automationStep=d.s;self.w.automationDraft=d.v end
        self.w.revision=self.w.revision+1
    end
end
function R:frame()
    if self.drag and not input.isMouseButtonPressed(1) then self:finish(true)
    elseif not self.drag and self.generation~=self.w.generation then self:draw() end
end
return R
