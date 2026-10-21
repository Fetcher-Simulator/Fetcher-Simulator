-- Persistent note widgets keep mouse capture through selection and drag previews.
-- Musical edits commit on release, producing one history/transport replacement.
local ui=require('openmw.ui');local util=require('openmw.util');local async=require('openmw.async');local input=require('openmw.input')
local Project=require('scripts.multiplayer.fetcher.procedural-synth.shared.project')(require('scripts.multiplayer.fetcher.procedural-synth.shared.schema'))
local R={};R.__index=R;local v=util.vector2;local white=ui.texture{path='white'}
local colors={bg=util.color.rgb(0.035,0.045,0.063),row=util.color.rgb(0.075,0.09,0.12),line=util.color.rgb(0.14,0.17,0.21),
    note=util.color.rgb(0.35,0.65,0.95),selected=util.color.rgb(0.2,0.85,0.72)}
local function rect(x,y,w,h,color) return {type=ui.TYPE.Image,props={position=v(x,y),size=v(w,h),resource=white,color=color}} end
local function cb(fn) return async:callback(function(e) fn(e);return false end) end
local function clamp(n,a,b) return math.max(a,math.min(b,n)) end
local function round(n,q) return math.floor(n/q+0.5)*q end
local function shift() return input.isShiftPressed and input.isShiftPressed() end
function R.new(owner,content)
    local w=owner.w;local t=w.project.tracks[w.selected]
    local self=setmetatable({owner=owner,w=w,index=w.selected,track=t,x=76,y=owner.pianoFocus and 141 or 260,width=owner.width-106,
        height=math.max(264,(owner.height or 695)-(owner.pianoFocus and 312 or 431)),pool={},visible={}},R)
    self.rows=math.min(128,math.max(12,math.floor(self.height/22)))
    self.base=math.min(w.base,128-self.rows)
    -- Keep approximately the same musical zoom as the viewport grows. The
    -- explicit zoom buttons continue to control the baseline time density.
    self.steps=math.min(256,math.max(w.rollSteps,math.floor(w.rollSteps*self.width/834/4+1e-6)*4))
    self.cw=self.width/self.steps;self.rh=self.height/self.rows
    self.start=w.rollStart or w.bar*16;local notes=w:noteData(self.index) or {}
    local finish=t.kind=='midi' and math.max(w.project.timeline and w.project.timeline.beats*4 or w.song and w.song.beats*4 or 16,16) or Project.maxSteps
    local clip=w:clipSource(self.index);if clip then finish=clip.length end
    for _,n in ipairs(notes) do finish=math.max(finish,n.s+n.l) end
    self.maximum=math.max(0,finish-self.steps);self.start=clamp(self.start,0,self.maximum)
    local grid=rect(self.x,self.y,self.width,self.height,colors.bg);local cells={}
    for row=0,self.rows-1 do
        local note=self.base+self.rows-1-row;local black=({[1]=true,[3]=true,[6]=true,[8]=true,[10]=true})[note%12]
        cells[#cells+1]=rect(0,row*self.rh,self.width,self.rh-1,black and colors.bg or colors.row)
    end
    self.songGrid=t.kind=='midi' and not clip
    self.barSteps=self.songGrid and Project.barBeats(w.project,self.start/4)*4 or 16
    self.lines={}
    for step=0,self.steps-1 do
        local line=owner:element(rect(step*self.cw,0,1,self.height,colors.line))
        self.lines[#self.lines+1]=line;cells[#cells+1]=line
    end
    grid.content=ui.content(cells)
    grid.events={mousePress=cb(function(e)
        if e.button~=1 then return end
        w:selectNote(self.index)
        if w:canEditNotes(self.index) then
            local point=self:point(e);local step=math.max(0,round(self.start+point.x/self.cw,self:quantum()))
            local pitch=clamp(self.base+self.rows-1-math.floor(point.y/self.rh),0,127)
            w:addNote(self.index,pitch,step)
        end
        self:draw()
    end),mouseMove=cb(function(e) self:move(e) end)}
    content[#content+1]=grid
    self.barGrid=owner:element{type=ui.TYPE.Widget,props={position=v(self.x,self.y),size=v(self.width,self.height)},content=ui.content{}}
    self.barGrid.layout.events=grid.events
    content[#content+1]=self.barGrid
    for slot=1,math.min(512,#notes) do
        local r=rect(0,0,1,1,colors.note);r.props.visible=false
        r.events={mousePress=cb(function(e) self:press(slot,e) end),mouseMove=cb(function(e) self:move(e) end)}
        self.pool[slot]=owner:element(r);content[#content+1]=self.pool[slot]
    end
    self.caption=owner:element{type=ui.TYPE.Text,props={position=v(76,self.y-23),size=v(self.width,20),text='',textSize=11,textColor=colors.selected}}
    content[#content+1]=self.caption
    self.thumb=owner:element(rect(0,0,20,12,colors.selected))
    local events={mousePress=cb(function(e) if e.button==1 then self.scrollDrag=true;self:scroll(e) end end),mouseMove=cb(function(e) if self.scrollDrag then self:scroll(e) end end)}
    self.thumb.layout.events=events
    local bar=rect(self.x,self.y+self.height+7,self.width,12,colors.line);bar.events=events;bar.content=ui.content{self.thumb}
    content[#content+1]=bar
    self:draw();return self
end
function R:quantum() return self.w.snap>0 and self.w.snap or (self.track.edited and 4/self.track.edited.division or 1/240) end
function R:point(e)
    local p=self.owner.root and self.owner.root.layout.props.position or v(0,0);local scale=self.owner.window.scale
    if e.position then return v((e.position.x-p.x)/scale-self.x,(e.position.y-p.y)/scale-38-self.y) end
    return v(e.offset.x/scale,e.offset.y/scale)
end
function R:press(slot,e)
    local id=self.visible[slot];if not id then return end
    local w=self.w
    if e.button==3 then
        self:finish(true)
        if w:canEditNotes(self.index) then
            w:selectNote(self.index,id);w:deleteNotes(self.index);self:draw()
        end
        return
    end
    if e.button~=1 then return end
    if shift() then w:selectNote(self.index,id,true)
    elseif w.selectionTrack~=self.index or not w.noteSelection[id] then w:selectNote(self.index,id) end
    self:draw()
    if not w:canEditNotes(self.index) or not w.noteSelection[id] then return end
    local n=w:noteData(self.index)[id];local point=self:point(e)
    local at=self.pool[slot].layout.props.size.x;local resize=e.offset and e.offset.x>=at-math.min(8*self.owner.window.scale,at*0.3) and n.s+n.l<=self.start+self.steps
    self.drag={x=point.x,y=point.y,resize=resize,notes=w:noteData(self.index),ids=w:selectedNotes(self.index),change={step=0,pitch=0,length=0}}
end
function R:move(e)
    local d=self.drag;if not d then return end
    local point=self:point(e);local delta=round((point.x-d.x)/self.cw,self:quantum())
    local pitch=math.floor((d.y-point.y)/self.rh+0.5)
    local minS,minP,maxP,minL,maxS,maxL,maxEnd=math.huge,127,0,math.huge,0,0,0
    for _,id in ipairs(d.ids) do local n=d.notes[id];minS=math.min(minS,n.s);maxS=math.max(maxS,n.s)
        minP=math.min(minP,n.n);maxP=math.max(maxP,n.n);minL=math.min(minL,n.l);maxL=math.max(maxL,n.l);maxEnd=math.max(maxEnd,n.s+n.l) end
    local clip=self.w:clipSource(self.index)
    local limit=clip and clip.length or self.track.edited and Project.maxSongBeats*4 or Project.maxSteps
    local minimum=self.track.edited and 4/self.track.edited.division or 1/960
    d.change=d.resize and {length=clamp(delta,minimum-minL,clip and limit-maxEnd or limit-maxL)}
        or {step=clamp(delta,-minS,clip and limit-maxEnd or limit-self:quantum()-maxS),pitch=clamp(pitch,-minP,127-maxP)}
    self:draw()
end
function R:finish(cancel)
    local d=self.drag;self.drag=nil;self.scrollDrag=nil
    if d and not cancel and self.w.project.tracks[self.index]==self.track then self.w:editSelected(self.index,d.change) end
end
function R:frame()
    if not input.isMouseButtonPressed(1) then self:finish(false) end
end
function R:pan(delta)
    self.start=clamp(self.start+delta,0,self.maximum);self.w.rollStart=self.start;self.w.bar=math.floor(self.start/16);self:draw()
end
function R:scroll(e)
    local point=self:point(e);local width=self.width-self.thumbWidth
    self:pan((width>0 and clamp((point.x-self.thumbWidth/2)/width,0,1)*self.maximum or 0)-self.start)
end
function R:draw()
    local w=self.w;local slot,shown=0,0;self.visible={}
    local first=math.ceil(self.start)
    for index,line in ipairs(self.lines) do
        local step=first+index-1;local props=line.layout.props
        props.position=v((step-self.start)*self.cw,0)
        props.size=v(not self.songGrid and step%self.barSteps==0 and 3 or step%4==0 and 2 or 1,self.height)
        self.owner.window:update(line)
    end
    local bars={}
    if self.songGrid then
        for _,marker in ipairs(Project.barBoundaries(w.project,self.start/4,(self.start+self.steps)/4,1030) or {}) do
            bars[#bars+1]=rect((marker.beat*4-self.start)*self.cw,0,3,self.height,colors.line)
        end
    end
    self.barGrid.layout.content=ui.content(bars);self.owner.window:update(self.barGrid)
    for id,source in ipairs(w:noteData(self.index) or {}) do
        local n,s,l=source.n,source.s,source.l
        if self.drag and w.noteSelection[id] then local d=self.drag.change;n=n+(d.pitch or 0);s=s+(d.step or 0);l=l+(d.length or 0) end
        if n>=self.base and n<self.base+self.rows and s<self.start+self.steps and s+math.max(l,0.06)>self.start then
            shown=shown+1
            if slot<#self.pool then
                slot=slot+1;self.visible[slot]=id;local r=self.pool[slot].layout.props
                local left,right=math.max(s,self.start),math.min(s+l,self.start+self.steps)
                r.visible=true;r.position=v(self.x+(left-self.start)*self.cw,self.y+(self.base+self.rows-1-n)*self.rh)
                r.size=v(math.max(3,(right-left)*self.cw-1),self.rh-2)
                r.color=w.selectionTrack==self.index and w.noteSelection[id] and colors.selected or colors.note
                self.owner.window:update(self.pool[slot])
            end
        end
    end
    for n=slot+1,#self.pool do self.pool[n].layout.props.visible=false;self.owner.window:update(self.pool[n]) end
    self.thumbWidth=math.max(18,self.width*self.steps/(self.maximum+self.steps))
    self.thumb.layout.props.position=v(self.maximum>0 and (self.width-self.thumbWidth)*self.start/self.maximum or 0,0)
    self.thumb.layout.props.size=v(self.thumbWidth,12);self.owner.window:update(self.thumb)
    self.caption.layout.props.text=string.format('Bars %.2f - %.2f  |  %d selected%s',self.songGrid and Project.barPosition(w.project,self.start/4) or self.start/16+1,self.songGrid and Project.barPosition(w.project,(self.start+self.steps)/4) or (self.start+self.steps)/16+1,#w:selectedNotes(self.index),shown>512 and '  |  Zoom in to see remaining notes' or '')
    self.owner.window:update(self.caption)
    if not self.drag and self.owner.noteVelocityRefresh then self.owner.noteVelocityRefresh() end
end
return R
