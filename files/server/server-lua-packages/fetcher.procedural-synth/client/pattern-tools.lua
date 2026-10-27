local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=require(prefix..'shared.schema')
local Format=require(prefix..'shared.project')(schema)
local Arrange=require(prefix..'client.arrangement')
local SoundSets=require(prefix..'client.soundsets')
local MIDI=require(prefix..'client.midi')
local M={}
local function copy(v) if type(v)~='table' then return v end;local out={};for k,x in pairs(v) do out[k]=copy(x) end;return out end
local function commit(w,p,selected)
    if not Arrange.commitProject(w,p) then return false end
    w:stop(true);w.clipEdit=nil;w.regionSelection={};w.regionIndex=nil;w.noteCache={};w.noteSelection={}
    w.selected=selected or math.min(w.selected,math.max(1,#w.project.tracks))
    w.bar=math.min(w.bar,w.project.length/16-1);w:configureAll();return true
end
function M.duplicate(w,index)
    local track=w.project.tracks[index];if not track then return false end
    if #w.project.tracks>=Format.maxTracks then w.cb.message('Track limit: '..Format.maxTracks..'.');return false end
    local p=copy(w.project);local t=copy(track);t.name=t.name:sub(1,43)..' copy'
    if t.kind=='midi' and not t.edited then
        if not w.song then w.cb.message('Load the source MIDI before duplicating this part.');return false end
        p=w:timingProject();if not p then return false end
        local notes=MIDI.editableSpans(w.song,t.part,t.automation and t.automation.pedal and true or nil)
        t.edited=Format.encodeNotes(notes,w.song.division)
        if not t.edited then w.cb.message('This part exceeds the editable-note budget.');return false end
        t.part=''
    end
    p.tracks[#p.tracks+1]=t
    if not commit(w,p,#p.tracks) then return false end
    w.mixerPage=math.floor((w.selected-1)/8)
    local lane=0;for n,t in ipairs(p.tracks) do if t.kind=='drum' then lane=lane+1 end end
    if track.kind=='drum' then w.drumPage=math.floor((lane-1)/8) end
    return true
end
function M.clearBar(w)
    local first=w.bar*16;local p=copy(w.project);local found=false
    if first<0 or first>=p.length or first%16~=0 then return false end
    for _,t in ipairs(p.tracks) do if t.kind=='drum' then
        t.steps=t.steps:sub(1,first)..string.rep('0',16)..t.steps:sub(first+17);found=true
    end end
    return found and commit(w,p) or false
end
function M.deleteBar(w)
    local p=copy(w.project);local first=w.bar*16;local last=first+16
    if first<0 or first>=p.length or first%16~=0 then return false end
    local shift=p.length>16;local found=false
    for _,t in ipairs(p.tracks) do
        if t.kind=='drum' then
            found=true;t.steps=t.steps:sub(1,first)..(shift and t.steps:sub(last+1)..string.rep('0',16) or string.rep('0',16)..t.steps:sub(last+1))
        elseif t.kind=='piano' then
            found=true;local notes={}
            for _,n in ipairs(t.notes) do
                if n.s<first or n.s>=last then
                    if shift then
                        if n.s>=last then n.s=n.s-16
                        elseif n.s+n.l>first then n.l=n.l-math.min(16,n.s+n.l-first) end
                    end
                    notes[#notes+1]=n
                end
            end
            t.notes=notes
        end
    end
    if not found then w.cb.message('No piano/drum pattern to edit.');return false end
    if shift then p.length=p.length-16 end
    return commit(w,p)
end
function M.capture(w)
    if w.clipEdit then w.cb.message('Leave source editing before capturing a pattern.');return false end
    local p=w:timingProject() or copy(w.project);local selected={};local firstTrack
    if not p.timeline then
        local bars=math.max(8,p.length/16)
        p.composition={bars=bars,numerator=4,denominator=4,loopStart=1,loopEnd=bars,loop=false}
        p.timeline={beats=bars*4,tempos={{beat=0,tempo=math.floor(60000000/p.bpm+.5)}}}
    end
    for index,t in ipairs(p.tracks) do if t.kind=='drum' or t.kind=='piano' or t.edited then
        if t.kind=='drum' and t.steps:sub(p.length+1):find('[1-9]') then
            w.cb.message('This shortened pattern has hits in hidden bars. Extend its length before capturing.');return false
        end
        for _,n in ipairs(t.notes) do if n.s>=p.length then
            w.cb.message('This shortened pattern has notes in hidden bars. Extend its length before capturing.');return false
        end end
        local notes,remaining={},{}
        if t.kind=='drum' then
            for step=0,p.length-1 do local velocity=tonumber(t.steps:sub(step+1,step+1)) or 0
                if velocity>0 then notes[#notes+1]={n=t.note,s=step+(step%2==1 and p.swing or 0),l=t.gate*p.bpm/60*4,v=velocity/9} end
            end
        elseif t.edited then
            for _,n in ipairs(assert(Format.decodeNotes(t.edited))) do
                local target=n.s<p.length and notes or remaining;target[#target+1]=n
            end
        else
            for _,n in ipairs(t.notes) do if n.s<p.length then local note=copy(n)
                note.s=note.s+(note.s%2==1 and p.swing or 0);notes[#notes+1]=note
            end end
        end
        if #notes>0 then
            local length=p.length;for _,n in ipairs(notes) do length=math.max(length,n.s+n.l) end
            if length>Format.maxSteps then w.cb.message('Shorten notes crossing the end of the 16-bar pattern before capturing.');return false end
            local role=t.sourceRole or SoundSets.trackRole(t)
            local division=t.edited and t.edited.division or 960
            t.kind='midi';t.part='';t.sourceRole=role;t.edited=assert(Format.encodeNotes(remaining,division))
            local a=t.arrangement or {clips={},regions={}};t.arrangement=a
            if #a.clips>=Format.maxClips or #a.regions>=Format.maxRegions then
                w.cb.message('A track has reached its source/placement limit; previous pattern kept.');return false
            end
            a.clips[#a.clips+1]={name=t.name:sub(1,32),length=length,notes=assert(Format.encodeNotes(notes,division))}
            a.regions[#a.regions+1]={clip=#a.clips,s=0,l=length,transpose=0,mute=false}
            -- Captured notes become the source exactly once.
            t.notes={};t.steps=string.rep('0',Format.maxSteps)
            selected[index]={[#a.regions]=true};firstTrack=firstTrack or index
        end
    end end
    if not firstTrack then w.cb.message('Add notes or drum hits to the sequencer before capturing its pattern.');return false end
    if not commit(w,p,firstTrack) then return false end
    local a=w.project.tracks[firstTrack].arrangement
    w.regionSelection=selected;w.regionTrack=firstTrack;w.regionIndex=#a.regions;w.arrangeClip=#a.clips;w.arrangeBar=0;w.arrangePage=0
    return true
end
return M
