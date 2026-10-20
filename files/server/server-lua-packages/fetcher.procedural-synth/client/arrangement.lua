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
function M.commit(w,index,candidate)
    if w.recording or w.failedTake then w.cb.message('Keep, retry or cancel the take before arranging.');return false end
    local p=copy(w.project);p.tracks[index]=candidate
    local valid=Format.project(p)
    if not valid then w.cb.message('Arrangement change exceeds note, time, pitch or clip limits; previous data kept.');return false end
    if w.mode=='pattern' and w.playing then w:stop(true) end
    w.project.tracks[index]=valid.tracks[index];w.noteCache={};w.noteSelection={};w:touch();return true
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
    local first,finish=math.huge,0;local bar=Format.barBeats(w.project)*4
    for _,n in ipairs(notes) do first=math.min(first,n.s);finish=math.max(finish,n.s+math.max(n.l,1/960)) end
    first=math.floor(first/bar)*bar;local length=math.max(bar,math.ceil((finish-first)/bar-1e-9)*bar)
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
    w.arrangeClip=#a.clips;w.regionTrack=index;w.regionIndex=#a.regions;return true
end
function M.place(w,index,id,step)
    local t=w.project.tracks[index];local clip=t and t.arrangement and t.arrangement.clips[id]
    if not clip then w.cb.message('Create or capture a clip on this track first.');return false end
    local candidate=copy(t);local regions=candidate.arrangement.regions
    regions[#regions+1]={clip=id,s=step,l=clip.length,transpose=0,mute=false}
    if not M.commit(w,index,candidate) then return false end
    w.regionTrack=index;w.regionIndex=#regions;return true
end
function M.change(w,index,id,changes)
    local t=w.project.tracks[index];if not (t and t.arrangement and t.arrangement.regions[id]) then return false end
    local candidate=copy(t);for k,value in pairs(changes) do candidate.arrangement.regions[id][k]=value end
    return M.commit(w,index,candidate)
end
function M.duplicate(w,index,id)
    local t=w.project.tracks[index];local r=t and t.arrangement and t.arrangement.regions[id];if not r then return false end
    local candidate=copy(t);local regions=candidate.arrangement.regions;local next=copy(r);next.s=r.s+r.l;regions[#regions+1]=next
    if not M.commit(w,index,candidate) then return false end
    w.regionTrack=index;w.regionIndex=#regions;return true
end
function M.remove(w,index,id)
    local t=w.project.tracks[index];if not (t and t.arrangement and t.arrangement.regions[id]) then return false end
    local candidate=copy(t);table.remove(candidate.arrangement.regions,id)
    if not M.commit(w,index,candidate) then return false end
    w.regionIndex=nil;return true
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
    w.clipEdit=nil;w.arrangeClip=1;w.regionIndex=nil;return true
end
return M
