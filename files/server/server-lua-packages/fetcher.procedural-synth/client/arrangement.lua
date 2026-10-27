-- Musical arrangement operations. The UI owns selection/capture; every change
-- validates a candidate document before replacing one track and entering history.
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local Format=require(prefix..'shared.project')(require(prefix..'shared.schema'))
local SoundSets=require(prefix..'client.soundsets')
local M={}
local function copy(t) if type(t)~='table' then return t end;local out={};for k,v in pairs(t) do out[k]=copy(v) end;return out end
function M.source(w,index)
    local e=w.clipEdit;local t=w.project.tracks[index]
    return e and e.track==index and t and t.arrangement and t.arrangement.clips[e.clip]
end
function M.commitProject(w,p,key)
    if w.recording or w.failedTake then w.cb.message('Keep, retry or cancel the take before arranging.');return false end
    -- Placement length is independent of the sequencer's pattern length.
    -- Extend an authored song when arranging beyond its previous end.
    if p.composition and p.timeline then
        local finish=p.timeline.beats
        for _,t in ipairs(p.tracks) do for _,r in ipairs(t.arrangement and t.arrangement.regions or {}) do
            finish=math.max(finish,(r.s+r.l)/4)
        end end
        if finish>p.timeline.beats then
            local bars=math.ceil(Format.barPosition(p,finish)-1e-8)-1
            if bars>256 then w.cb.message('An authored song supports at most 256 bars.');return false end
            local old=p.composition.bars;p.composition.bars=math.max(old,bars)
            if p.composition.loopEnd==old then p.composition.loopEnd=p.composition.bars end
            p.timeline.beats=Format.barStart(p,p.composition.bars+1)
        end
    end
    local valid=Format.project(p)
    if not valid then w.cb.message('Arrangement change exceeds note, time, pitch or clip limits; previous data kept.');return false end
    if w.mode=='pattern' and w.playing then w:stop(true) end
    w.project=valid;w.noteCache={};w.noteSelection={};w:touch(key);return true
end
function M.commit(w,index,candidate,key)
    local p=copy(w.project);p.tracks[index]=candidate
    return M.commitProject(w,p,key)
end
-- Selection is transient. Stable track/region indices are reset on reindexing.
function M.selection(w)
    local out={}
    if w.regionSelection==nil and w.regionTrack and w.regionIndex then
        local t=w.project.tracks[w.regionTrack];local r=t and t.arrangement and t.arrangement.regions[w.regionIndex]
        if r then return {{track=w.regionTrack,id=w.regionIndex,region=r}} end
    end
    for track,t in ipairs(w.project.tracks) do
        for id,r in ipairs(t.arrangement and t.arrangement.regions or {}) do
            if w.regionSelection and w.regionSelection[track] and w.regionSelection[track][id] then out[#out+1]={track=track,id=id,region=r} end
        end
    end
    return out
end
function M.select(w,track,id,toggle)
    local t=w.project.tracks[track];if not (t and t.arrangement and t.arrangement.regions[id]) then return false end
    local old=M.selection(w);w.regionSelection={}
    if toggle then for _,e in ipairs(old) do w.regionSelection[e.track]=w.regionSelection[e.track] or {};w.regionSelection[e.track][e.id]=true end end
    local lane=w.regionSelection[track] or {};w.regionSelection[track]=lane
    lane[id]=not lane[id]
    local selected=M.selection(w);local primary=lane[id] and {track=track,id=id} or selected[1]
    w.regionTrack=primary and primary.track;w.regionIndex=primary and primary.id
    if primary then w.selected=primary.track;w.arrangeClip=w.project.tracks[primary.track].arrangement.regions[primary.id].clip end
    return true
end
function M.selectAll(w)
    w.regionSelection={}
    for track,t in ipairs(w.project.tracks) do for id in ipairs(t.arrangement and t.arrangement.regions or {}) do
        w.regionSelection[track]=w.regionSelection[track] or {};w.regionSelection[track][id]=true
    end end
    local first=M.selection(w)[1];w.regionTrack=first and first.track;w.regionIndex=first and first.id
    if first then w.selected=first.track;w.arrangeClip=w.project.tracks[first.track].arrangement.regions[first.id].clip end
    w.revision=w.revision+1;return first~=nil
end
local function selectEntries(w,list)
    w.regionSelection={}
    for _,e in ipairs(list) do w.regionSelection[e.track]=w.regionSelection[e.track] or {};w.regionSelection[e.track][e.id]=true end
    local first=list[1];w.regionTrack=first and first.track;w.regionIndex=first and first.id
    if first then w.selected=first.track;w.arrangeClip=w.project.tracks[first.track].arrangement.regions[first.id].clip end
end
local function finite(value) return type(value)=='number' and value==value and math.abs(value)<math.huge end
function M.batch(w,action,value)
    local selected=M.selection(w);if #selected==0 then return false end
    local p=copy(w.project);local result={};local first,last=math.huge,0
    for _,e in ipairs(selected) do first=math.min(first,e.region.s);last=math.max(last,e.region.s+e.region.l) end
    if action=='move' then if not finite(value) then return false end;value=math.max(-first,value);if value==0 then return false end end
    if action=='pitch' and (not finite(value) or value%1~=0 or value==0) then return false end
    if action=='mute' and type(value)~='boolean' then return false end
    if action~='move' and action~='pitch' and action~='mute' and action~='duplicate' and action~='unique' and action~='remove' then return false end
    for _,e in ipairs(selected) do
        local a=p.tracks[e.track].arrangement;local r=a.regions[e.id];local id=e.id
        if action=='move' then r.s=r.s+value
        elseif action=='pitch' then r.transpose=r.transpose+value
        elseif action=='mute' then r.mute=value
        elseif action=='duplicate' then r=copy(r);r.s=r.s+last-first;a.regions[#a.regions+1]=r;id=#a.regions
        elseif action=='unique' then
            if #a.clips>=Format.maxClips then w.cb.message('Make unique needs a free source slot for each selected placement (8 per track).');return false end
            local source=copy(a.clips[r.clip]);source.name=source.name:sub(1,23)..' copy '..(#a.clips+1)
            a.clips[#a.clips+1]=source;r.clip=#a.clips
        end
        result[#result+1]={track=e.track,id=id}
    end
    if action=='remove' then
        for n=#selected,1,-1 do local e=selected[n];table.remove(p.tracks[e.track].arrangement.regions,e.id) end
        result={}
    end
    if not M.commitProject(w,p) then return false end
    selectEntries(w,result);return true
end
function M.moveBars(w,bars)
    if not finite(bars) or bars%1~=0 then return false end
    local selected=M.selection(w);local anchor=selected[1]
    for _,e in ipairs(selected) do if e.track==w.regionTrack and e.id==w.regionIndex then anchor=e end end
    if not anchor then return false end
    local bar=Format.barAt(w.project,anchor.region.s/4)
    local delta=(Format.barStart(w.project,math.max(1,bar+bars))-Format.barStart(w.project,bar))*4
    return M.batch(w,'move',delta)
end
function M.copyTo(w,index,id,target)
    local t=w.project.tracks[index];local source=t and t.arrangement and t.arrangement.clips[id];local destination=w.project.tracks[target]
    if not source or not destination or not destination.edited or index==target then return false end
    local candidate=copy(destination);candidate.arrangement=candidate.arrangement or {clips={},regions={}}
    if #candidate.arrangement.clips>=Format.maxClips then w.cb.message('Destination source library is full (8 per track).');return false end
    local clip=copy(source);candidate.arrangement.clips[#candidate.arrangement.clips+1]=clip
    if not M.commit(w,target,candidate) then return false end
    selectEntries(w,{});w.selected=target;w.arrangeClip=#candidate.arrangement.clips
    w.arrangePage=math.floor((target-1)/8);return true
end
function M.edit(w,index,id)
    if w.recording or w.failedTake then w.cb.message('Keep, retry or cancel the take first.');return false end
    local t=w.project.tracks[index];local clip=t and t.arrangement and t.arrangement.clips[id]
    if not clip then return false end
    w:stop(true);w.clipEdit={track=index,clip=id};w.selected=index;w.arrangeClip=id;w.noteSelection={};w.noteCache={}
    w.rollStart=0;w.rollSteps=math.min(16,clip.length);w.bar=0;w.page='piano';w.lastEditKey=nil;w.revision=w.revision+1;return true
end
function M.leave(w)
    if w.recording or w.failedTake then w.cb.message('Keep, retry or cancel the take first.');return false end
    if w.mode=='pattern' then w:stop(true) end
    w.clipEdit=nil;w.noteSelection={};w.noteCache={};w.rollStart=0;w.bar=0;w.lastEditKey=nil;w.revision=w.revision+1;return true
end
function M.empty(w,index)
    local t=w.project.tracks[index]
    if not w.project.timeline or not t or not t.edited then w.cb.message('Create a song part or convert this MIDI part with Edit MIDI first.');return false end
    local candidate=copy(t);candidate.arrangement=candidate.arrangement or {clips={},regions={}}
    local a=candidate.arrangement;if #a.clips>=Format.maxClips then w.cb.message('Clip library limit: 8 per track.');return false end
    a.clips[#a.clips+1]={name='Clip '..(#a.clips+1),length=Format.barBeats(w.project)*4,notes=Format.encodeNotes({},960)}
    if not M.commit(w,index,candidate) then return false end
    w.arrangeClip=#a.clips;return w.arrangeClip
end
function M.capture(w,index)
    if M.source(w,index) then return false end
    local t=w.project.tracks[index]
    if not w.project.timeline or not t then w.cb.message('Create or import a song first.');return false end
    if t.kind=='midi' and not t.edited then w.cb.message('Use Edit MIDI before capturing this part.');return false end
    local candidate=copy(t);candidate.arrangement=candidate.arrangement or {clips={},regions={}}
    local a=candidate.arrangement
    if #a.clips>=Format.maxClips or #a.regions>=Format.maxRegions then w.cb.message('Clip/placement limit reached.');return false end
    local notes,remaining={},{};local division=t.edited and t.edited.division or 960
    if t.edited then
        local ids={};for _,id in ipairs(w:selectedNotes(index)) do ids[id]=true end
        for id,n in ipairs(w:noteData(index)) do local target=ids[id] and notes or remaining;target[#target+1]=copy(n) end
    elseif t.kind=='piano' then
        notes=copy(t.notes);for _,n in ipairs(notes) do if n.s%2==1 then n.s=n.s+w.project.swing end end
    elseif t.kind=='drum' then
        for step=0,#t.steps-1 do local velocity=tonumber(t.steps:sub(step+1,step+1)) or 0
            if velocity>0 then notes[#notes+1]={n=t.note,s=step+(step%2==1 and w.project.swing or 0),l=t.gate*w.project.bpm/60*4,v=velocity/9} end
        end
    end
    if #notes==0 then w.cb.message('Select piano-roll notes, or choose a nonempty piano/drum clip.');return false end
    local first,finish=math.huge,0
    for _,n in ipairs(notes) do first=math.min(first,n.s);finish=math.max(finish,n.s+math.max(n.l,1/960)) end
    local startBar=Format.barAt(w.project,first/4);first=Format.barStart(w.project,startBar)*4
    local endBar=Format.barAt(w.project,math.max(0,finish/4-1e-8))+1
    local length=math.max(Format.barStart(w.project,startBar+1)*4-first,Format.barStart(w.project,endBar)*4-first)
    if not t.edited then first=0;length=math.max(w.project.length,finish) end
    if length>Format.maxSteps then w.cb.message('A reusable clip can span at most 64 quarter notes. Select a shorter phrase.');return false end
    for _,n in ipairs(notes) do n.s=n.s-first end
    if not t.edited then
        candidate.kind='midi';candidate.part='';candidate.notes={};candidate.steps=string.rep('0',Format.maxSteps)
        candidate.sourceRole=SoundSets.trackRole(t)
    end
    candidate.edited=assert(Format.encodeNotes(remaining,division))
    a.clips[#a.clips+1]={name='Clip '..(#a.clips+1),length=length,notes=assert(Format.encodeNotes(notes,division))}
    a.regions[#a.regions+1]={clip=#a.clips,s=first,l=length,transpose=0,mute=false}
    if not M.commit(w,index,candidate) then return false end
    w.arrangeClip=#a.clips;M.select(w,index,#a.regions,false);return true
end
function M.place(w,index,id,step)
    local t=w.project.tracks[index];local clip=t and t.arrangement and t.arrangement.clips[id]
    if not clip then w.cb.message('Create or capture a clip on this track first.');return false end
    local candidate=copy(t);local regions=candidate.arrangement.regions
    regions[#regions+1]={clip=id,s=step,l=clip.length,transpose=0,mute=false}
    if not M.commit(w,index,candidate) then return false end
    M.select(w,index,#regions,false);return true
end
function M.change(w,index,id,changes)
    local t=w.project.tracks[index];if not (t and t.arrangement and t.arrangement.regions[id]) then return false end
    local candidate=copy(t);for k,value in pairs(changes) do candidate.arrangement.regions[id][k]=value end
    return M.commit(w,index,candidate,changes.offset~=nil and 'note:regionOffset:'..index..':'..id or nil)
end
function M.duplicate(w,index,id)
    local t=w.project.tracks[index];local r=t and t.arrangement and t.arrangement.regions[id];if not r then return false end
    local candidate=copy(t);local regions=candidate.arrangement.regions;local next=copy(r);next.s=r.s+r.l;regions[#regions+1]=next
    if not M.commit(w,index,candidate) then return false end
    M.select(w,index,#regions,false);return true
end
function M.remove(w,index,id)
    local t=w.project.tracks[index];if not (t and t.arrangement and t.arrangement.regions[id]) then return false end
    local candidate=copy(t);table.remove(candidate.arrangement.regions,id)
    if not M.commit(w,index,candidate) then return false end
    selectEntries(w,{});return true
end
function M.clip(w,index,id,changes)
    local t=w.project.tracks[index];if not (t and t.arrangement and t.arrangement.clips[id]) then return false end
    local candidate=copy(t);for k,value in pairs(changes) do candidate.arrangement.clips[id][k]=value end
    return M.commit(w,index,candidate)
end
function M.deleteClip(w,index,id)
    local t=w.project.tracks[index];if not (t and t.arrangement and t.arrangement.clips[id]) then return false end
    local candidate=copy(t);local a=candidate.arrangement;local regions={}
    for _,r in ipairs(a.regions) do if r.clip~=id then if r.clip>id then r.clip=r.clip-1 end;regions[#regions+1]=r end end
    table.remove(a.clips,id);a.regions=regions
    if not M.commit(w,index,candidate) then return false end
    w.clipEdit=nil;w.arrangeClip=1;selectEntries(w,{});return true
end
return M
