-- Local installed-asset discovery and preview. No speech commands, decoded PCM,
-- or game files cross the network; part parameters carry only a reference.
local schema=require('scripts.multiplayer.fetcher.procedural-synth.shared.schema')
local metadata=require('scripts.multiplayer.fetcher.procedural-synth.client.speech-catalog')
local M={};M.__index=M
local catalog,scan,scanError,texts
local function decorate(entry)
    if entry.category~='Voices' then return entry end
    local text,source,alternatives=texts:lookup(entry.id)
    entry.text=text;entry.textSource=source
    entry.label=entry.id:match('([^/]+)$')..(text~='' and '  /  '..text:sub(1,150) or '')
    entry.search=entry.id..' '..(metadata[entry.id] or '')..' '..text..' '..alternatives
    return entry
end
function M.category(path)
    return path:sub(1,6)=='music/' and 'Music' or (path:sub(1,9)=='sound/vo/' and 'Voices' or 'Effects')
end
function M.status(synth,reference)
    if not reference then return {status='Synth'} end
    if (reference.fadeIn or reference.fadeOut) and (synth.version or 0)<41 then
        return {status='Unavailable',error='Sample fades require the updated Studio client (API 41).'}
    end
    if (reference.reverse or reference.loop) and (synth.version or 0)<38 then
        return {status='Unavailable',error='Sample editing requires the updated Studio client (API 38).'}
    end
    local info=synth.sampleInfo(reference.path,reference.offset,reference.duration)
    if info.status=='Ready' and reference.hash and info.hash~=reference.hash then
        return {status='Mismatch',error='Installed file differs from the saved sample'}
    end
    if info.status=='Ready' and (reference.fadeIn or reference.fadeOut) and (reference.fadeIn or 0)+(reference.fadeOut or 0)>info.duration+1e-6 then
        return {status='Unavailable',error='Fades extend past the decoded excerpt.'}
    end
    if info.status=='Ready' and reference.loop and reference.loop.finish>info.duration+1e-6 then
        return {status='Unavailable',error='Loop extends past the decoded excerpt. Shorten its end point.'}
    end
    local pending=info
    for _,attack in ipairs(schema.attackOrder) do if reference.attacks and reference.attacks[attack] then
        local status=M.status(synth,reference.attacks[attack])
        if status.status=='Mismatch' or status.status=='Unavailable' then return {status=status.status,error=attack..' attack: '..(status.error or status.status)} end
        if status.status~='Ready' then pending=status end
    end end
    if info.status=='Ready' then return pending end
    return info
end
function M.new(synth,vfs,source,dialogue)
    if not texts then
        local VoiceText=require('scripts.multiplayer.fetcher.procedural-synth.client.voice-text')
        texts=VoiceText.new(dialogue,require('scripts.multiplayer.fetcher.procedural-synth.shared.voice-transcripts'))
    end
    return setmetatable({synth=synth,vfs=vfs,source=source,category='Voices',root=60,oneShot=true,offset=0,duration=30},M)
end
function M:start()
    if catalog then return end
    catalog={}
    scan=coroutine.create(function()
        local count=0
        for _,prefix in ipairs{'sound/','music/'} do for path in self.vfs.pathsWithPrefix(prefix) do
            if path:match('%.mp3$') or path:match('%.wav$') or path:match('%.ogg$') or path:match('%.flac$') or path:match('%.opus$') then
                catalog[#catalog+1]=decorate{id=path,label=path,category=M.category(path),search=path..' '..(metadata[path] or '')}
                if #catalog>=100000 then scanError='Showing the first 100000 installed sounds';return end
            end
            count=count+1;if count%300==0 then coroutine.yield() end
        end end
        table.sort(catalog,function(a,b) return a.id<b.id end)
    end)
end
function M:frame()
    texts:frame()
    if scan and coroutine.status(scan)~='dead' then
        local ok,err=coroutine.resume(scan);if not ok then scanError=tostring(err);scan=nil end
    end
    if self.preview then self.preview:keepAlive() end
    self:applyPendingRecipe()
end
function M:entries()
    local out={};for _,entry in ipairs(catalog or {}) do
        if self.category=='All' or self.category==entry.category then out[#out+1]=decorate(entry) end
    end;return out
end
function M:scanStatus()
    return scanError or ((scan and coroutine.status(scan)~='dead' and 'Scanning: ' or 'Installed: ')..#(catalog or {})..' sounds')
end
function M:select(path)
    self:stop();self.path=path;self.offset=0;self.duration=30;self.root=60;self.oneShot=true
    self.loop=nil;self.reverse=nil;self.gain=nil;self.waveform=nil;self.waveKey=nil;self.expectedHash=nil;self.editView=nil
    self.pendingRecipe=nil;self.recipeError=nil;self.fadeIn=nil;self.fadeOut=nil;self.editUndo={};self.editRedo={};self.editGroup=nil;self.sliceParent=nil
end
function M:loadReference(ref)
    ref=schema.sample(ref);if not ref or schema.isBank(ref) then return false end
    self:select(ref.path)
    for k,value in pairs(ref) do self[k]=value end
    self.expectedHash=ref.hash;return true
end
function M:trim(first,last)
    if type(first)~='number' or type(last)~='number' or first~=first or last~=last or first<0 or first>600 or last-first<.01 or last-first>30 then return false end
    self:stop();self.offset=math.floor(first*1000+.5)/1000;self.duration=math.floor((last-first)*1000+.5)/1000
    self.loop=nil;self.waveform=nil;self.waveKey=nil;self.editView=nil;self.pendingRecipe=nil;self.recipeError=nil;
    local ref={fadeIn=self.fadeIn,fadeOut=self.fadeOut};M.fitFades(ref,self.duration);self.fadeIn,self.fadeOut=ref.fadeIn,ref.fadeOut
    self.editUndo={};self.editRedo={};self.editGroup=nil;return true
end
function M:viewKey() return table.concat({self.path or '',self.offset,self.duration,tostring(self.reverse)},'|') end
function M:draftKey()
    local fields={self:viewKey(),tostring(self.root),tostring(self.oneShot),tostring(self.gain),tostring(self.fadeIn),tostring(self.fadeOut),tostring(self.expectedHash)}
    for _,key in ipairs{'start','finish','crossfade'} do fields[#fields+1]=tostring(self.loop and self.loop[key]) end
    local view=self.editView
    if view then
        fields[#fields+1]=view.key
        for _,key in ipairs{'trimFirst','trimLast','sensitivity','spacing','snap'} do fields[#fields+1]=tostring(view[key]) end
        for _,point in ipairs(view.points or {}) do fields[#fields+1]=tostring(point) end
    end
    return table.concat(fields,'|')
end
function M:recipe(kind)
    if self.pendingRecipe then return nil,'Wait for the saved sample to finish loading.' end
    local ref=self:reference();if not ref then return nil,'Choose a matching Ready sample before saving.' end
    local info=self:info();local span=math.floor(info.duration*1000+1e-6)/1000;local view=self.editView
    if view and view.key==self:viewKey() and (math.abs(view.trimFirst or 0)>.0001 or math.abs((view.trimLast or span)-span)>.0001) then
        return nil,'Apply trim before saving the sample or kit.'
    end
    local value={version=1,kind=kind,source=ref,span=span}
    if kind=='kit' then
        if not view or view.key~=self:viewKey() then return nil,'Open the waveform editor and add or detect slice markers first.' end
        value.markers=view.points;value.settings={sensitivity=view.sensitivity or .5,spacing=view.spacing or .08,snap=view.snap~=false}
    end
    local valid=require('scripts.multiplayer.fetcher.procedural-synth.shared.sample-library')(schema).recipe(value)
    if valid then return valid end
    return nil,'Check the sample bounds; a kit needs 1-15 ordered markers with at least 10ms per slice.'
end
function M:loadRecipe(value)
    local recipe=require('scripts.multiplayer.fetcher.procedural-synth.shared.sample-library')(schema).recipe(value)
    if not recipe or not self:loadReference(recipe.source) then return false end
    self.pendingRecipe=recipe;self.pendingRecipeKey=self:draftKey();self:applyPendingRecipe();return true
end
function M:applyPendingRecipe()
    local recipe=self.pendingRecipe;if not recipe then return end
    if self.pendingRecipeKey~=self:draftKey() then self.pendingRecipe=nil;return end
    local info=self:info();if info.status~='Ready' then return end
    self.pendingRecipe=nil
    if math.abs(math.floor(info.duration*1000+1e-6)/1000-recipe.span)>.0001 then
        self.recipeError='Decoded excerpt length differs from the saved sample. Choose a matching source or a new trim.';return
    end
    local settings=recipe.settings or {sensitivity=.5,spacing=.08,snap=true};local points={}
    for n,point in ipairs(recipe.markers or {}) do points[n]=point end
    self.editView={key=self:viewKey(),first=0,span=recipe.span,caret=0,trimFirst=0,trimLast=recipe.span,points=points,
        sensitivity=settings.sensitivity,spacing=settings.spacing,snap=settings.snap}
end
function M:setLoop(first,last,fade)
    local info=self:info();if info.status~='Ready' then return false end
    local reference={path=self.path,offset=self.offset,duration=self.duration,root=self.root,oneShot=false,hash=info.hash,
        reverse=self.reverse,fadeIn=self.fadeIn,fadeOut=self.fadeOut,loop={start=first,finish=last,crossfade=fade}}
    local ref=schema.sample(reference)
    if not ref or ref.loop.finish>info.duration+1e-6 then return false end
    self:stop();self.oneShot=false;self.loop=ref.loop;return true
end
function M:toggleLoop()
    if self.loop then self:stop();self.loop=nil;return true end
    local info=self:info();if info.status~='Ready' then return false end
    local length=math.floor(info.duration*1000)/1000
    if length<.02 then return false end
    return self:setLoop(0,length,math.min(.02,math.floor(length*250)/1000))
end
function M:wave()
    if not self.path or not self.synth.sampleWaveform then return nil end
    local key=table.concat({self.path,self.offset,self.duration},'|')
    if key~=self.waveKey then
        local result=self.synth.sampleWaveform(self.path,self.offset,self.duration)
        if result.minimum then self.waveform=result;self.waveKey=key end
    end
    return self.waveform
end
function M:slices(points)
    local ref=self:reference();local info=self:info()
    if not ref or type(points)~='table' or #points<1 or #points>15 then return nil end
    local length=math.floor(math.min(info.duration,ref.duration)*1000+1e-6)
    local edges={0};for _,point in ipairs(points) do
        if type(point)~='number' or point~=point then return nil end
        local ms=math.floor(point*1000+.5)
        if ms-edges[#edges]<10 or ms>length-10 then return nil end
        edges[#edges+1]=ms
    end
    edges[#edges+1]=length
    local out={}
    for n=1,#edges-1 do
        local first,last=edges[n],edges[n+1]
        out[n]=self:excerpt(first/1000,last/1000)
        if not out[n] then return nil,'Every slice must start within the first ten minutes of the file.' end
    end
    return out
end
-- Source fades are playback-relative seconds; shortened slices fit both ramps.
function M.fitFades(ref,length)
    local a,b=ref.fadeIn or 0,ref.fadeOut or 0
    if a+b>length then local ratio=length/(a+b);a=math.floor(a*ratio*1000)/1000;b=math.min(b,length-a) end
    ref.fadeIn=a>0 and a or nil;ref.fadeOut=b>0 and b or nil;return ref
end
function M:excerpt(first,last)
    local ref=self:reference();local info=self:info()
    if not ref or first<0 or last>info.duration+1e-6 or last-first<.01 then return nil end
    ref.offset=math.floor((ref.offset+(ref.reverse and info.duration-last or first))*1000+.5)/1000
    ref.duration=math.floor((last-first)*1000+.5)/1000;ref.loop=nil;ref.oneShot=true
    return schema.sample(M.fitFades(ref,ref.duration))
end
function M:editState() return {gain=self.gain,fadeIn=self.fadeIn,fadeOut=self.fadeOut} end
function M:finishEdit() self.editGroup=nil end
function M:setEdits(gain,first,last,group)
    local info=self:info();local length=math.floor((info.duration or 0)*1000+1e-6)/1000
    if info.status~='Ready' or (self.synth.version or 0)<41 then return false end
    if type(gain)~='number' or gain~=gain or gain<.25 or gain>8 or type(first)~='number' or type(last)~='number'
        or first~=first or last~=last or first<0 or last<0 then return false end
    gain=math.floor(gain*1000+.5)/1000;first=math.floor(first*1000+.5)/1000;last=math.floor(last*1000+.5)/1000
    if first+last>length+1e-7 then return false end
    if gain==(self.gain or 1) and first==(self.fadeIn or 0) and last==(self.fadeOut or 0) then return true end
    self.editUndo=self.editUndo or {};self.editRedo={}
    if not group or self.editGroup~=group then
        self.editUndo[#self.editUndo+1]=self:editState();if #self.editUndo>32 then table.remove(self.editUndo,1) end
    end
    self.editGroup=group;self:stop();self.gain=gain~=1 and gain or nil;self.fadeIn=first>0 and first or nil;self.fadeOut=last>0 and last or nil;return true
end
function M:undoEdit(redo)
    self:finishEdit();local from=redo and self.editRedo or self.editUndo;local to=redo and self.editUndo or self.editRedo
    if not from or #from==0 then return false end
    to[#to+1]=self:editState();local state=table.remove(from);self:stop();self.gain,self.fadeIn,self.fadeOut=state.gain,state.fadeIn,state.fadeOut;return true
end
function M:normalize()
    local info=self:info();local peak=info.peak
    if info.status~='Ready' or not self:reference() or (self.synth.version or 0)<41 or type(peak)~='number' or peak~=peak then return false,'Wait for Ready on an API 41 client before normalizing.' end
    if peak<1e-7 then return false,'The excerpt is silent; gain was kept.' end
    local target=10^(-1/20);local gain=math.floor(math.min(8,target/peak)*1000)/1000
    if gain<.25 then return false,'Source peak exceeds the supported gain range. Use a quieter source.' end
    self:finishEdit();local ok=self:setEdits(gain,self.fadeIn or 0,self.fadeOut or 0);self:finishEdit()
    return ok,gain==8 and peak*gain<target-.001 and 'Boost capped at 8x; this quiet source remains below -1 dBFS.' or 'Source normalized to at most -1 dBFS. Effects and mixer levels can still raise the output.'
end
function M:chops(count)
    if type(count)~='number' or count%1~=0 or count<2 or count>16 then return nil end
    local info=self:info();if info.status~='Ready' then return nil end
    local length=math.floor(math.min(info.duration,self.duration)*1000+1e-6)
    local points={};for n=1,count-1 do points[n]=math.floor(n*length/count)/1000 end
    return self:slices(points)
end
function M:detect(sensitivity,spacing,snap)
    if not self.synth.sampleTransients then return nil,'Automatic slicing requires the updated Studio client (API 40).' end
    if type(sensitivity)~='number' or sensitivity~=sensitivity or sensitivity<0 or sensitivity>1
        or type(spacing)~='number' or spacing~=spacing or spacing<.01 or spacing>2 then return nil,'Choose sensitivity 0-100% and minimum gap 10-2000ms.' end
    local info=self:info();if info.status~='Ready' then return nil,'Wait until the sample is Ready.' end
    local result=self.synth.sampleTransients(self.path,self.offset,self.duration,sensitivity,spacing,self.reverse or false,snap==true)
    if not result.points or result.hash~=info.hash then return nil,'The sample cannot be analysed as a raw excerpt.' end
    local points={};local previous=0;local length=math.floor(info.duration*1000)/1000
    if #result.points>15 then return nil,'Invalid slice analysis.' end
    for _,point in ipairs(result.points) do
        if type(point)~='number' or point~=point or point-previous<.01-1e-7 or point>length-.01+1e-7 then return nil,'Invalid slice analysis.' end
        points[#points+1]=point;previous=point
    end
    return {points=points,found=result.found or #points,truncated=result.truncated==true}
end
function M:waveRange(first,last)
    if not self.path or not self.synth.sampleWaveformRange then return nil end
    local key=table.concat({self.path,self.offset,self.duration,first,last,tostring(self.reverse)},'|')
    if key~=self.rangeKey then
        local wave=self.synth.sampleWaveformRange(self.path,self.offset,self.duration,first,last,self.reverse or false)
        if not wave.minimum then return nil end
        self.rangeKey=key;self.rangeWave=wave
    end
    return self.rangeWave
end
function M:snapPoint(point,enabled)
    local info=self:info();local length=info.duration or 0
    if enabled and self.synth.sampleZeroCrossing then
        point=self.synth.sampleZeroCrossing(self.path,self.offset,self.duration,point,self.reverse or false) or point
    end
    return math.max(0,math.min(math.floor(length*1000)/1000,math.floor(point*1000+.5)/1000))
end
function M:trimView(first,last)
    local info=self:info();if info.status~='Ready' or first<0 or last>info.duration+1e-6 or last-first<.01 then return false end
    local offset=self.offset+(self.reverse and info.duration-last or first)
    return self:trim(offset,offset+last-first)
end
function M:info()
    if self.recipeError then return {status='Unavailable',error=self.recipeError} end
    if not self.path then return {status='Choose a sound'} end
    local info=self.synth.sampleInfo(self.path,self.offset,self.duration)
    if info.status=='Ready' and self.expectedHash and info.hash~=self.expectedHash then
        return {status='Mismatch',error='Installed file differs from the saved sample'}
    end
    return info
end
function M:description()
    if not self.path then return '' end
    local text,source=texts:lookup(self.path)
    return text~='' and (text..'  ['..source..']') or metadata[self.path] or M.category(self.path)
end
function M:reference()
    local info=self:info();if info.status~='Ready' then return nil end
    local ref=schema.sample{path=self.path,offset=self.offset,duration=self.duration,root=self.root,oneShot=self.oneShot,hash=info.hash,
        gain=self.gain,reverse=self.reverse,loop=self.loop,fadeIn=self.fadeIn,fadeOut=self.fadeOut}
    if ref and M.status(self.synth,ref).status=='Ready' then return ref end
end
function M:audition()
    return self:auditionReference(self:reference())
end
function M:auditionReference(ref)
    self:stop();if not ref or M.status(self.synth,ref).status~='Ready' then return false end
    self.preview=self.synth.create(self.source)
    self.preview:set('attack',0.002);self.preview:set('sustain',1);self.preview:set('release',0.08)
    self.preview:set('cutoff',18000);self.preview:set('resonance',0)
    self.preview:sample(ref);self.preview:noteOn(ref.root,0.85);return true
end
function M:release() if self.preview then self.preview:noteOff(self.root);return true end;return false end
function M:stop() if self.preview then self.preview:close();self.preview=nil end end
return M
