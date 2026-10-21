local ui=require('openmw.ui')
local util=require('openmw.util')
local async=require('openmw.async')
local input=require('openmw.input')
local I=require('openmw.interfaces')
local Controls=require('scripts.multiplayer.fetcher.procedural-synth.client.controls')
local schema=require('scripts.multiplayer.fetcher.procedural-synth.shared.schema')
local SoundSets=require('scripts.multiplayer.fetcher.procedural-synth.client.soundsets')
local Project=require('scripts.multiplayer.fetcher.procedural-synth.shared.project')(schema)
local Dropdown=require('scripts.multiplayer.fetcher.procedural-synth.client.dropdown')
local Window=require('scripts.multiplayer.fetcher.procedural-synth.client.window')
local AutomationRoll=require('scripts.multiplayer.fetcher.procedural-synth.client.automation-roll')
local PianoRoll=require('scripts.multiplayer.fetcher.procedural-synth.client.piano-roll')
local Arranger=require('scripts.multiplayer.fetcher.procedural-synth.client.arranger')
local S={};S.__index=S
local v=util.vector2
local white=ui.texture{path='white'}
local c={bg=util.color.rgb(0.035,0.045,0.063),panel=util.color.rgb(0.075,0.09,0.12),track=util.color.rgb(0.14,0.17,0.21),
    accent=util.color.rgb(0.2,0.85,0.72),text=util.color.rgb(0.85,0.89,0.94),muted=util.color.rgb(0.5,0.58,0.68),note=util.color.rgb(0.35,0.65,0.95)}
local function rect(x,y,w,h,color) return {type=ui.TYPE.Image,props={position=v(x,y),size=v(w,h),resource=white,color=color}} end
local function label(text,x,y,w,size,color)
    return {type=ui.TYPE.Text,props={text=text,position=v(x,y),size=v(w,24),textSize=size or 13,textColor=color or c.text}}
end
local function cb(fn) return async:callback(function(...) fn(...);return false end) end
local names={'C','C#','D','D#','E','F','F#','G','G#','A','A#','B'}
local function pitch(n) return names[n%12+1]..(math.floor(n/12)-1) end
function S:button(text,x,y,w,fn,on)
    local r=rect(x,y,w,27,on and c.accent or c.track)
    r.content=ui.content{label(text,6,5,w-10,12,on and c.bg or c.text)};r.events={mouseClick=cb(fn)};return r
end
function S:element(layout)
    local e=self.window:create(layout);self.children[#self.children+1]=e;return e
end
function S:slider(x,y,width,value,fn)
    local r=rect(x,y,width,14,c.track);r.content=ui.content{rect(0,0,math.max(1,width*value),14,c.accent)}
    local function set(e) fn(math.max(0,math.min(1,e.offset.x/(width*self.window.scale)))) end
    r.events={mousePress=cb(set),mouseMove=cb(function(e) if e.button==1 then set(e) end end)};return r
end
function S:select(delta)
    self:releaseAll()
    if self.w.clipEdit and not self.w:leaveClip() then return end
    local n=#self.w.project.tracks;if n>0 then self.w.selected=(self.w.selected-1+delta)%n+1;self.w.revision=self.w.revision+1 end
end
function S:barControls(content,y,maxBar)
    local w=self.w
    content[#content+1]=self:button('< bar',20,y,62,function() w.bar=math.max(0,w.bar-1);w.revision=w.revision+1 end)
    content[#content+1]=label('Bar '..(w.bar+1)..' / '..maxBar,90,y+4,120)
    content[#content+1]=self:button('bar >',205,y,62,function() w.bar=math.min(maxBar-1,w.bar+1);w.revision=w.revision+1 end)
    local velocity=Controls.slider(self,'velocity','Velocity',{min=0.1,max=1,unit='%',step=0.01},function() return w.velocity end,function(value) w.velocity=value end,292,y-6,180)
    content[#content+1]=velocity
end
local function choicesRange(first,last)
    local out={};for n=first,last do out[#out+1]={id=n,label=tostring(n)} end;return out
end
local function meters()
    local out={};for _,denominator in ipairs{1,2,4,8,16,32,64} do for numerator=1,32 do
        out[#out+1]={id=numerator..'/'..denominator,label=numerator..'/'..denominator}
    end end;return out
end
function S:newSong(content)
    self.songDraft=self.songDraft or {bpm=120,numerator=4,denominator=4,bars=8}
    local d=self.songDraft
    content[#content+1]=label('CREATE A NEW SONG',20,175,870,20,c.accent)
    content[#content+1]=label('Start with an empty piano part. Add melodic and drum parts on the Song page.',20,222,870,14)
    content[#content+1]=label('Undo immediately after creation restores the current arrangement.',20,252,870,12,c.muted)
    local tempo=Controls.slider(self,'newBpm','Tempo',{min=40,max=240,step=1},function() return d.bpm end,function(value) d.bpm=value end,20,310,270)
    content[#content+1]=tempo
    content[#content+1]=self:button('Meter: '..d.numerator..'/'..d.denominator..' v',320,316,200,function()
        Dropdown.open(self,meters(),d.numerator..'/'..d.denominator,function(id)
            local n,b=id:match('(%d+)/(%d+)');d.numerator=tonumber(n);d.denominator=tonumber(b);self.w.revision=self.w.revision+1
        end,320,350,390)
    end)
    content[#content+1]=self:button('Length: '..d.bars..' bars v',550,316,220,function()
        Dropdown.open(self,choicesRange(1,256),d.bars,function(value) d.bars=value;self.w.revision=self.w.revision+1 end,400,350,370)
    end)
    content[#content+1]=self:button('Create song',20,407,200,function() self.cb.newSong(d.bpm,d.numerator,d.denominator,d.bars) end)
    content[#content+1]=self:button('Back',240,407,140,function() self.w.page='song';self.w.revision=self.w.revision+1 end)
    content[#content+1]=label('Save stores your project on this character. Export MIDI writes a local .mid file.',20,473,880,12,c.muted)
end
function S:song(content)
    local w,p=self.w,self.w.project;local s=p.composition
    if p.timeline or w.song then content[#content+1]=self:button('Timing map >',550,157,175,function() self:openTiming() end) end
    if p.timeline then content[#content+1]=self:button('Arrange clips >',740,157,150,function() self:openArranger() end) end
    if not s then
        content[#content+1]=label(w.song and 'Imported MIDI song / timing map' or 'Create a song from scratch',20,185,870,20,c.accent)
        content[#content+1]=self:button('New Song',20,240,180,function() w.page='newsong';w.revision=w.revision+1 end)
        content[#content+1]=label('Imported MIDI parts remain editable on Piano. New Song creates an independent arrangement.',20,300,880,13,c.muted)
        return
    end
    content[#content+1]=label('SONG SETTINGS / '..s.numerator..'/'..s.denominator..' / '..s.bars..' bars',20,167,870,16,c.accent)
    content[#content+1]=self:button('Length: '..s.bars..' bars v',20,204,190,function()
        Dropdown.open(self,choicesRange(1,256),s.bars,function(value)
            w:setComposition{bars=value,loopStart=math.min(s.loopStart,value),loopEnd=s.loopEnd==s.bars and value or math.min(s.loopEnd,value)}
        end,20,237,380)
    end)
    content[#content+1]=self:button('Meter: '..s.numerator..'/'..s.denominator..' v',225,204,190,function()
        Dropdown.open(self,meters(),s.numerator..'/'..s.denominator,function(id)
            local n,d=id:match('(%d+)/(%d+)');w:setComposition{numerator=tonumber(n),denominator=tonumber(d)}
        end,225,237,380)
    end)
    content[#content+1]=self:button('Song loop',430,204,120,function() w:setComposition{loop=not s.loop} end,s.loop)
    content[#content+1]=self:button('From bar '..s.loopStart..' v',565,204,155,function()
        Dropdown.open(self,choicesRange(1,s.loopEnd),s.loopStart,function(value) w:setComposition{loopStart=value} end,500,237,380)
    end)
    content[#content+1]=self:button('To bar '..s.loopEnd..' v',735,204,155,function()
        Dropdown.open(self,choicesRange(s.loopStart,s.bars),s.loopEnd,function(value) w:setComposition{loopEnd=value} end,510,237,380)
    end)
    content[#content+1]=label('Play song uses these bounds. Shortening preserves later notes; extending reveals them again.',20,249,870,12,c.muted)
    content[#content+1]=label('Timing map adds tempo/meter changes. Notes keep quarter-note positions; each meter change starts a bar.',20,277,870,12,c.muted)
    local page=w.songPage or 0
    for row=1,8 do
        local index=page*8+row;local t=p.tracks[index]
        if t then
            local y=316+(row-1)*33
            content[#content+1]=self:button(index..'  '..t.name:sub(1,20),20,y,185,function() w.selected=index;w.revision=w.revision+1 end,w.selected==index)
            content[#content+1]=self:button('Rename',215,y,70,function() self:renameTrack(index) end)
            content[#content+1]=label(w:presetName(t),295,y+5,220,12)
            content[#content+1]=self:button('Notes',530,y,75,function() w.selected=index;w.base=math.floor(t.note/12)*12;w.page='piano';w.revision=w.revision+1 end)
            content[#content+1]=self:button('Sound',620,y,75,function() self.cb.sound(index) end)
            content[#content+1]=label((t.output.slot or 0)==0 and 'This keyboard' or p.band[t.output.slot].name,710,y+5,175,11,c.muted)
        end
    end
    content[#content+1]=self:button('+ Melodic part v',20,602,200,function()
        local choices={};for _,r in ipairs{'keys','bass','guitar','pad'} do choices[#choices+1]={id=r,label=r} end
        Dropdown.open(self,choices,nil,function(role) w:addPart(role) end,20,385,390)
    end)
    content[#content+1]=self:button('+ Drum part v',235,602,200,function()
        local choices={};for _,r in ipairs{'kick','snare','hat','openhat','tom'} do choices[#choices+1]={id=r,label=r} end
        Dropdown.open(self,choices,nil,function(role) w:addPart(role) end,235,385,390)
    end)
    content[#content+1]=self:button('Parts 1-8',470,602,120,function() w.songPage=0;w.revision=w.revision+1 end,page==0)
    content[#content+1]=self:button('Parts 9-16',605,602,120,function() w.songPage=1;w.revision=w.revision+1 end,page==1)
    content[#content+1]=self:button('Route in Band',740,602,150,function() w.page='band';w.revision=w.revision+1 end)
    content[#content+1]=label('Each drum part has its own synth voice. Draw or record its hits in Notes. Save before Finish.',20,656,880,12,c.muted)
end
function S:openTiming()
    self:releaseAll();if self.w.clipEdit and not self.w:leaveClip() then return end
    self.w.page='timing';self.w.revision=self.w.revision+1
end
function S:timing(content)
    local w=self.w;local p=w:timingProject()
    if not p then content[#content+1]=label('Create or import a song first.',20,180,870,16,c.accent);return end
    local beat=w.timingBeat or 0
    local function position(value)
        w.timingBeat=value;w.timingBpm=math.max(40,math.min(240,60000000/Project.tempoAt(p,value)))
        w.timingNumerator,w.timingDenominator=Project.meterAt(p,value);w.revision=w.revision+1
    end
    w.timingBpm=w.timingBpm or math.max(40,math.min(240,60000000/Project.tempoAt(p,beat)))
    if not w.timingNumerator then w.timingNumerator,w.timingDenominator=Project.meterAt(p,beat) end
    content[#content+1]=self:button('< Song',20,163,110,function() w.page='song';w.revision=w.revision+1 end)
    content[#content+1]=label('TEMPO / METER MAP  |  Applying an edit stops the old playback clock.',150,169,740,14,c.accent)
    content[#content+1]=Controls.slider(self,'mapBeat','Quarter note (from 0)',{min=0,max=Project.maxSongBeats,step=.25},function() return w.timingBeat or 0 end,function(value) w.timingBeat=value end,20,211,270)
    content[#content+1]=self:button('Position by bar v',310,220,160,function()
        local choices={};local last=p.composition and p.composition.bars+1 or math.min(4096,Project.barAt(p,p.timeline.beats)+1)
        for bar=1,last do choices[#choices+1]={id=bar,label='Bar '..bar..' / Q '..string.format('%.4g',Project.barStart(p,bar))} end
        Dropdown.open(self,choices,Project.barAt(p,beat),function(bar) position(Project.barStart(p,bar)) end,310,252,500)
    end)
    local function apply(ok) if not ok then ui.showMessage('Choose a supported value or an existing point. The initial map point cannot be removed.') end end
    content[#content+1]=Controls.slider(self,'mapTempo','Tempo (BPM)',{min=40,max=240,step=1},function() return w.timingBpm end,function(value) w.timingBpm=value end,20,278,270)
    content[#content+1]=self:button('Set tempo',310,287,160,function() apply(w:editTempo(w.timingBeat or 0,w.timingBpm)) end)
    content[#content+1]=self:button('Remove tempo',485,287,160,function() apply(w:editTempo(w.timingBeat or 0,nil,true)) end)
    content[#content+1]=self:button('Meter: '..w.timingNumerator..'/'..w.timingDenominator..' v',20,352,270,function()
        Dropdown.open(self,meters(),w.timingNumerator..'/'..w.timingDenominator,function(id)
            local n,d=id:match('(%d+)/(%d+)');w.timingNumerator=tonumber(n);w.timingDenominator=tonumber(d);w.revision=w.revision+1
        end,20,384,450)
    end)
    content[#content+1]=self:button('Set meter',310,352,160,function() apply(w:editMeter(w.timingBeat or 0,w.timingNumerator,w.timingDenominator)) end)
    content[#content+1]=self:button('Remove meter',485,352,160,function() apply(w:editMeter(w.timingBeat or 0,nil,nil,true)) end)
    local tempoChoices,meterChoices={},{}
    for _,t in ipairs(p.timeline.tempos) do tempoChoices[#tempoChoices+1]={id=t.beat,label=string.format('Bar %.3g / Q %.5g / %.5g BPM',Project.barPosition(p,t.beat),t.beat,60000000/t.tempo)} end
    for _,m in ipairs(Project.meterMap(p)) do meterChoices[#meterChoices+1]={id=m.beat,label=string.format('Bar %d / Q %.5g / %d/%d',m.bar,m.beat,m.numerator,m.denominator)} end
    content[#content+1]=self:button('Tempo points: '..#tempoChoices..' v',20,415,410,function()
        Dropdown.open(self,tempoChoices,w.timingBeat,function(value)
            position(value)
        end,20,447,600)
    end)
    content[#content+1]=self:button('Meter points: '..#meterChoices..' v',475,415,410,function()
        Dropdown.open(self,meterChoices,w.timingBeat,function(value)
            position(value)
        end,290,447,600)
    end)
    for i=1,5 do
        content[#content+1]=label(tempoChoices[i] and tempoChoices[i].label or '',20,466+(i-1)*27,420,12)
        content[#content+1]=label(meterChoices[i] and meterChoices[i].label or '',475,466+(i-1)*27,410,12)
    end
    content[#content+1]=label('Pick any event in the scrollable lists to edit/remove it. Set adds or replaces a point at this position.',20,612,870,12,c.muted)
    content[#content+1]=label('Meters start a new bar; notes, clips and automation retain quarter-note positions. Undo restores timing.',20,639,870,12,c.muted)
    content[#content+1]=label('Shortening a song preserves later map points. Imported tempo values are retained; authored changes use 40-240 BPM.',20,666,870,11,c.muted)
end
function S:openArranger()
    self:releaseAll();if not self.w:leaveClip() then return end
    self.w.page='arrange';self.w.revision=self.w.revision+1
end
function S:arrange(content)
    local w,p=self.w,self.w.project;local index=w.selected;local t=p.tracks[index]
    content[#content+1]=self:button('< time',20,166,75,function() w.arrangeBar=math.max(0,(w.arrangeBar or 0)-(w.arrangeBars or 8));w.revision=w.revision+1 end)
    content[#content+1]=label('From bar '..((w.arrangeBar or 0)+1),108,172,190,12,c.accent)
    content[#content+1]=self:button('time >',300,166,75,function() w.arrangeBar=(w.arrangeBar or 0)+(w.arrangeBars or 8);w.revision=w.revision+1 end)
    content[#content+1]=self:button((w.arrangeBars or 8)..' bars in view v',390,166,145,function()
        Dropdown.open(self,{{id=4,label='4 bars'},{id=8,label='8 bars'},{id=16,label='16 bars'}},w.arrangeBars or 8,function(id) w.arrangeBars=id;w.revision=w.revision+1 end,390,198,390)
    end)
    content[#content+1]=self:button('Tracks 1-8',550,166,120,function() w.arrangePage=0;w.revision=w.revision+1 end,(w.arrangePage or 0)==0)
    content[#content+1]=self:button('Tracks 9-16',685,166,130,function() w.arrangePage=1;w.revision=w.revision+1 end,w.arrangePage==1)
    content[#content+1]=label(t and t.name or 'Select a track',20,212,210,13,c.accent)
    local id=w.arrangeClip or 1;local clips=t and t.arrangement and t.arrangement.clips or {};local clip=clips[id]
    content[#content+1]=self:button((clip and clip.name or 'Choose source clip')..' v',240,205,240,function()
        local choices={};for n,source in ipairs(clips) do choices[#choices+1]={id=n,label=source.name} end
        Dropdown.open(self,choices,id,function(value) w.arrangeClip=value;w.regionSelection={};w.regionIndex=nil;w.revision=w.revision+1 end,240,237,470)
    end)
    content[#content+1]=self:button('Edit source',495,205,110,function() if clip then self:releaseAll();w:editClip(index,id) end end)
    content[#content+1]=self:button('+ Empty clip',620,205,125,function() if w:newClip(index) then w.revision=w.revision+1 end end)
    content[#content+1]=self:button('Delete source',760,205,130,function() w:deleteClip(index,id) end)
    if clip then
        local draft=clip.name
        local function rename() self.typing=false;if draft~=clip.name then w:setClip(index,id,{name=draft}) end end
        content[#content+1]=self:element{type=ui.TYPE.TextEdit,props={position=v(20,241),size=v(190,25),text=clip.name,textSize=12,textColor=c.text},events={
            textChanged=cb(function(text) draft=text:gsub('%c',''):sub(1,32) end),focusGain=cb(function() self.typing=true end),focusLoss=cb(rename),
            keyPress=cb(function(e) if e.code==input.KEY.Escape then self:key(e.code,true) elseif e.code==input.KEY.Enter or e.code==input.KEY.NP_Enter then rename() end end)}}
        content[#content+1]=self:button(string.format('Source: %.2g quarters v',clip.length/4),225,240,175,function()
            local choices={};local bar=4
            for n=1,math.floor(Project.maxSteps/bar) do choices[#choices+1]={id=n*bar,label=n..' quarters'} end
            Dropdown.open(self,choices,clip.length,function(length) w:setClip(index,id,{length=length}) end,225,272,430)
        end)
    end
    local selected=p.tracks[w.regionTrack or 0];local region=selected and selected.arrangement and selected.arrangement.regions[w.regionIndex or 0]
    content[#content+1]=label(region and string.format('Placement: bar %.2f, %.2f bars, %+d semitones',Project.barPosition(p,region.s/4),Project.barPosition(p,(region.s+region.l)/4)-Project.barPosition(p,region.s/4),region.transpose) or 'Click an empty bar to place the selected source.',420,246,470,11,c.muted)
    self.arranger=Arranger.new(self,content)
    content[#content+1]=self:button('Duplicate placement',20,592,170,function() w:batchRegions('duplicate') end)
    content[#content+1]=self:button('Remove placement',205,592,165,function() w:batchRegions('remove') end)
    content[#content+1]=self:button('Pitch -',390,592,90,function() if region then w:batchRegions('pitch',-1) end end)
    content[#content+1]=self:button('Pitch +',495,592,90,function() if region then w:batchRegions('pitch',1) end end)
    content[#content+1]=self:button('Mute placement',605,592,140,function() if region then w:batchRegions('mute',not region.mute) end end,region and region.mute)
    content[#content+1]=self:button('Song settings',760,592,130,function() w.page='song';w.revision=w.revision+1 end)
    content[#content+1]=self:button('Selection: '..#w:selectedRegions()..' / Tools v',20,635,220,function()
        Dropdown.open(self,{{id='all',label='Select all placements (all tracks)'},{id='clear',label='Clear selection'},
            {id='left',label='Move selection one bar earlier'},{id='right',label='Move selection one bar later'}},nil,function(action)
            if action=='all' then w:selectAllRegions()
            elseif action=='clear' then w.regionSelection={};w.regionIndex=nil;w.revision=w.revision+1
            else w:moveRegions(action=='left' and -1 or 1) end
        end,20,355,540)
    end)
    content[#content+1]=self:button('Make unique',255,635,135,function() w:batchRegions('unique') end)
    content[#content+1]=self:button('Copy source to v',405,635,180,function()
        if not clip then return end
        local choices={};for n,track in ipairs(p.tracks) do if n~=index and track.edited then choices[#choices+1]={id=n,label=n..'  '..track.name} end end
        Dropdown.open(self,choices,nil,function(target) w:copyClip(index,id,target) end,400,355,490)
    end)
    if region then local source=selected.arrangement.clips[region.clip];local offsetTrack,offsetRegion=w.regionTrack,w.regionIndex
        content[#content+1]=Controls.slider(self,'regionOffset','Source offset (quarters)',{min=0,max=math.max(0,source.length/4-1/960),step=.0625},
            function() return (w.project.tracks[offsetTrack].arrangement.regions[offsetRegion].offset or 0)/4 end,function(value) w:changeRegion(offsetTrack,offsetRegion,{offset=value*4}) end,605,628,285)
    end
    content[#content+1]=label('Shift-click selects; drag moves the group in time. Right edge resizes one. Unique separates sources. Ctrl+A/D, Delete, arrows.',20,679,880,10,c.muted)
end
function S:automation(content)
    local w,p=self.w,self.w.project;local t=p.tracks[w.selected]
    if not t then content[#content+1]=label('Add a part on Song or Mixer first.',20,175,880,16,c.accent);return end
    local name=w.automationName or 'cutoff';local spec=Project.automationSpecs[name]
    local step=w.automationStep or 0;w.automationDraft=w.automationDraft or w:automationValue(w.selected,name,step)
    content[#content+1]=self:button(t.name..' v',20,163,225,function()
        local choices={};for n,track in ipairs(p.tracks) do choices[#choices+1]={id=n,label=n..'  '..track.name} end
        Dropdown.open(self,choices,w.selected,function(index) w:endAutomationWrite();w.selected=index;w.automationDraft=nil;w.automationPoint=nil;w.revision=w.revision+1 end,20,196,480)
    end)
    content[#content+1]=self:button(name..' v',260,163,225,function()
        local choices={};for _,key in ipairs(Project.automationOrder) do choices[#choices+1]={id=key,label=key} end
        Dropdown.open(self,choices,name,function(key) w:endAutomationWrite();w.automationName=key;w.automationDraft=nil;w.automationPoint=nil;w.revision=w.revision+1 end,260,196,480)
    end)
    content[#content+1]=self:button('Import MIDI controls',500,163,185,function() w:importControls(w.selected) end)
    content[#content+1]=self:button('Follow programs',700,163,185,function() w:followPrograms(w.selected,not t.followPrograms) end,t.followPrograms)
    content[#content+1]=Controls.slider(self,'automationValue',name,spec,function() return w.automationDraft end,function(value)
        w.automationDraft=value;if w.automationWriting then w:recordAutomation(w.selected,name,value) end
    end,20,206,360)
    content[#content+1]=self:button(w.automationLinear==false and 'Step' or 'Linear',400,215,110,function() w.automationLinear=not (w.automationLinear~=false);w.revision=w.revision+1 end)
    content[#content+1]=self:button(w.automationWriting and 'End write' or 'Write while playing',525,215,175,function()
        if w.automationWriting then w:endAutomationWrite() else w:beginAutomationWrite() end;w.revision=w.revision+1
    end,w.automationWriting)
    content[#content+1]=self:button('Add / update point',715,215,170,function()
        w:setAutomation(w.selected,name,w.automationStep or 0,w.automationDraft,w.automationLinear~=false);w.revision=w.revision+1
    end)
    local start=w.automationStart or 0;local steps=w.automationSteps or 64
    content[#content+1]=self:button('< time',20,263,75,function() w.automationStart=math.max(0,start-steps);w.revision=w.revision+1 end)
    content[#content+1]=self:button('time >',105,263,75,function() w.automationStart=math.min(Project.maxSongBeats*4-steps,start+steps);w.revision=w.revision+1 end)
    content[#content+1]=self:button(steps/16 ..' x 4/4 bars v',195,263,155,function()
        local choices={};for _,n in ipairs{16,32,64,128,256} do choices[#choices+1]={id=n,label=n/16 ..' x 4/4 bars'} end
        Dropdown.open(self,choices,steps,function(n) w.automationSteps=n;w.revision=w.revision+1 end,195,296,390)
    end)
    content[#content+1]=label(string.format('Quarter notes %.2f - %.2f | %s',start/4,(start+steps)/4,name),370,270,self.width-400,12,c.accent)
    self.automationRoll=AutomationRoll.new(self,content)
    content[#content+1]=label(string.format('%.4g',spec.max),20,304,52,12,c.muted)
    content[#content+1]=label(string.format('%.4g',spec.min),20,304+self.automationRoll.height-18,52,12,c.muted)
    local footer=self.height-157
    content[#content+1]=Controls.slider(self,'automationTime','Point at quarter note',{min=0,max=Project.maxSongBeats,step=.0625},function() return (w.automationStep or 0)/4 end,function(value) w.automationStep=value*4 end,20,footer,340)
    content[#content+1]=self:button('Delete selected',385,footer+9,160,function()
        if w.automationPoint then w:setAutomation(w.selected,name,w.automationPoint,nil,false,true);w.automationPoint=nil;w.revision=w.revision+1 end
    end)
    content[#content+1]=self:button('Back to Piano',565,footer+9,155,function() w:endAutomationWrite();w.page='piano';w.revision=w.revision+1 end)
    content[#content+1]=label('Click adds; drag a point moves it; right-click deletes. Time snaps to 1/64 notes. Undo restores edits.',20,self.height-91,self.width-50,12,c.muted)
    content[#content+1]=label('Write captures slider changes at the native playhead as one take. End write applies the take at the next bar.',20,self.height-64,self.width-50,12,c.muted)
    content[#content+1]=label('Import MIDI controls replaces performance lanes. Program changes use sound-set families; sample parts keep their sample.',20,self.height-37,self.width-50,11,c.muted)
end
function S:mixer(content)
    local w,p=self.w,self.w.project
    local page=w.mixerPage or 0
    content[#content+1]=self:button('<',20,150,28,function() w:cycleSoundSet(-1) end)
    content[#content+1]=self:button('Sound set: '..w:soundSetName()..' v',55,150,300,function()
        local choices={};for _,s in ipairs(SoundSets.entries) do choices[#choices+1]={id=s.id,label=s.name} end
        Dropdown.open(self,choices,p.soundSet,function(id) w:applySoundSet(id) end,55,181,590)
    end)
    content[#content+1]=self:button('>',362,150,28,function() w:cycleSoundSet(1) end)
    content[#content+1]=label('Changes sounds on all tracks. Undo restores the previous set.',410,155,480,11,c.muted)
    content[#content+1]=label('PART / TRACK',28,183,240,10,c.muted)
    content[#content+1]=label('SAVED SOUND',295,183,225,10,c.muted)
    content[#content+1]=label('LEVEL',554,183,90,10,c.muted)
    self.meters={}
    for row=1,8 do
        local index=page*8+row;local t=p.tracks[index]
        if t then
            local y=210+(row-1)*43
            content[#content+1]=self:button(index..'  '..t.name:sub(1,30),20,y,265,function() w.selected=index;w.revision=w.revision+1 end,index==w.selected)
            content[#content+1]=self:button('<',294,y,24,function() w:cyclePreset(index,-1) end)
            content[#content+1]=self:button(w:presetName(t)..' v',326,y,173,function()
                local choices={};for _,p in ipairs(w.catalog) do choices[#choices+1]={id=p.id,label=p.name,search=p.name..' '..(p.bank or '')..' '..(p.role or '')} end
                Dropdown.open(self,choices,t.presetId,function(id) w:preset(index,id) end,294,math.min(y+29,290),590)
            end)
            content[#content+1]=self:button('>',505,y,24,function() w:cyclePreset(index,1) end)
            content[#content+1]=self:slider(546,y+5,94,t.level,function(value) w:setTrack(index,'level',value) end)
            content[#content+1]=self:button('M',650,y,30,function() w:setTrack(index,'mute',not t.mute) end,t.mute)
            content[#content+1]=self:button('S',686,y,30,function() w:setTrack(index,'solo',not t.solo) end,t.solo)
            content[#content+1]=self:button('Sound',726,y,57,function() self.cb.sound(index) end)
            content[#content+1]=self:button('Mix',790,y,45,function() w.selected=index;w.page='channel';w.revision=w.revision+1 end)
            content[#content+1]=self:button('Del',843,y,42,function() w:remove(index) end)
            local meter=self:element(rect(295,y+31,1,3,c.accent));content[#content+1]=meter;self.meters[index]=meter
            local text=self:element(label('',546,y+29,340,10,c.muted));self.meterLabels[index]=text;content[#content+1]=text
        end
    end
    content[#content+1]=self:button('+ Piano clip',20,578,120,function() if w:add('piano') then w.page='piano' end end)
    content[#content+1]=self:button('+ Drum lane',150,578,120,function() if w:add('drum') then w.page='drums' end end)
    content[#content+1]=label('MASTER',302,582,72,12,c.muted)
    content[#content+1]=self:slider(380,584,190,p.master,function(value) p.master=value;w:configureAll();w:touch() end)
    content[#content+1]=self:button('Tracks 1-8',620,578,110,function() w.mixerPage=0;w.revision=w.revision+1 end,page==0)
    content[#content+1]=self:button('Tracks 9-16',740,578,120,function() w.mixerPage=1;w.revision=w.revision+1 end,page==1)
    local t=p.tracks[w.selected]
    content[#content+1]=self:button('Rename track',20,614,155,function() if t then self:renameTrack(w.selected) end end)
    content[#content+1]=label(t and ('Selected: '..t.name) or 'Select a track to edit its name.',195,623,495,14,c.accent)
    content[#content+1]=self:button('Delay / Reverb',705,614,180,function() w.page='returns';w.revision=w.revision+1 end)
    self:addMasterMeter(content,650)
end
function S:band(content)
    local w,p=self.w,self.w.project;local page=w.bandPage or 0
    content[#content+1]=label('BAND MEMBERS / INSTRUMENTS & PERFORMERS',20,158,500,13,c.accent)
    content[#content+1]=label('TRACK OUTPUTS',550,158,340,13,c.accent)
    content[#content+1]=label('Name                 Instrument                     Level        Mute',20,181,510,10,c.muted)
    local function update(slot,key,value)
        local old=p.band[slot];local m={name=old.name,target=old.target,level=old.level,mute=old.mute};m[key]=value
        w:setMember(slot,m)
    end
    for slot,m in ipairs(p.band) do
        local y=205+(slot-1)*42
        local name=self:element{type=ui.TYPE.TextEdit,props={position=v(20,y),size=v(98,27),text=m.name,textSize=11,textColor=c.text},
            events={textChanged=cb(function(text) update(slot,'name',text:gsub('%c',''):sub(1,48)) end),
                focusGain=cb(function() self.typing=true end),focusLoss=cb(function() self.typing=false end)}}
        content[#content+1]=name
        content[#content+1]=self:button(m.target and ((m.target.kind=='player' and 'Player #'..m.target.characterId or m.target.kind=='npc' and 'NPC #'..m.target.mpNum or 'Keyboard #'..m.target.mpNum)..(m.target.instrument and ' / '..m.target.instrument or '')..' v') or 'Bind performer v',128,y,180,function()
            local choices={{id=0,label='Unbound (silent)'},{id=-1,label='Create owned NPC performer (up to 8)'}};local targets={}
            if m.target and m.target.kind=='npc' then choices[#choices+1]={id=-2,label='Dismiss this owned NPC'} end
            for _,entry in ipairs(w.cb.instruments and w.cb.instruments() or {}) do
                choices[#choices+1]={id=entry.id,label=entry.label};targets[entry.id]=entry.target
            end
            Dropdown.open(self,choices,m.target and Project.targetKey(m.target) or 0,function(id)
                if id==-1 then if w.cb.hire then w.cb.hire(slot) end;return end
                if id==-2 then if w.cb.dismiss then w.cb.dismiss(slot) end;return end
                update(slot,'target',targets[id]);w.revision=w.revision+1
            end,128,math.min(y+29,290),575)
        end)
        content[#content+1]=self:slider(320,y+6,110,m.level,function(value) update(slot,'level',value) end)
        content[#content+1]=self:button('M',440,y,32,function() update(slot,'mute',not m.mute);w.revision=w.revision+1 end,m.mute)
        if m.target and m.target.kind=='player' then content[#content+1]=self:button('Invite',478,y,47,function() if w.cb.invite then w.cb.invite(slot) end end) end
        local status=w.cb.bandStatus and w.cb.bandStatus(slot) or 'Unbound'
        content[#content+1]=label(status,130,y+27,370,10,status=='Ready' and c.accent or c.muted)
        local index=page*8+slot;local track=p.tracks[index]
        if track then
            content[#content+1]=label(index..'  '..track.name:sub(1,21),550,y+5,170,11)
            local output=track.output.slot or 0
            local title=output==0 and 'This keyboard' or p.band[output].name
            content[#content+1]=self:button(title..' v',725,y,168,function()
                local choices={{id=0,label='This keyboard'}}
                for n,member in ipairs(p.band) do choices[#choices+1]={id=n,label=n..': '..member.name} end
                Dropdown.open(self,choices,output,function(id) w:setOutput(index,id) end,520,math.min(y+29,290),380)
            end)
        end
    end
    content[#content+1]=self:button('Refresh instruments',20,566,190,function() if w.cb.refreshBand then w.cb.refreshBand() end;w.revision=w.revision+1 end)
    content[#content+1]=self:button('Tracks 1-8',550,566,140,function() w.bandPage=0;w.revision=w.revision+1 end,page==0)
    content[#content+1]=self:button('Tracks 9-16',705,566,150,function() w.bandPage=1;w.revision=w.revision+1 end,page==1)
    content[#content+1]=label('Bind keyboards, invite players, or create owned NPCs. Players use F9 to ready an instrument and accept.',20,609,880,12,c.muted)
    content[#content+1]=label('Changing a binding or track output stops playback. Missing or busy members stay silent.',20,638,880,12,c.muted)
    content[#content+1]=label('Save stores routing. Consent is session-only. Leave / Not ready in F9 immediately silences your part.',20,667,880,12,c.accent)
end
local function db(value) return value>0.00001 and string.format('%.1f',20*math.log(value)/math.log(10)) or '-inf' end
function S:addMasterMeter(content,y)
    local text=self:element(label('',20,y,890,12,c.accent));self.meterLabels[0]=text;content[#content+1]=text
end
function S:channel(content)
    local w=self.w;local t=w.project.tracks[w.selected]
    content[#content+1]=self:button('< track',20,169,75,function() self:select(-1) end)
    content[#content+1]=label(t and ('MIX / '..t.name) or 'Select a track in Mixer',110,174,470,16,c.accent)
    content[#content+1]=self:button('track >',610,169,75,function() self:select(1) end)
    content[#content+1]=self:button('Back to mixer',710,169,175,function() w.page='mixer';w.revision=w.revision+1 end)
    if not t then return end
    local m=t.mixer or Project.mixer();t.mixer=m
    for n,entry in ipairs{{'EQ','eqEnabled',20},{'Compressor','compEnabled',475}} do
        local title,key,x=entry[1],entry[2],entry[3]
        content[#content+1]=label(title,x,218,260,18,c.accent)
        content[#content+1]=self:button(m[key]==1 and 'On' or 'Bypassed',x+290,212,120,function()
            w:setMixer(w.selected,key,1-m[key]);w.revision=w.revision+1
        end,m[key]==1)
    end
    local controls={
        {'highpass','High-pass (Hz)',20,260,'Hz',true,1}, {'bass','Bass shelf / 120 Hz (dB)',20,321,'dB',false,0.1},
        {'mid','Mid bell (dB)',20,382,'dB',false,0.1}, {'midFrequency','Mid frequency (Hz)',20,443,'Hz',true,10},
        {'treble','Treble shelf / 6 kHz (dB)',20,504,'dB',false,0.1},
        {'threshold','Threshold (dB)',475,260,'dB',false,0.1}, {'ratio','Ratio',475,321,'',true,0.1},
        {'attack','Attack (ms)',475,382,'ms',true,0.1}, {'release','Release (ms)',475,443,'ms',true,1},
        {'makeup','Makeup gain (dB)',475,504,'dB',false,0.1}}
    for _,entry in ipairs(controls) do
        local key,title,x,y,unit,log,step=entry[1],entry[2],entry[3],entry[4],entry[5],entry[6],entry[7];local spec=Project.mixerSpecs[key]
        content[#content+1]=Controls.slider(self,'mix:'..key,title,{min=spec[1],max=spec[2],unit=unit,log=log,step=step},
            function() return t.mixer[key] end,function(value) w:setMixer(w.selected,key,value) end,x,y,410)
    end
    local text=self:element(label('',20,552,865,14,c.accent));self.meterLabels[w.selected]=text;content[#content+1]=text
    for n,key in ipairs{'delaySend','reverbSend'} do
        content[#content+1]=Controls.slider(self,'send:'..key,n==1 and 'Delay send' or 'Reverb send',{min=0,max=1,unit='%',step=0.01},
            function() return t.mixer[key] end,function(value) w:setMixer(w.selected,key,value) end,20+(n-1)*455,603,265)
    end
    content[#content+1]=self:button('Returns',775,609,110,function() w.page='returns';w.revision=w.revision+1 end)
    content[#content+1]=label('Bypass preserves settings. Peak / RMS are dBFS; GR shows compressor reduction in dB.',20,658,890,10,c.muted)
    self:addMasterMeter(content,576)
end
function S:returns(content)
    local w=self.w;local e=w.project.effects
    content[#content+1]=label('SHARED DELAY / REVERB',20,174,570,18,c.accent)
    content[#content+1]=self:button('Back to mixer',710,169,175,function() w.page='mixer';w.revision=w.revision+1 end)
    for n,key in ipairs{'delayEnabled','reverbEnabled'} do
        local x=20+(n-1)*455
        content[#content+1]=label(n==1 and 'DELAY' or 'REVERB',x,220,265,18,c.accent)
        content[#content+1]=self:button(e[key]==1 and 'On' or 'Bypassed',x+290,214,120,function() w:setEffect(key,1-e[key]);w.revision=w.revision+1 end)
    end
    local controls={{'delayTime','Time (ms)',20,270,'ms',true,1},{'delayFeedback','Feedback',20,350,'%',false,0.01},
        {'delayReturn','Return level',20,430,'%',false,0.01},{'reverbDecay','Decay (seconds)',475,270,'s',true,0.05},
        {'reverbDamping','Damping (Hz)',475,350,'Hz',true,10},{'reverbReturn','Return level',475,430,'%',false,0.01}}
    for _,entry in ipairs(controls) do
        local key,title,x,y,unit,log,step=entry[1],entry[2],entry[3],entry[4],entry[5],entry[6],entry[7];local spec=Project.effectsSpecs[key]
        -- The shared numeric control displays seconds as milliseconds.
        if key=='reverbDecay' then title='Decay (ms)' end
        content[#content+1]=Controls.slider(self,'return:'..key,title,{min=spec[1],max=spec[2],unit=unit,log=log,step=step},
            function() return w.project.effects[key] end,function(value) w:setEffect(key,value) end,x,y,410)
    end
    content[#content+1]=label('Set sends with Mix on each track. Each physical output has its own delay and reverb tails.',20,535,890,13,c.muted)
    content[#content+1]=label('Each physical output: dry tracks + returns > master volume > member level > limiter',20,567,890,13,c.muted)
    content[#content+1]=label('Stop fades and clears effect tails. Delay time changes glide in pitch.',20,600,890,12,c.muted)
    self.returnMeter=self:element(label('',20,626,890,13,c.accent));content[#content+1]=self.returnMeter
    self:addMasterMeter(content,650)
end
function S:setPianoFocus(active)
    self.pianoFocus=active;self.window.expanded=active
    self.window.restoreExpanded=active and function() self:setPianoFocus(false) end or nil
    self.window:bounds();self.window.changed=true;self.w.revision=self.w.revision+1
end
function S:piano(content)
    local w,p=self.w,self.w.project;local t=p.tracks[w.selected]
    local top=self.pianoFocus and 50 or 169;local tools=top+38
    content[#content+1]=self:button('< track',20,top,70,function() self:select(-1) end)
    local source=w:clipSource()
    content[#content+1]=label(source and (t.name..' / '..source.name) or t and t.name or 'No track selected',104,top+5,self.width-600,15,c.accent)
    content[#content+1]=self:button('Rename',self.width-480,top,100,function() if t then self:renameTrack(w.selected) end end)
    content[#content+1]=self:button('track >',self.width-365,top,70,function() self:select(1) end)
    content[#content+1]=self:button('+ Piano',self.width-280,top,86,function() if p.composition then w:addPart('keys') else w:add('piano') end end)
    content[#content+1]=self:button(source and 'Arrange clips' or 'Make clip',self.width-185,top,140,function()
        self:releaseAll();if source then self:openArranger() elseif w:captureClip(w.selected) then self:openArranger() end
    end)
    if not t then return end
    if t.kind=='drum' then content[#content+1]=label('Choose a piano or MIDI track, or use the Drums page for this lane.',20,250,880,15,c.muted);return end
    local snapNames={[0]='Off',[0.25]='1/64',[0.5]='1/32',[1]='1/16',[2]='1/8',[4]='1/4'}
    content[#content+1]=self:button('Snap: '..snapNames[w.snap],20,tools,110,function()
        local values={0.25,0.5,1,2,4,0};local at=1;for n,q in ipairs(values) do if q==w.snap then at=n end end
        w.snap=values[at%#values+1];w.revision=w.revision+1
    end)
    content[#content+1]=self:button('Zoom -',140,tools,78,function() self:zoomRoll(2) end)
    content[#content+1]=self:button('Zoom +',228,tools,78,function() self:zoomRoll(0.5) end)
    if t.kind=='midi' and not source then content[#content+1]=self:button(p.composition and 'Song part' or t.edited and 'Edited MIDI' or 'Edit MIDI',self.width-195,tools,150,function() w:convertPart(w.selected) end,t.edited~=nil) end
    content[#content+1]=self:button(self.pianoFocus and 'Back to Piano [Esc]' or 'Expand editor',self.width-370,tools,165,function() self:setPianoFocus(not self.pianoFocus) end)
    self.roll=PianoRoll.new(self,content)
    local roll=self.roll;local footer=self.height-137
    content[#content+1]=label(string.format('%.2g quarters in view',roll.steps/4),320,tools+5,180,12,c.muted)
    for row=0,roll.rows-1 do content[#content+1]=label(pitch(roll.base+roll.rows-1-row),20,roll.y+row*roll.rh+3,52,12,c.muted) end
    self.playhead=self:element(rect(76,roll.y-5,3,4,c.accent));content[#content+1]=self.playhead
    content[#content+1]=self:button('Oct -',20,footer,62,function() w.base=math.max(0,roll.base-12);w.revision=w.revision+1 end)
    content[#content+1]=self:button('Oct +',90,footer,62,function() w.base=math.min(128-roll.rows,roll.base+12);w.revision=w.revision+1 end)
    content[#content+1]=self:button('< time',175,footer,70,function() self.roll:pan(-roll.steps) end)
    content[#content+1]=self:button('time >',255,footer,70,function() self.roll:pan(roll.steps) end)
    content[#content+1]=self:button('Len -',347,footer,57,function() w.noteLength=math.max(0.25,w.noteLength-(w.snap>0 and w.snap or 0.25));w.revision=w.revision+1 end)
    content[#content+1]=label(string.format('%.2g steps',w.noteLength),412,footer+5,95)
    content[#content+1]=self:button('Len +',510,footer,57,function() w.noteLength=math.min(256,w.noteLength+(w.snap>0 and w.snap or 0.25));w.revision=w.revision+1 end)
    local function velocity()
        local id=w:selectedNotes(w.selected)[1];return id and w:noteData(w.selected)[id].v or w.velocity
    end
    local velocityControl,refreshVelocity=Controls.slider(self,'noteVelocity','Velocity',{min=0.01,max=1,unit='%',step=0.01},velocity,function(value)
        w.velocity=value;if #w:selectedNotes(w.selected)>0 then w:editSelected(w.selected,{velocity=value},'note:velocity:'..w.selected,true) end
    end,self.width-330,footer-8,285)
    self.noteVelocityRefresh=refreshVelocity;content[#content+1]=velocityControl
    for n,entry in ipairs{{'Select all',function() w:selectAllNotes(w.selected);self.roll:draw() end},
        {'Copy',function() w:copyNotes(w.selected) end},{'Paste',function() w:pasteNotes(w.selected,self.roll.start) end},
        {'Duplicate',function() w:duplicateNotes(w.selected) end},{'Delete',function() w:deleteNotes(w.selected) end}} do
        content[#content+1]=self:button(entry[1],20+(n-1)*116,self.height-93,106,entry[2])
    end
    content[#content+1]=self:button('Record keyboard >',self.width-330,self.height-93,285,function() w.page='record';w.revision=w.revision+1 end)
    content[#content+1]=label(source and 'Shared source: edits update all linked placements. Record while stopped; Play source auditions this clip.' or t.kind=='midi' and not t.edited and 'Read only: Edit MIDI creates saved notes; the original file stays intact.' or 'Click adds. Right-click deletes. Shift-click selects. Drag moves; drag the right edge resizes.',20,self.height-51,self.width-45,12,c.muted)
    if source then content[#content+1]=self:button('Play source',self.width-195,tools,150,function() w:playClip() end) end
    content[#content+1]=label('Ctrl+A/C/V/D, Delete, arrow keys. Wheel: pitch; Shift-wheel: time; Ctrl-wheel: zoom.',20,self.height-25,self.width-45,12,c.muted)
end
function S:recordKey(note,on,velocity)
    self.recordHeld=self.recordHeld or {}
    if on then
        if self.recordHeld[note] or not self.w:recordOn(note,velocity) then return end
        self.recordHeld[note]=true
    else self.w:recordOff(note);self.recordHeld[note]=nil end
end
function S:record(content)
    local w=self.w;local t=w.project.tracks[w.selected];local base=w.recordBase or 60
    local source=w:clipSource()
    content[#content+1]=self:button('< track',20,169,70,function() self:select(-1) end)
    content[#content+1]=label(source and (t.name..' / '..source.name) or t and t.name or 'Add a piano track first',104,174,450,15,c.accent)
    content[#content+1]=self:button('track >',575,169,70,function() self:select(1) end)
    content[#content+1]=self:button('+ Piano',660,169,86,function() self:releaseAll();if w.project.composition then w:addPart('keys') else w:add('piano') end end)
    content[#content+1]=self:button('Piano roll',755,169,140,function() w.page='piano';w.revision=w.revision+1 end)
    content[#content+1]=label('QUANTIZED KEYBOARD RECORDING',20,222,850,18,c.accent)
    content[#content+1]=self:button('Start take',20,265,150,function() w:startTake(w.selected) end)
    content[#content+1]=self:button('Keep take',185,265,150,function() w:endTake(false) end)
    content[#content+1]=self:button('Cancel take',350,265,150,function() w:endTake(true) end)
    if w.failedTake then content[#content+1]=self:button('Retry take',515,265,150,function() w:recoverTake() end) end
    self.recordText=self:element(label(w:recordStatus(),20,320,875,17,c.accent));content[#content+1]=self.recordText
    content[#content+1]=label('Stopped: one-bar visual count-in from the current piano-roll view. Playing: overdub the audible clock.',20,365,880,12,c.muted)
    content[#content+1]=label(source and 'Shared source: record while stopped. The take ends at source length and updates all linked placements.' or 'Piano clips record to the next loop boundary. Edited MIDI records on the full song timeline.',20,393,880,12,c.muted)
    content[#content+1]=self:button('Oct -',20,430,80,function() w.recordBase=math.max(0,base-12);w.revision=w.revision+1 end)
    content[#content+1]=self:button('Oct +',110,430,80,function() w.recordBase=math.min(103,base+12);w.revision=w.revision+1 end)
    content[#content+1]=label('Grid: '..(w.snap>0 and (w.snap..' sixteenth steps') or 'Off')..'  |  Notes use the selected track sound and band destination.',210,436,680,12,c.muted)
    self.recordKeyboard={}
    for n=0,24 do
        local note=base+n;local black=names[note%12+1]:find('#')~=nil
        local key=rect(20+n*35,480,33,black and 85 or 120,black and c.track or c.text)
        key.content=ui.content{label(pitch(note),2,black and 59 or 94,32,10,black and c.text or c.bg)}
        key.events={mousePress=cb(function(e) if e.button==1 then self.mouseRecord=note;self:recordKey(note,true,math.max(0.2,math.min(1,e.offset.y/(120*self.window.scale)))) end end),
            mouseRelease=cb(function() self:recordKey(note,false);self.mouseRecord=nil end)}
        local element=self:element(key);self.recordKeyboard[note]={element=element,color=black and c.track or c.text};content[#content+1]=element
    end
    content[#content+1]=label('Play after count-in: Z S X D C V G B H N J M ,  |  mouse keys span two octaves.',20,628,880,12,c.muted)
    content[#content+1]=label('Keep commits one undo action. Cancel discards the take. Stop/Hide keep notes; Save before Finish.',20,660,880,12,c.muted)
end
function S:zoomRoll(factor)
    local source=self.w:clipSource();local minimum=source and math.min(8,source.length) or 8
    self.w.rollSteps=math.max(minimum,math.min(64,self.w.rollSteps*factor));self.w.revision=self.w.revision+1
end
function S:wheel(delta)
    if self.arranger and self.w.page=='arrange' then
        self.w.arrangeBar=math.max(0,math.min(self.arranger.maximum,(self.w.arrangeBar or 0)-math.floor(delta)));self.w.revision=self.w.revision+1;return
    end
    if not self.roll or self.w.page~='piano' then return end
    if input.isCtrlPressed and input.isCtrlPressed() then self:zoomRoll(delta>0 and 0.5 or 2)
    elseif input.isShiftPressed and input.isShiftPressed() then self.roll:pan(-delta*self.roll.steps/4)
    else self.w.base=math.max(0,math.min(128-self.roll.rows,self.roll.base+math.floor(delta*3)));self.w.revision=self.w.revision+1 end
end
function S:drums(content)
    local w,p=self.w,self.w.project
    local drums={};for n,t in ipairs(p.tracks) do if t.kind=='drum' then drums[#drums+1]={index=n,track=t} end end
    local page=w.drumPage or 0
    self:barControls(content,169,p.length/16)
    content[#content+1]=self:button('Duplicate to next bar',505,169,190,function() w:duplicateDrumBar() end)
    content[#content+1]=label('Copies all lanes; replaces the next bar.',20,204,280,11,c.muted)
    local gx,gy,cw=304,225,(self.width-334)/16
    for step=0,15 do content[#content+1]=label(tostring(step+1),gx+step*cw+6,204,cw,11,c.muted) end
    for row=1,8 do
        local entry=drums[page*8+row]
        if entry then
            local index,t=entry.index,entry.track;local y=gy+(row-1)*41
            content[#content+1]=self:button(t.name:sub(1,18),20,y,160,function() w.selected=index;w:audition(index);w.revision=w.revision+1 end,index==w.selected)
            content[#content+1]=self:button('-',190,y,25,function() w:setTrack(index,'note',math.max(0,t.note-1)) end)
            content[#content+1]=label(pitch(t.note),220,y+5,47)
            content[#content+1]=self:button('+',269,y,25,function() w:setTrack(index,'note',math.min(127,t.note+1)) end)
            for column=0,15 do
                local step=w.bar*16+column;local digit=t.steps:sub(step+1,step+1)
                local r=rect(gx+column*cw,y,cw-3,27,digit~='0' and c.accent or (column%4==0 and c.track or c.panel))
                r.content=ui.content{label(digit~='0' and digit or '',8,5,cw-8,12,c.bg)}
                r.events={mouseClick=cb(function() w:toggleStep(index,step) end)};content[#content+1]=r
            end
        end
    end
    self.playhead=self:element(rect(gx,gy-5,3,4,c.accent));self.grid={x=gx,y=gy-5,cw=cw};content[#content+1]=self.playhead
    local function add(preset,name,note)
        w:add('drum',preset,name,note)
    end
    content[#content+1]=self:button('+ Kick',20,578,90,function() add('b:kick','Kick',36) end)
    content[#content+1]=self:button('+ Snare',120,578,90,function() add('b:snare','Snare',38) end)
    content[#content+1]=self:button('+ Hat',220,578,90,function() add('b:hat','Hat',42) end)
    content[#content+1]=self:button('+ Open hat',320,578,110,function() add('b:openhat','Open hat',46) end)
    content[#content+1]=self:button('+ Kit',440,578,80,function() w:addKit(w.kitBank) end)
    content[#content+1]=self:button('Sound',530,578,110,function() if p.tracks[w.selected] then self.cb.sound(w.selected) end end)
    content[#content+1]=self:button('Lanes 1-8',650,578,100,function() w.drumPage=0;w.revision=w.revision+1 end,page==0)
    content[#content+1]=self:button('Lanes 9-16',760,578,110,function() w.drumPage=1;w.revision=w.revision+1 end,page==1)
    content[#content+1]=self:button('Kit: '..schema.kits[w.kitBank or 1].name,20,610,220,function()
        w.kitBank=(w.kitBank or 1)%#schema.kits+1;w.revision=w.revision+1
    end)
    content[#content+1]=label('Choose a kit, then + Kit. Sound edits the instrument.',260,616,470,12,c.accent)
    content[#content+1]=self:button('Capture lane',745,610,145,function() self:releaseAll();if w:captureClip(w.selected) then self:openArranger() end end)
    content[#content+1]=label('Play clips loops the piano and drum clips together. Play song also follows the imported MIDI tempo map.',20,657,880,12,c.muted)
end
function S:samplePage(content)
    local w=self.w
    if not w.cb.samples then content[#content+1]=label('Sample browser unavailable',20,180,870);return end
    if not self.samples then
        self.samples=w.cb.samples();self.samples:start()
        local t=w.project.tracks[w.selected];local ref=t and t.params.sample
        if ref then for k,value in pairs(ref) do self.samples[k]=value end end
    end
    local browser=self.samples;local t=w.project.tracks[w.selected]
    local function refresh() w.revision=w.revision+1 end
    content[#content+1]=label('INSTALLED GAME SAMPLES',20,166,870,18,c.accent)
    content[#content+1]=self:button(t and ('Part '..w.selected..': '..t.name..' v') or 'Choose a part v',20,207,390,function()
        local entries={};for n,track in ipairs(w.project.tracks) do entries[#entries+1]={id=n,label=n..': '..track.name} end
        Dropdown.open(self,entries,w.selected,function(n) w.selected=n;refresh() end,20,242,600)
    end)
    content[#content+1]=self:button(browser.category..' v',430,207,200,function()
        local entries={};for _,name in ipairs{'All','Voices','Effects','Music'} do entries[#entries+1]={id=name,label=name} end
        Dropdown.open(self,entries,browser.category,function(name) browser.category=name;refresh() end,430,242,400)
    end)
    content[#content+1]=self:button('Browse sounds v',650,207,240,function()
        Dropdown.open(self,browser:entries(),browser.path,function(path) browser:select(path);refresh() end,20,245,870)
    end)
    content[#content+1]=label((browser.path or 'Choose an installed sound. Search also finds race, collection, gender and voice type.'):sub(1,125),20,256,880,12)
    content[#content+1]=label(browser:description(),20,279,880,11,c.muted)
    self.sampleStatus=self:element(label('',20,310,880,11,c.accent));content[#content+1]=self.sampleStatus
    content[#content+1]=label('Start (seconds)',20,333,180,13,c.muted)
    content[#content+1]=label('End (seconds)',240,333,180,13,c.muted)
    local startText,endText=tostring(browser.offset),tostring(browser.offset+browser.duration)
    local function field(x,value,change)
        return self:element{type=ui.TYPE.TextEdit,template={content=ui.content{{type=ui.TYPE.Image,props={relativeSize=v(1,1),resource=white,color=c.track}}}},
            props={position=v(x,365),size=v(180,28),text=value,textSize=15,textColor=c.text},events={
            textChanged=cb(change),focusGain=cb(function() self.typing=true end),focusLoss=cb(function() self.typing=false end)}}
    end
    content[#content+1]=field(20,startText,function(text) startText=text:sub(1,20) end)
    content[#content+1]=field(240,endText,function(text) endText=text:sub(1,20) end)
    content[#content+1]=self:button('Apply trim',460,363,150,function()
        local first,last=tonumber(startText),tonumber(endText)
        if not first or not last or first~=first or last~=last or first<0 or first>600 or last-first<0.01 or last-first>30 then
            w.cb.message('Start must be 0-600 seconds; the excerpt must be 0.01-30 seconds long.');return
        end
        browser:stop();browser.offset=math.floor(first*1000+0.5)/1000;browser.duration=math.floor((last-first)*1000+0.5)/1000;refresh()
    end)
    content[#content+1]=label('Up to 30 seconds per excerpt; start within the first 10 minutes.',20,408,870,12,c.muted)
    content[#content+1]=self:button('Root: '..pitch(browser.root)..' v',20,451,200,function()
        local entries={};for n=0,127 do entries[#entries+1]={id=n,label=pitch(n)..' / MIDI '..n} end
        Dropdown.open(self,entries,browser.root,function(n) browser:stop();browser.root=n;refresh() end,20,280,390)
    end)
    content[#content+1]=self:button(browser.oneShot and 'One-shot' or 'Gated',240,451,180,function() browser:stop();browser.oneShot=not browser.oneShot;refresh() end,browser.oneShot)
    content[#content+1]=self:button('Audition',460,451,150,function()
        if not browser:audition() then w.cb.message('Wait until the preview is Ready, or choose another trim.');end
    end)
    content[#content+1]=self:button('Stop preview',630,451,180,function() browser:stop() end)
    content[#content+1]=label('Root plays at original speed. Gated follows MIDI note length; one-shot plays to the end.',20,499,870,12,c.muted)
    content[#content+1]=self:button('Use sample on part',20,543,250,function()
        local ref=browser:reference();if ref then browser:stop();w:setSample(w.selected,ref)
        else w.cb.message('Choose a sound and wait until Ready before assigning it.');end
    end)
    content[#content+1]=self:button('Sound / save preset',290,543,220,function() if t then self.cb.sound(w.selected) end end)
    content[#content+1]=self:button('Use synth keys',530,543,160,function() w:preset(w.selected,'b:studio-keys') end)
    content[#content+1]=self:button('Edit part sample',710,543,180,function()
        if t and t.params.sample then browser:stop();for k,value in pairs(t.params.sample) do browser[k]=value end;refresh() end
    end)
    content[#content+1]=label(t and t.params.sample and ('Assigned: '..t.params.sample.path):sub(1,125) or 'Assigned: synthesized sound',20,595,880,12,c.accent)
    content[#content+1]=label('Piano, recorded notes, imported MIDI and linked clips all play the assigned sound through its band output.',20,627,880,12,c.muted)
    content[#content+1]=label('Save stores the reference and file identity. Each listener needs the same installed file.',20,657,880,12,c.muted)
end
function S:renameTrack(index)
    local t=self.w.project.tracks[index];if not t then return end
    self:releaseAll();self.renameReturn=self.w.page;self.renameIndex=index;self.renameDraft=t.name
    self.w.page='rename';self.w.revision=self.w.revision+1
end
function S:renamePage(content)
    content[#content+1]=label('RENAME TRACK',20,183,870,20,c.accent)
    content[#content+1]=label('Name this part. Notes, sounds and band assignments stay attached to it.',20,235,870,14)
    local function cancel() self.w.page=self.renameReturn or 'mixer';self.w.revision=self.w.revision+1 end
    self.cancelRename=cancel
    local function apply()
        if self.w:renameTrack(self.renameIndex,self.renameDraft) then cancel()
        else self.w.cb.message('Enter a track name of 1-48 bytes without control characters.');end
    end
    content[#content+1]=self:element{type=ui.TYPE.TextEdit,
        template={content=ui.content{{type=ui.TYPE.Image,props={relativeSize=v(1,1),resource=white,color=c.track}}}},
        props={position=v(20,285),size=v(660,32),text=self.renameDraft or '',textSize=16,textColor=c.text},events={
            textChanged=cb(function(text) self.renameDraft=text end),focusGain=cb(function() self.typing=true end),focusLoss=cb(function() self.typing=false end),
            keyPress=cb(function(e) if e.code==input.KEY.Escape then self:key(e.code,true) elseif e.code==input.KEY.Enter or e.code==input.KEY.NP_Enter then apply() end end)}}
    content[#content+1]=self:button('Apply name',20,350,180,apply)
    content[#content+1]=self:button('Cancel',220,350,140,cancel)
end
function S:projects(content)
    local l=self.w.library
    content[#content+1]=label('PROJECT LIBRARY / RECOVERY',20,177,880,20,c.accent)
    if not l then return end
    content[#content+1]=label(l.status,20,213,880,12,c.muted)
    if l.action then
        content[#content+1]=label('Keep your unsaved changes before you '..l.actionCaption..'.',20,260,880,15)
        content[#content+1]=self:button('Save and continue',20,311,210,function() l:save(false) end)
        content[#content+1]=self:button('Keep recovery and continue',245,311,280,function() l:snapshot(true) end)
        content[#content+1]=self:button('Cancel',545,311,130,function() l:cancel() end)
        content[#content+1]=label('Recovery preserves a draft without replacing the named save. Up to three recent drafts are retained.',20,365,880,12,c.muted)
        return
    end
    if self.libraryMode=='saveAs' then
        content[#content+1]=label('Name the new project copy:',20,263,880,15)
        content[#content+1]=self:element{type=ui.TYPE.TextEdit,
            template={content=ui.content{{type=ui.TYPE.Image,props={relativeSize=v(1,1),resource=white,color=c.track}}}},
            props={position=v(20,306),size=v(680,32),text=self.saveAsDraft,textSize=15,textColor=c.text},events={
                textChanged=cb(function(text) self.saveAsDraft=text end),focusGain=cb(function() self.typing=true end),focusLoss=cb(function() self.typing=false end)}}
        content[#content+1]=self:button('Create project copy',20,365,240,function() if l:save(true,self.saveAsDraft) then self.libraryMode=nil;self.w.revision=self.w.revision+1 end end)
        content[#content+1]=self:button('Cancel copy',280,365,180,function() self.libraryMode=nil;self.w.revision=self.w.revision+1 end)
        return
    end
    content[#content+1]=self:button('Save current',20,250,155,function() l:save(false) end)
    content[#content+1]=self:button('Save As new',190,250,155,function() self.libraryMode='saveAs';self.saveAsDraft=self.w.project.name;self.w.revision=self.w.revision+1 end)
    content[#content+1]=self:button('Refresh',360,250,110,function() l:list() end)
    content[#content+1]=self:button('Back to Song',485,250,155,function() self.w.page='song';self.w.revision=self.w.revision+1 end)
    local selected
    for _,e in ipairs(l.entries) do if e.id==(self.librarySelected or l.active) then selected=e end end
    content[#content+1]=self:button(selected and selected.name..'  v' or 'Choose a saved project  v',20,292,680,function()
        local entries={};for _,e in ipairs(l.entries) do entries[#entries+1]={id=e.id,label=e.name..' / ID '..e.id} end
        Dropdown.open(self,entries,selected and selected.id,function(id)
            self.librarySelected=id;for _,e in ipairs(l.entries) do if e.id==id then self.libraryDraft=e.name end end
            self.deleteProject=nil;self.w.revision=self.w.revision+1
        end,20,250,700)
    end)
    content[#content+1]=self:element{type=ui.TYPE.TextEdit,
        template={content=ui.content{{type=ui.TYPE.Image,props={relativeSize=v(1,1),resource=white,color=c.track}}}},
        props={position=v(20,333),size=v(680,30),text=self.libraryDraft or selected and selected.name or '',textSize=14,textColor=c.text},events={
            textChanged=cb(function(text) self.libraryDraft=text end),focusGain=cb(function() self.typing=true end),focusLoss=cb(function() self.typing=false end)}}
    content[#content+1]=self:button('Open',20,378,120,function() if selected then l:open(selected.id,false) end end)
    content[#content+1]=self:button('Rename project',155,378,175,function() if selected then l:rename(selected,self.libraryDraft or selected.name) end end)
    content[#content+1]=self:button('Delete project',345,378,175,function() if selected then self.deleteProject=selected;self.w.revision=self.w.revision+1 end end)
    if self.deleteProject then
        local entry=self.deleteProject
        content[#content+1]=self:button('Confirm delete',535,378,165,function() self.deleteProject=nil;l:delete(entry) end)
        content[#content+1]=self:button('Cancel delete',715,378,160,function() self.deleteProject=nil;self.w.revision=self.w.revision+1 end)
    end
    content[#content+1]=label('RECOVERY SNAPSHOTS / newest first / restored as unsaved copies',20,434,880,14,c.accent)
    for n,e in ipairs(l.recoveries) do
        content[#content+1]=label(e.name..' / draft '..e.id,20,468+(n-1)*39,575,13)
        content[#content+1]=self:button('Restore draft',610,463+(n-1)*39,180,function() l:open(e.id,true) end)
    end
    content[#content+1]=label('Named saves: '..#l.entries..'/16. Autosave: every 30 seconds while dirty, including when Studio is hidden.',20,610,880,12,c.muted)
    content[#content+1]=label('Projects and recovery drafts belong to this character in the server database. MIDI exports stay local.',20,644,880,12,c.muted)
end
function S:render()
    if self.pianoFocus and self.w.page~='piano' then self.pianoFocus=false;self.window.expanded=false;self.window.restoreExpanded=nil end
    self.window:bounds();self.width,self.height=self.window.canvasWidth,self.window.canvasHeight
    if self.samples and self.w.page~='samples' then self.samples:stop() end
    self.sampleStatus=nil
    if self.lastPage=='record' and self.w.page~='record' then self:releaseAll() end;self.lastPage=self.w.page
    self.recordText=nil;self.recordKeyboard=nil
    self.controlEpoch=(self.controlEpoch or 0)+1;self.noteVelocityRefresh=nil
    if self.roll then self.roll:finish(true);self.roll=nil end
    if self.automationRoll then self.automationRoll:finish(true);self.automationRoll=nil end
    if self.arranger then self.arranger:finish(true);self.arranger=nil end
    Dropdown.close(self)
    self.controlEpoch=(self.controlEpoch or 0)+1
    self.typing=false
    if self.root then self.root:destroy() end
    for _,e in ipairs(self.children) do e:destroy() end
    self.children={};self.meters={};self.meterLabels={};self.returnMeter=nil;self.playhead=nil;self.grid=nil
    local w,p=self.w,self.w.project;local content={label('OPENMW / STUDIO',20,15,215,18)}
    if self.pianoFocus then
        content={label('PIANO EDITOR',20,13,200,21,c.accent)}
        content[#content+1]=self:button('Play song',230,12,96,self.cb.playSong)
        content[#content+1]=self:button('Play clips',335,12,96,self.cb.playClips)
        content[#content+1]=self:button('Stop',440,12,58,self.cb.stop)
        self.transport=self:element(label('',515,18,self.width-755,12,c.accent));content[#content+1]=self.transport
        content[#content+1]=self:button('Undo',self.width-226,12,80,function() w:undo() end,#w.undoStack>0)
        content[#content+1]=self:button('Redo',self.width-133,12,80,function() w:redo() end,#w.redoStack>0)
        self.bodyOffset=0;self:piano(content)
        self.root=self.window:root(content);self.revision=w.revision;return
    end
    local tabs={{'Synth',self.cb.synth},{'Song',function() w.page='song';w.revision=w.revision+1 end},{'Mixer',function() w.page='mixer';w.revision=w.revision+1 end},
        {'Piano',function() w.page='piano';w.revision=w.revision+1 end},{'Drums',function() w.page='drums';w.revision=w.revision+1 end},{'Band',function() w.page='band';if w.cb.refreshTargets then w.cb.refreshTargets() end;w.revision=w.revision+1 end},
        {'Auto',function() w.page='auto';w.revision=w.revision+1 end},{'Samples',function() w.page='samples';w.revision=w.revision+1 end}}
    for n,tab in ipairs(tabs) do content[#content+1]=self:button(tab[1],245+(n-1)*60,12,56,function() w:endAutomationWrite();tab[2]() end,w.page==tab[1]:lower()) end
    content[#content+1]=self:button('Hide [F8]',self.width-198,12,104,self.cb.hide)
    content[#content+1]=self:button('Finish',self.width-84,12,64,self.cb.close)
    self.transport=self:element(label('',20,43,self.width-280,12,c.accent));content[#content+1]=self.transport
    content[#content+1]=self:button('Undo',self.width-226,41,80,function() w:undo() end,#w.undoStack>0)
    content[#content+1]=self:button('Redo',self.width-133,41,80,function() w:redo() end,#w.redoStack>0)
    content[#content+1]=self:button('Play song',20,70,96,function() self.cb.playSong() end)
    content[#content+1]=self:button('Play clips',125,70,96,function() self.cb.playClips() end)
    content[#content+1]=self:button('Stop',230,70,58,self.cb.stop)
    local tempo=Controls.slider(self,'bpm','BPM',{min=40,max=240,step=1},function() return p.bpm end,function(value) w:setTempo(value) end,309,64,180)
    content[#content+1]=tempo
    local swing=Controls.slider(self,'swing','Swing',{min=0,max=0.6,unit='%',step=0.01},function() return p.swing end,function(value) p.swing=value;w:touch('swing',true) end,504,64,113)
    content[#content+1]=swing
    content[#content+1]=self:button(p.length/16 ..' clip bars',self.width-310,70,70,function() w:length(p.length%256+16) end)
    content[#content+1]=self:button('Clip loop',self.width-228,70,61,function() p.loop=not p.loop;w:touch() end,p.loop)
    content[#content+1]=self:button('Save',self.width-156,70,54,function() w:save() end)
    content[#content+1]=self:button('Projects',self.width-98,70,75,self.cb.load)
    local name=self:element{type=ui.TYPE.TextEdit,props={position=v(20,112),size=v(170,28),text=p.name,textSize=14,textColor=c.text},
        events={textChanged=cb(function(text) p.name=text:gsub('%c',''):sub(1,64);w:touch('name',true) end),focusGain=cb(function() self.typing=true end),focusLoss=cb(function() self.typing=false end)}}
    content[#content+1]=name
    content[#content+1]=self:button('New Song',200,112,88,function() self:releaseAll();w.page='newsong';w.revision=w.revision+1 end)
    content[#content+1]=self:button('< MIDI',299,112,65,function() self.cb.cycleMidi(-1);w.revision=w.revision+1 end)
    content[#content+1]=self:button(self.cb.midiLabel()..'  v',374,112,self.width-685,function()
        if self.cb.midiChoices then Dropdown.open(self,self.cb.midiChoices(),self.cb.midiSelected(),self.cb.midiSelect,30,145,850) end
    end)
    content[#content+1]=self:button('MIDI >',self.width-306,112,65,function() self.cb.cycleMidi(1);w.revision=w.revision+1 end)
    content[#content+1]=self:button('Import',self.width-229,112,92,self.cb.import)
    content[#content+1]=self:button('Export MIDI',self.width-125,112,100,function() w:exportMidi() end)
    local body={};self.bodyOffset=(w.page=='piano' or w.page=='drums' or w.page=='auto') and 0 or math.max(0,(self.width-940)/2)
    if w.page=='timing' then self:timing(body) elseif w.page=='auto' then self:automation(body) elseif w.page=='projects' then self:projects(body) elseif w.page=='rename' then self:renamePage(body) elseif w.page=='samples' then self:samplePage(body) elseif w.page=='arrange' then self:arrange(body) elseif w.page=='newsong' then self:newSong(body) elseif w.page=='song' then self:song(body) elseif w.page=='record' then self:record(body) elseif w.page=='piano' then self:piano(body) elseif w.page=='drums' then self:drums(body) elseif w.page=='channel' then self:channel(body) elseif w.page=='returns' then self:returns(body) elseif w.page=='band' then self:band(body) else self:mixer(body) end
    -- Keep the header above centered page containers in native hit testing.
    if self.bodyOffset>0.1 then table.insert(content,1,{type=ui.TYPE.Widget,props={position=v(self.bodyOffset,0),size=v(940,self.height)},content=ui.content(body)})
    else for _,item in ipairs(body) do content[#content+1]=item end end
    self.root=self.window:root(content);self.revision=w.revision
end
function S:resize() self:render() end
function S.new(workstation,callbacks)
    local self=setmetatable({kind='studio',w=workstation,cb=callbacks,window=Window.new(),children={},width=940,lastFrame=-1},S)
    self.window.keyPress=cb(function(e) if e.code==input.KEY.Escape then self:key(e.code,true) end end)
    self.window.pickup=callbacks.pickup;self:render();I.UI.addMode('Interface',{windows={}})
    if I.Controls and I.Controls.overrideUiControls then I.Controls.overrideUiControls(true) end
    return self
end
function S:releaseAll()
    self.w:endAutomationWrite()
    if self.roll then self.roll:finish(true) end
    if self.automationRoll then self.automationRoll:finish(true) end
    if self.arranger then self.arranger:finish(true) end
    for note in pairs(self.recordHeld or {}) do self.w:recordOff(note) end
    self.recordHeld={};self.recordKeys={};self.mouseRecord=nil;self.w:endTake(false)
end
function S:key(code,on)
    -- Focused native Lua widgets can consume key presses before onKeyPress.
    -- Root key events and engine events share one press/release latch.
    if code==input.KEY.Escape then
        if not on then self.escapeHeld=false;return end
        if self.escapeHeld then return end;self.escapeHeld=true
    end
    self.recordKeys=self.recordKeys or {}
    if not on and self.recordKeys[code] then self:recordKey(self.recordKeys[code],false);self.recordKeys[code]=nil;return end
    if self.dropdown then if on and code==input.KEY.Escape then Dropdown.close(self);self.restoreInterfaceFrames=2 end;return end
    if on and code==input.KEY.Escape and self.pianoFocus then self:setPianoFocus(false);self.restoreInterfaceFrames=2;return end
    if on and code==input.KEY.Escape then if self.w.page=='rename' and self.cancelRename then self.cancelRename();self.restoreInterfaceFrames=2 else self.cb.hide() end;return end
    if not on or self.typing then return end
    local ctrl=input.isCtrlPressed and input.isCtrlPressed()
    if self.w.page=='record' and not ctrl then
        for n,key in ipairs{input.KEY.Z,input.KEY.S,input.KEY.X,input.KEY.D,input.KEY.C,input.KEY.V,input.KEY.G,input.KEY.B,input.KEY.H,input.KEY.N,input.KEY.J,input.KEY.M,input.KEY.Comma} do
            if key==code and not self.recordKeys[code] then local note=(self.w.recordBase or 60)+n-1;self:recordKey(note,true);self.recordKeys[code]=note end
        end
        return
    end
    if ctrl then
        if self.roll then self.roll:finish(true) end
        if self.automationRoll then self.automationRoll:finish(true) end
        if self.arranger then self.arranger:finish(true) end
        if code==input.KEY.Z then if input.isShiftPressed and input.isShiftPressed() then self.w:redo() else self.w:undo() end
        elseif code==input.KEY.Y then self.w:redo()
        elseif self.arranger then
            if code==input.KEY.A then self.w:selectAllRegions()
            elseif code==input.KEY.D then self.w:batchRegions('duplicate') end
        elseif self.roll then
            local w=self.w;local n=w.selected
            if code==input.KEY.A then w:selectAllNotes(n);self.roll:draw()
            elseif code==input.KEY.C then w:copyNotes(n)
            elseif code==input.KEY.V then w:pasteNotes(n,self.roll.start)
            elseif code==input.KEY.D then w:duplicateNotes(n) end
        end
    elseif self.arranger then
        self.arranger:finish(true)
        if code==input.KEY.Delete then self.w:batchRegions('remove')
        elseif code==input.KEY.LeftArrow then self.w:moveRegions(-1)
        elseif code==input.KEY.RightArrow then self.w:moveRegions(1)
        elseif code==input.KEY.UpArrow then self.w:batchRegions('pitch',1)
        elseif code==input.KEY.DownArrow then self.w:batchRegions('pitch',-1) end
    elseif self.roll then
        local w=self.w
        if code==input.KEY.Delete then w:deleteNotes(w.selected)
        elseif code==input.KEY.LeftArrow then w:editSelected(w.selected,{step=-(w.snap>0 and w.snap or 0.25)})
        elseif code==input.KEY.RightArrow then w:editSelected(w.selected,{step=w.snap>0 and w.snap or 0.25})
        elseif code==input.KEY.UpArrow then w:editSelected(w.selected,{pitch=1})
        elseif code==input.KEY.DownArrow then w:editSelected(w.selected,{pitch=-1}) end
    end
end
function S:frame(now)
    self.window:frame(self)
    Dropdown.frame(self)
    -- Native Escape can pop Interface before Lua receives the key. Consuming
    -- it to leave the expanded editor must retain Studio's mouse/input mode.
    if self.restoreInterfaceFrames then
        local frames=self.restoreInterfaceFrames
        self.restoreInterfaceFrames=frames>1 and frames-1 or nil
        if I.UI.getMode()==nil then I.UI.addMode('Interface',{windows={}});return end
    end
    if I.UI.getMode()~='Interface' then self.cb.hide();return end
    if self.roll then self.roll:frame() end
    if self.automationRoll then self.automationRoll:frame() end
    if self.arranger then self.arranger:frame() end
    if self.samples then self.samples:frame() end
    if self.mouseRecord and not input.isMouseButtonPressed(1) then self:recordKey(self.mouseRecord,false);self.mouseRecord=nil end
    if self.revision~=self.w.revision then self:render() end
    if now-self.lastFrame<0.1 then return end;self.lastFrame=now
    self.transport.layout.props.text=string.format('%s  |  %.1fs  |  Beat %.1f  |  %s%s',self.w.awaiting and 'BUFFER' or (self.w.playing and 'PLAY' or 'STOP'),self.w.time,self.w.beat+1,self.w.project.name,self.w.dirty and ' *' or '')
    if self.w.mode=='song' and (self.w.project.timeline or self.w.song) then
        local view={timeline=self.w.project.timeline or self.w.song,composition=self.w.project.composition}
        local n,d=Project.meterAt(view,self.w.beat)
        self.transport.layout.props.text=self.transport.layout.props.text..string.format('  |  Bar %.2f  %d/%d  %.3g BPM',Project.barPosition(view,self.w.beat),n,d,60000000/Project.tempoAt(view,self.w.beat))
    end
    self.window:update(self.transport)
    if self.sampleStatus then
        local info=self.samples:info();local t=self.w.project.tracks[self.w.selected]
        local assigned=t and self.w.cb.sampleStatus(t.params.sample) or {status='No part selected'}
        self.sampleStatus.layout.props.text=self.samples:scanStatus()..'  |  Preview: '..info.status
            ..(info.duration and string.format(' / %.3fs',info.duration) or '')..'  |  Part: '..assigned.status
            ..((info.error and info.error~='') and (' / '..info.error) or '')
        self.window:update(self.sampleStatus)
    end
    if self.recordText then self.recordText.layout.props.text=self.w:recordStatus();self.window:update(self.recordText) end
    for note,key in pairs(self.recordKeyboard or {}) do key.element.layout.props.color=self.recordHeld and self.recordHeld[note] and c.accent or key.color;self.window:update(key.element) end
    for lane,meter in pairs(self.meters) do meter.layout.props.size=v(math.max(1,220*math.max(0,math.min(1,(20*math.log(math.max(0.001,self.w.cb.meter(lane)))/math.log(10)+60)/60))),3);self.window:update(meter) end
    for n,text in pairs(self.meterLabels) do
        local peak,rms,reduction=0,0,0
        if self.w.cb.meters then peak,rms,reduction=self.w.cb.meters(n) end
        text.layout.props.text=n==0 and string.format('OUTPUT MAX  pre-limiter peak %s dBFS  |  RMS %s dBFS%s',db(peak),db(rms),peak>1 and '  |  LIMITING - lower levels' or '')
            or string.format('Peak %s / RMS %s dBFS  |  GR %.1f dB',db(peak),db(rms),reduction)
        self.window:update(text)
    end
    if self.returnMeter and self.w.cb.returnMeters then
        local delay,reverb=self.w.cb.returnMeters()
        self.returnMeter.layout.props.text='Delay return '..db(delay)..' dBFS  |  Reverb return '..db(reverb)..' dBFS'
        self.window:update(self.returnMeter)
    end
    if self.playhead and self.roll then
        local source=self.w:clipSource();local step=self.w.mode=='song' and self.w.beat*4 or self.w.beat*4%(source and source.length or self.w.project.length)
        self.playhead.layout.props.visible=not (source and self.w.mode=='song') and step>=self.roll.start and step<self.roll.start+self.roll.steps
        self.playhead.layout.props.position=v(self.roll.x+(step-self.roll.start)*self.roll.cw,self.roll.y-5);self.window:update(self.playhead)
    elseif self.playhead then
        self.playhead.layout.props.position=v(self.grid.x+math.max(0,self.w.step%16)*self.grid.cw,self.grid.y);self.window:update(self.playhead)
    end
end
function S:destroy()
    if self.samples then self.samples:stop() end
    self:releaseAll()
    Dropdown.close(self)
    self.uiClosed=true
    if I.Controls and I.Controls.overrideUiControls then I.Controls.overrideUiControls(false) end
    self.root:destroy();for _,e in ipairs(self.children) do e:destroy() end
    if I.UI.getMode()=='Interface' then I.UI.removeMode('Interface') end
end
return S
