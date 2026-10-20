local ui=require('openmw.ui');local util=require('openmw.util');local async=require('openmw.async');local input=require('openmw.input')
local Format=require('scripts.multiplayer.fetcher.procedural-synth.shared.project')(require('scripts.multiplayer.fetcher.procedural-synth.shared.schema'))
local R={};R.__index=R;local v=util.vector2;local white=ui.texture{path='white'}
local c={bg=util.color.rgb(0.075,0.09,0.12),line=util.color.rgb(0.14,0.17,0.21),clip=util.color.rgb(0.35,0.65,0.95),selected=util.color.rgb(0.2,0.85,0.72),text=util.color.rgb(0.035,0.045,0.063)}
local function rect(x,y,w,h,color) return {type=ui.TYPE.Image,props={position=v(x,y),size=v(w,h),resource=white,color=color}} end
local function cb(fn) return async:callback(function(e) fn(e);return false end) end
local function clamp(n,a,b) return math.max(a,math.min(b,n)) end
function R.new(owner,content)
    local w=owner.w;local bar=Format.barBeats(w.project)*4
    local self=setmetatable({owner=owner,w=w,x=190,y=292,width=700,height=264,bar=bar,pool={},labels={},page=w.arrangePage or 0},R)
    self.bars=w.arrangeBars or 8;self.steps=self.bars*bar;self.cw=self.width/self.steps
    local finish=w.project.timeline and w.project.timeline.beats*4 or self.steps
    for _,t in ipairs(w.project.tracks) do for _,r in ipairs(t.arrangement and t.arrangement.regions or {}) do finish=math.max(finish,r.s+r.l) end end
    self.maximum=math.max(0,math.ceil(finish/bar)-self.bars);w.arrangeBar=clamp(w.arrangeBar or 0,0,self.maximum);self.start=w.arrangeBar*bar
    for row=1,8 do
        local index=self.page*8+row;local t=w.project.tracks[index]
        if t then
            content[#content+1]=owner:button(index..' '..t.name:sub(1,17),20,self.y+(row-1)*33,160,function()
                if not w:leaveClip() then return end;w.selected=index;w.arrangeClip=1;w.regionIndex=nil;w.revision=w.revision+1
            end,w.selected==index)
            local grid=rect(self.x,self.y+(row-1)*33,self.width,30,c.bg);local cells={}
            for b=0,self.bars-1 do
                cells[#cells+1]=rect(b*self.width/self.bars,0,1,30,c.line)
                if row==1 then local label=owner:element{type=ui.TYPE.Text,props={position=v(self.x+b*self.width/self.bars,269),size=v(50,20),text=tostring(w.arrangeBar+b+1),textSize=11,textColor=c.selected}};self.labels[#self.labels+1]=label;content[#content+1]=label end
            end
            grid.content=ui.content(cells);grid.events={mousePress=cb(function(e)
                if e.button~=1 then return end
                local point=self:point(e);local step=math.floor((self.start+point.x/self.cw)/self.bar)*self.bar
                w:placeClip(index,w.selected==index and (w.arrangeClip or 1) or 1,math.max(0,step))
                w.selected=index
            end)};content[#content+1]=grid
            for id,r in ipairs(t.arrangement and t.arrangement.regions or {}) do
                local slot=#self.pool+1;local layout=rect(0,0,1,1,c.clip)
                local text=owner:element{type=ui.TYPE.Text,props={position=v(5,5),size=v(1,22),text='',textSize=10,textColor=c.text}}
                layout.content=ui.content{text};layout.events={mousePress=cb(function(e) self:press(slot,e) end),mouseMove=cb(function(e) self:move(e) end)}
                local element=owner:element(layout);content[#content+1]=element
                self.pool[slot]={element=element,text=text,index=index,id=id,track=t,row=row}
            end
        end
    end
    self.thumb=owner:element(rect(0,0,20,12,c.selected))
    local function scroll(e)
        local point=self:point(e);local width=self.width-self.thumbWidth
        local start=width>0 and clamp((point.x-self.thumbWidth/2)/width,0,1)*self.maximum or 0
        w.arrangeBar=math.floor(start+0.5);self.start=w.arrangeBar*self.bar;self:draw()
    end
    local events={mousePress=cb(function(e) if e.button==1 then self.scrolling=true;scroll(e) end end),mouseMove=cb(function(e) if self.scrolling then scroll(e) end end)}
    local barLayout=rect(self.x,565,self.width,12,c.line);barLayout.content=ui.content{self.thumb};barLayout.events=events
    self.thumb.layout.events=events;content[#content+1]=barLayout
    self.playhead=owner:element(rect(self.x,self.y,2,self.height,c.selected));content[#content+1]=self.playhead
    self:draw();return self
end
function R:point(e)
    local p=self.owner.root and self.owner.root.layout.props.position or v(0,0);local scale=self.owner.window.scale
    if e.position then return v((e.position.x-p.x)/scale-self.x-(self.owner.bodyOffset or 0),(e.position.y-p.y)/scale-38-self.y) end
    return v(e.offset.x/scale,e.offset.y/scale)
end
function R:press(slot,e)
    if e.button~=1 then return end
    local entry=self.pool[slot];local r=entry.track.arrangement.regions[entry.id]
    local w=self.w;w.regionTrack=entry.index;w.regionIndex=entry.id;w.selected=entry.index;w.arrangeClip=r.clip
    local width=entry.element.layout.props.size.x
    self.drag={entry=entry,x=self:point(e).x,source=r,s=r.s,l=r.l,resize=e.offset and e.offset.x>=width-math.min(width*0.3,8*self.owner.window.scale) and r.s+r.l<=self.start+self.steps}
    self:draw()
end
function R:move(e)
    local d=self.drag;if not d then return end
    local delta=math.floor((self:point(e).x-d.x)/self.cw/self.bar+0.5)*self.bar
    if d.resize then d.l=math.max(self.bar,d.source.l+delta) else d.s=math.max(0,d.source.s+delta) end
    self:draw()
end
function R:finish(cancel)
    local d=self.drag;self.drag=nil
    if self.scrolling then self.scrolling=nil;if not cancel then self.w.revision=self.w.revision+1 end end
    if d and not cancel and self.w.project.tracks[d.entry.index]==d.entry.track then
        if d.s~=d.source.s or d.l~=d.source.l then self.w:changeRegion(d.entry.index,d.entry.id,{s=d.s,l=d.l})
        else self.w.revision=self.w.revision+1 end
    end
end
function R:frame()
    if not input.isMouseButtonPressed(1) then self:finish(false) end
    local step=self.w.beat*4;local visible=self.w.playing and self.w.mode=='song' and step>=self.start and step<self.start+self.steps
    self.playhead.layout.props.visible=visible
    self.playhead.layout.props.position=v(self.x+(step-self.start)*self.cw,self.y);self.owner.window:update(self.playhead)
end
function R:draw()
    for index,label in ipairs(self.labels) do label.layout.props.text=tostring(self.w.arrangeBar+index);self.owner.window:update(label) end
    for _,entry in ipairs(self.pool) do
        local r=entry.track.arrangement.regions[entry.id];local s,l=r.s,r.l
        if self.drag and self.drag.entry==entry then s,l=self.drag.s,self.drag.l end
        local left,right=math.max(s,self.start),math.min(s+l,self.start+self.steps);local props=entry.element.layout.props
        props.visible=right>left;props.position=v(self.x+(left-self.start)*self.cw,self.y+(entry.row-1)*33)
        props.size=v(math.max(3,(right-left)*self.cw-2),29)
        props.color=self.w.regionTrack==entry.index and self.w.regionIndex==entry.id and c.selected or r.mute and c.line or c.clip
        local clip=entry.track.arrangement.clips[r.clip]
        entry.text.layout.props.text=clip.name..(r.transpose~=0 and string.format(' %+d',r.transpose) or '')..(r.mute and ' [M]' or '')
        entry.text.layout.props.size=v(math.max(1,props.size.x-8),22)
        self.owner.window:update(entry.text);self.owner.window:update(entry.element)
    end
    self.thumbWidth=math.max(18,self.width*self.bars/(self.maximum+self.bars))
    self.thumb.layout.props.position=v(self.maximum>0 and (self.width-self.thumbWidth)*self.w.arrangeBar/self.maximum or 0,0)
    self.thumb.layout.props.size=v(self.thumbWidth,12);self.owner.window:update(self.thumb)
end
return R
