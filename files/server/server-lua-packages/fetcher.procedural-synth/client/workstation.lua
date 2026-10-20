-- Transport, parts, clip editing and routing; no UI or engine dependencies.
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=require(prefix..'shared.schema')
local format=require(prefix..'shared.project')(schema)
local MIDI=require(prefix..'client.midi')
local SoundSets=require(prefix..'client.soundsets')
local W={};W.__index=W
local function copy(value)
    if type(value)~='table' then return value end
    local out={};for k,v in pairs(value) do out[k]=copy(v) end;return out
end
local function equal(a,b)
    if type(a)~=type(b) then return false end
    if type(a)~='table' then return a==b end
    for k,v in pairs(a) do if not equal(v,b[k]) then return false end end
    for k in pairs(b) do if a[k]==nil then return false end end
    return true
end
function W.new(cb)
    local self=setmetatable({cb=cb,project=format.new(),catalog=schema.builtins,selected=1,page='mixer',bar=0,base=60,
        noteLength=1,velocity=0.8,time=0,beat=0,step=-1,playing=false,pending={},counts={},preview={},wall=0,dirty=false,revision=0,generation=0,
        undoStack={},redoStack={},noteSelection={},snap=1,rollSteps=16},W)
    self.checkpoint=copy(self.project);return self
end
function W:touch(key,quiet)
    if key and key:find('^sound:') then self.project.soundSet='' end
    if self.native and self.playing and self.checkpoint.loop~=self.project.loop then self:stop(true) end
    if self.native and self.playing and (not key or key=='swing' or key:find('^note:') or key=='sequence') then self.sequenceDirty=true end
    if self.editDepth and self.editDepth>0 then self.transactionTouched=true;return end
    if not equal(self.checkpoint,self.project) then
        if not key or key~=self.lastEditKey or self.wall-(self.lastEditTime or -1)>0.4 then
            self.undoStack[#self.undoStack+1]=self.checkpoint
            if #self.undoStack>64 then table.remove(self.undoStack,1) end
        end
        self.redoStack={};self.checkpoint=copy(self.project);self.lastEditKey=key;self.lastEditTime=self.wall
    end
    self.dirty=true;if not quiet then self.revision=self.revision+1 end;self.generation=self.generation+1
end
function W:restore(project)
    self:stop(true)
    if self.project.midi~=project.midi then self.song=nil end
    self.project=copy(project);self.checkpoint=copy(project);self.lastEditKey=nil
    self.selected=math.max(1,math.min(self.selected,#project.tracks));self.bar=math.min(self.bar,project.length/16-1)
    self.noteSelection={};self.noteCache={};self:cacheSpans();self:configureAll();self.dirty=true;self.revision=self.revision+1;self.generation=self.generation+1
    if self:needsMidi() and not self.song then self.cb.importMidi(project.midi) end
end
function W:undo()
    local project=table.remove(self.undoStack);if not project then return false end
    self.redoStack[#self.redoStack+1]=copy(self.project);self:restore(project);return true
end
function W:redo()
    local project=table.remove(self.redoStack);if not project then return false end
    self.undoStack[#self.undoStack+1]=copy(self.project);self:restore(project);return true
end
function W:audible(index)
    local solo=false;for _,t in ipairs(self.project.tracks) do if t.solo then solo=true end end
    local t=self.project.tracks[index];return t and not t.mute and (not solo or t.solo)
end
function W:configure(index)
    local t=self.project.tracks[index]
    if t then self.cb.configure(index,t.params,self:audible(index) and t.level or 0,t.mixer) end
end
function W:setMixer(index,name,value)
    local t=self.project.tracks[index];local spec=format.mixerSpecs[name]
    if not t or not spec then return false end
    local state=copy(t.mixer or format.mixer());state[name]=value
    state=format.mixer(state);if not state then return false end
    t.mixer=state;self:configure(index);self:touch('mixer:'..index..':'..name,true);return true
end
function W:setEffect(name,value)
    local state=copy(self.project.effects or format.effects());state[name]=value
    state=format.effects(state);if not state then return false end
    self.project.effects=state;if self.cb.effects then self.cb.effects(state,self.project.master) end
    self:touch('effects:'..name,true);return true
end
function W:configureBand()
    if self.cb.band then
        local routes={};for n,t in ipairs(self.project.tracks) do routes[n]=t.output.slot or 0 end
        self.cb.band(self.project.band,routes)
    end
end
function W:setOutput(index,slot)
    local t=self.project.tracks[index]
    if not t or type(slot)~='number' or slot%1~=0 or slot<0 or slot>format.maxMembers then return false end
    self:stop(true);t.output={kind='keyboard',slot=slot};self:configureBand();self:touch('output:'..index);return true
end
function W:setMember(slot,member)
    if not self.project.band[slot] then return false end
    local band=copy(self.project.band);band[slot]=copy(member);band=format.band(band);if not band then return false end
    if not equal(band[slot].target,self.project.band[slot].target) then self:stop(true) end
    self.project.band=band;self:configureBand();self:touch('member:'..slot,true);return true
end
function W:configureAll()
    self:configureBand()
    for n in ipairs(self.project.tracks) do self:configure(n) end
    if self.cb.prune then self.cb.prune(#self.project.tracks) end
    if self.cb.effects then self.cb.effects(self.project.effects,self.project.master) end
end
function W:setCatalog(entries) self.catalog=entries or {};self.revision=self.revision+1 end
function W:preset(index,id)
    local t=self.project.tracks[index];if not t then return false end
    for _,p in ipairs(self.catalog) do
        if p.id==id then
            local params=schema.state(p.state);if not params then return false end
            params.polyphony=math.min(8,params.polyphony)
            t.params=params;t.presetId=id;self.project.soundSet='';self:configure(index);self:touch();return true
        end
    end
    return false
end
function W:soundSetName()
    local s=SoundSets.find(self.project.soundSet);return s and s.name or 'Custom'
end
function W:applySoundSet(id)
    if not SoundSets.find(id) then return false end
    local parts={}
    if self.song then for _,p in ipairs(MIDI.parts(self.song)) do parts[p.id]=p end end
    local changes={}
    for n,t in ipairs(self.project.tracks) do
        if t.kind=='midi' and not t.edited and not parts[t.part] then self.cb.message('Wait for the MIDI file to finish loading.');return false end
        local role=SoundSets.trackRole(t,parts[t.part]);local state,presetId=SoundSets.assignment(id,role,t.kind=='midi',parts[t.part] and parts[t.part].note or t.note)
        if not state then return false end
        changes[n]={state,presetId}
    end
    for n,t in ipairs(self.project.tracks) do t.params=changes[n][1];t.presetId=changes[n][2] end
    self.project.soundSet=id;self:configureAll();self:touch('soundset:'..self.generation);return true
end
function W:cycleSoundSet(delta)
    local at=0;for n,s in ipairs(SoundSets.entries) do if s.id==self.project.soundSet then at=n end end
    if at==0 and delta<0 then at=1 end
    return self:applySoundSet(SoundSets.entries[(at-1+delta)%#SoundSets.entries+1].id)
end
function W:cyclePreset(index,delta)
    local t=self.project.tracks[index];if not t or #self.catalog==0 then return end
    local at=0;for n,p in ipairs(self.catalog) do if p.id==t.presetId then at=n end end
    self:preset(index,self.catalog[(at-1+delta)%#self.catalog+1].id)
end
function W:presetName(t)
    for _,p in ipairs(self.catalog) do if p.id==t.presetId then return p.name end end
    return 'Custom sound'
end
function W:add(kind,presetId,name,note)
    if #self.project.tracks>=format.maxTracks then self.cb.message('Track limit: 16.');return false end
    self:stop(true)
    local t=format.newTrack(kind,name or (kind=='drum' and 'Drum ' or 'Piano ')..(#self.project.tracks+1),self.project.length)
    local n=#self.project.tracks+1;self.project.tracks[n]=t;self.selected=n
    if kind=='drum' then
        t.note=note or 36
        for _,sound in ipairs(self.catalog) do if sound.id==(presetId or 'b:kick') then
            t.params=schema.state(sound.state);t.params.polyphony=math.min(8,t.params.polyphony);t.presetId=sound.id;break
        end end
    end
    self:configure(n);self:touch();return true
end
function W:addKit(kitIndex)
    if #self.project.tracks>format.maxTracks-4 then self.cb.message('A kit needs four free tracks.');return false end
    self.editDepth=(self.editDepth or 0)+1
    local kit=schema.kits[kitIndex or 1] or schema.kits[1]
    self:add('drum',kit.ids[1],kit.name..' Kick',36)
    self:add('drum',kit.ids[2],kit.name..' Snare',38)
    self:add('drum',kit.ids[3],kit.name..' Closed hat',42)
    self:add('drum',kit.ids[4],kit.name..' Open hat',46)
    self.selected=#self.project.tracks-3
    self.editDepth=self.editDepth-1
    if self.editDepth==0 and self.transactionTouched then self.transactionTouched=false;self:touch() end
    return true
end
function W:remove(index)
    if not self.project.tracks[index] then return false end
    self:stop(true);table.remove(self.project.tracks,index);self.selected=math.max(1,math.min(index,#self.project.tracks))
    self:configureAll();self:touch();return true
end
function W:setTrack(index,name,value)
    local t=self.project.tracks[index];if not t then return false end
    if name=='mute' or name=='solo' then
        if type(value)~='boolean' then return false end;t[name]=value;self:configureAll()
    elseif name=='level' then
        if type(value)~='number' or value~=value or value<0 or value>1 then return false end
        t.level=value;self:configure(index)
    elseif name=='note' then
        if type(value)~='number' or value%1~=0 or value<0 or value>127 then return false end;t.note=value
    else return false end
    self:touch(name..':'..index);return true
end
function W:length(length)
    if type(length)~='number' or length<16 or length>format.maxSteps or length%16~=0 then return false end
    self:stop(true)
    -- The loop length never truncates composition data in later bars.
    self.project.length=length;self.bar=math.min(self.bar,length/16-1);self:touch();return true
end
function W:toggleStep(index,step)
    local t=self.project.tracks[index]
    if not t or t.kind~='drum' or step<0 or step>=self.project.length then return false end
    local digit=t.steps:sub(step+1,step+1)=='0' and tostring(math.max(1,math.floor(self.velocity*9+0.5))) or '0'
    t.steps=t.steps:sub(1,step)..digit..t.steps:sub(step+2);self:touch();return true
end
function W:toggleNote(index,note,step)
    local t=self.project.tracks[index]
    if not t or t.kind~='piano' or note<0 or note>127 or step<0 or step>=self.project.length then return false end
    for at,n in ipairs(t.notes) do if n.n==note and n.s<=step and n.s+n.l>step then table.remove(t.notes,at);self:touch();return true end end
    local count=0;for _,track in ipairs(self.project.tracks) do count=count+#track.notes end
    if #t.notes>=128 or count>=format.maxNotes then self.cb.message('Project note limit reached.');return false end
    t.notes[#t.notes+1]={n=note,s=step,l=math.min(self.noteLength,self.project.length),v=self.velocity}
    self:touch();return true
end
function W:needsMidi()
    for _,t in ipairs(self.project.tracks) do if t.kind=='midi' and not t.edited then return true end end
    return false
end
function W:hasSong() return self.song~=nil or self.project.timeline~=nil and not self:needsMidi() end
function W:noteData(index)
    local t=self.project.tracks[index];if not t then return {} end
    if t.edited then
        self.noteCache=self.noteCache or {};local cached=self.noteCache[index]
        if not cached or cached.data~=t.edited then cached={data=t.edited,notes=assert(format.decodeNotes(t.edited))};self.noteCache[index]=cached end
        return cached.notes
    end
    return t.kind=='midi' and self.spans and self.spans[t.part] or t.notes
end
function W:canEditNotes(index)
    local t=self.project.tracks[index];return t and (t.kind=='piano' or t.kind=='midi' and t.edited~=nil)
end
function W:convertPart(index)
    local t=self.project.tracks[index]
    if not t or t.kind~='midi' or t.edited then return false end
    if not self.song then self.cb.message('Load the original MIDI before editing this part.');return false end
    local timeline={beats=self.song.beats,tempos={}}
    for _,tempo in ipairs(self.song.tempos or {}) do timeline.tempos[#timeline.tempos+1]={beat=tempo.beat,tempo=tempo.tempo} end
    timeline=self.project.timeline or format.timeline(timeline)
    if not timeline then self.cb.message('This MIDI tempo map exceeds the editable song limits.');return false end
    local notes=MIDI.editableSpans(self.song,t.part);local data=format.encodeNotes(notes,self.song.division)
    local total=#notes;for _,track in ipairs(self.project.tracks) do if track.edited then total=total+#track.edited.bytes/10 end end
    if not data or total>format.maxEditedNotes then self.cb.message('Editing supports 4,096 notes per part and 8,192 per project. Original part preserved.');return false end
    self:stop(true);self.project.timeline=timeline;t.edited=data;self.noteCache={};self.noteSelection={};self.selectionTrack=index
    for _,part in ipairs(MIDI.parts(self.song)) do if part.id==t.part then t.sourceRole=SoundSets.role(part);t.note=part.note or t.note end end
    if notes[1] then self.bar=math.floor(notes[1].s/16);self.base=math.max(0,math.min(116,math.floor(notes[1].n/12)*12)) end
    self:touch();return true
end
function W:selectNote(index,id,additive)
    if self.selectionTrack~=index or not additive then self.noteSelection={} end
    self.selectionTrack=index;self.lastEditKey=nil
    if id then self.noteSelection[id]=not self.noteSelection[id] or nil end
end
function W:selectAllNotes(index)
    self:selectNote(index);for id in ipairs(self:noteData(index) or {}) do self.noteSelection[id]=true end
end
function W:selectedNotes(index)
    local ids={};if self.selectionTrack==index then for id in ipairs(self:noteData(index) or {}) do if self.noteSelection[id] then ids[#ids+1]=id end end end
    return ids
end
function W:storeNotes(index,notes,key,quiet)
    local t=self.project.tracks[index];if not self:canEditNotes(index) then return false end
    if t.edited then
        local data=format.encodeNotes(notes,t.edited.division);local total=#notes
        for n,track in ipairs(self.project.tracks) do if n~=index and track.edited then total=total+#track.edited.bytes/10 end end
        if not data or total>format.maxEditedNotes then self.cb.message('Edited note limit reached; change cancelled.');return false end
        for _,n in ipairs(notes) do if format.seconds(self.project.timeline,(n.s+n.l)/4)>1800 then self.cb.message('Song limit: 30 minutes.');return false end end
        if data.bytes==t.edited.bytes then return true end
        t.edited=data
    else
        local candidate=copy(t);candidate.notes=notes;local valid=format.track(candidate,self.project.length)
        local count=#notes;for n,track in ipairs(self.project.tracks) do if n~=index then count=count+#track.notes end end
        if not valid or count>format.maxNotes then self.cb.message('Clip note limit or range exceeded; change cancelled.');return false end
        if equal(t.notes,valid.notes) then return true end
        t.notes=valid.notes
    end
    self.noteCache={};self:touch(key or 'sequence',quiet);return true
end
function W:addNote(index,pitch,step)
    if not self:canEditNotes(index) then return false end
    local notes=copy(self:noteData(index));notes[#notes+1]={n=pitch,s=step,l=self.noteLength,v=self.velocity}
    if not self:storeNotes(index,notes) then return false end
    self:selectNote(index,#notes);return true
end
function W:editSelected(index,change,key,quiet)
    if not self:canEditNotes(index) then return false end
    local ids=self:selectedNotes(index);if #ids==0 then return false end
    local notes=copy(self:noteData(index))
    for _,id in ipairs(ids) do local n=notes[id]
        n.s=n.s+(change.step or 0);n.n=n.n+(change.pitch or 0);n.l=n.l+(change.length or 0)
        if change.velocity then n.v=change.velocity end
    end
    return self:storeNotes(index,notes,key,quiet)
end
function W:deleteNotes(index)
    if not self:canEditNotes(index) then return false end
    local notes={};for id,n in ipairs(self:noteData(index)) do if self.selectionTrack~=index or not self.noteSelection[id] then notes[#notes+1]=copy(n) end end
    if #notes==#self:noteData(index) then return false end
    if not self:storeNotes(index,notes) then return false end
    self.noteSelection={};return true
end
function W:copyNotes(index)
    local ids=self:selectedNotes(index);if #ids==0 then return false end
    local notes=self:noteData(index);local first=math.huge;self.clipboard={}
    for _,id in ipairs(ids) do first=math.min(first,notes[id].s) end
    for _,id in ipairs(ids) do local n=copy(notes[id]);n.s=n.s-first;self.clipboard[#self.clipboard+1]=n end
    return first
end
function W:pasteNotes(index,step)
    if not self:canEditNotes(index) or not self.clipboard then return false end
    local notes=copy(self:noteData(index));local first=#notes+1
    for _,source in ipairs(self.clipboard) do local n=copy(source);n.s=n.s+step;notes[#notes+1]=n end
    if not self:storeNotes(index,notes) then return false end
    self:selectNote(index);for id=first,#notes do self.noteSelection[id]=true end;return true
end
function W:duplicateNotes(index)
    local step=self:copyNotes(index);return step and self:pasteNotes(index,step+(self.snap>0 and self.snap or 0.25)) or false
end
function W:import(song,path)
    local parts=MIDI.parts(song);local extra,old={},{}
    for _,t in ipairs(self.project.tracks) do
        if t.kind~='midi' then extra[#extra+1]=t elseif self.project.midi==path then old[t.part]=t end
    end
    local found={};for _,p in ipairs(parts) do found[p.id]=true end
    for _,t in ipairs(self.project.tracks) do if t.edited and self.project.midi==path and not found[t.part] then extra[#extra+1]=t end end
    if #parts+#extra>format.maxTracks then self.cb.message('MIDI has '..#parts..' parts; at most 16 tracks fit. Remove clips or choose a smaller MIDI.');return false end
    self:stop(true);local tracks={}
    for _,p in ipairs(parts) do
        local t=old[p.id] or format.newTrack('midi',p.name:sub(1,48),self.project.length)
        if not old[p.id] then
            t.params,t.presetId=SoundSets.assignment(SoundSets.find(self.project.soundSet) and self.project.soundSet or 'balanced',SoundSets.role(p),true,p.note)
        end
        t.part=p.id;tracks[#tracks+1]=t
        if not t.edited then t.sourceRole=SoundSets.role(p);t.note=p.note or t.note end
    end
    for _,t in ipairs(extra) do tracks[#tracks+1]=t end
    if self.project.midi~=path and not SoundSets.find(self.project.soundSet) then self.project.soundSet='balanced' end
    if self.project.midi~=path then self.project.timeline=nil end
    self.song=song;self:cacheSpans();self.project.midi=path;self.project.tracks=tracks;self.selected=1;self.noteSelection={};self.noteCache={}
    if song.tempos and song.tempos[1] and song.tempos[1].beat==0 then self.project.bpm=math.max(40,math.min(240,60000000/song.tempos[1].tempo)) end
    self:configureAll();self:touch();return true
end
function W:cacheSpans() self.spans=self.song and MIDI.noteSpans(self.song) or {} end
function W:load(project)
    local valid=format.project(project);if not valid then return false end
    self:stop(true);if self.project.midi~=valid.midi then self.song=nil end
    self.project=valid;self.checkpoint=copy(valid);self.undoStack={};self.redoStack={};self.lastEditKey=nil;self.noteSelection={};self.noteCache={};self:cacheSpans();self.selected=1;self.bar=0;self.dirty=false;self.revision=self.revision+1;self.generation=self.generation+1;self:configureAll()
    if self:needsMidi() and not self.song then self.cb.importMidi(valid.midi) end
    return true
end
function W:save()
    self.lastEditKey=nil
    local p=format.project(self.project)
    if not p then self.cb.message('Project contains unsupported data; save cancelled.');return false end
    self.cb.save(p,self.generation);return true
end
function W:audition(index)
    local t=self.project.tracks[index];if not t or t.kind=='midi' then return false end
    local key=index*128+t.note;self.counts[key]=(self.counts[key] or 0)+1
    if self.cb.on(index,t.note,self.velocity)==false then return false end
    self.preview[#self.preview+1]={lane=index,note=t.note,finish=self.wall+t.gate}
    return true
end
function W:stop(force)
    if self.cb.stopSequence and (self.native or force) then self.cb.stopSequence() end
    if self.native then self.native=false;self.awaiting=false;self.sequenceDirty=false end
    local midi=self.midi;self.midi=nil
    local active=self.playing or #self.preview>0;self.playing=false;self.pending={};self.preview={};self.counts={}
    if midi and midi.playing then midi:stop() elseif active or force then self.cb.panic() end
end
function W:play(mode)
    self:stop(true)
    if mode=='song' and not self:hasSong() then self.cb.message('Load the original MIDI file for unedited parts first.');return false end
    if mode=='song' and not self.cb.schedule then
        for _,t in ipairs(self.project.tracks) do if t.edited then self.cb.message('Edited MIDI requires native Studio playback.');return false end end
    end
    self:configureAll();self.time=0;self.beat=0;self.step=-1;self.pending={};self.counts={};self.playing=true;self.mode=mode
    if self.cb.schedule then
        local Sequence=require(prefix..'shared.sequence')
        local ok,plan=pcall(Sequence.compile,self.project,self.song,MIDI,mode,format)
        if not ok then self.playing=false;self.cb.message(tostring(plan));return false end
        self.native=true;self.nativeStarted=false;self.awaiting=true;self.sequenceDirty=false
        if self.cb.schedule(plan,false)==false then self:stop(true);return false end
        return true
    end
    if mode=='song' then
        local routes={};for n,t in ipairs(self.project.tracks) do if t.kind=='midi' then routes[t.part]=n end end
        self.midi=MIDI.player(self.song,{
            on=function(note,velocity,_,part) local lane=routes[part];if lane then self.cb.on(lane,note,velocity) end end,
            off=function(note,_,part) local lane=routes[part];if lane then self.cb.off(lane,note) end end,
            panic=function() self.cb.panic() end,
            finished=function(reason) self.playing=false;self.pending={};self.preview={};self.counts={};if reason then self.cb.message(reason) end end,
        },{multitimbral=true,percussion=true})
    end
    return true
end
function W:beatAt(time)
    if self.mode~='song' then return time*self.project.bpm/60 end
    local beat,seconds,tempo=0,0,500000
    local timeline=self.project.timeline or self.song
    for _,t in ipairs(timeline.tempos or {}) do
        local at=self.project.timeline and seconds+(t.beat-beat)*tempo/1000000 or t.time
        if at>time then break end;beat,seconds,tempo=t.beat,at,t.tempo
    end
    return beat+(time-seconds)*1000000/tempo
end
function W:frame(dt)
    if self.native then
        self.wall=self.wall+math.max(0,type(dt)=='number' and dt==dt and dt or 0)
        local preview={}
        for _,n in ipairs(self.preview) do
            if n.finish<=self.wall then
                local key=n.lane*128+n.note;self.counts[key]=self.counts[key]-1
                if self.counts[key]==0 then self.cb.off(n.lane,n.note) end
            else preview[#preview+1]=n end
        end
        self.preview=preview
        if self.sequenceDirty and self.wall-(self.lastSequenceEdit or -10)>0.25 then
            self.sequenceDirty=false;self.lastSequenceEdit=self.wall
            local Sequence=require(prefix..'shared.sequence')
            local ok,plan=pcall(Sequence.compile,self.project,self.song,MIDI,self.mode,format)
            if not ok or self.cb.schedule(plan,true)==false then self:stop(true);self.cb.message(tostring(plan));return end
        end
        if not self.awaiting then
            local playing,time,fault,complete=self.cb.progress()
            if playing then self.nativeStarted=true end
            self.time=time;self.beat=self:beatAt(time);self.step=math.floor(self.beat*4)
            if not playing and (self.nativeStarted or complete or fault and fault~=0) then self.playing=false;self.native=false;if fault and fault~=0 then self.cb.message('Native transport stopped: active-note limit.');self.cb.stopSequence() end end
        end
        return
    end
    if type(dt)~='number' or dt~=dt or dt<0 or dt>0.25 then
        if self.playing or #self.preview>0 then self:stop();self.cb.message('Transport stopped after a frame stall.') end
        return
    end
    self.wall=self.wall+dt
    local preview={}
    for _,n in ipairs(self.preview) do
        if n.finish<=self.wall then
            local key=n.lane*128+n.note;self.counts[key]=self.counts[key]-1
            if self.counts[key]==0 then self.cb.off(n.lane,n.note) end
        else preview[#preview+1]=n end
    end
    self.preview=preview
    if not self.playing then return end
    self.time=self.time+dt
    if self.mode=='song' then self.beat=self:beatAt(self.time) else self.beat=self.beat+dt*self.project.bpm/60 end
    if self.midi then self.midi:update(dt);if not self.playing then return end end
    local pending={}
    for _,n in ipairs(self.pending) do if n.finish<=self.beat then
            local key=n.lane*128+n.note;self.counts[key]=self.counts[key]-1
            if self.counts[key]==0 then self.cb.off(n.lane,n.note) end
        else pending[#pending+1]=n end end
    self.pending=pending
    local dispatched=0
    while true do
        local nextStep=self.step+1
        local beat=nextStep/4+(nextStep%2==1 and self.project.swing/4 or 0)
        if beat>self.beat then break end
        if not self.project.loop and nextStep>=self.project.length then
            if self.mode=='song' then self.step=math.floor(self.beat*4);break end
            self:stop(true);return
        end
        self.step=nextStep;local step=nextStep%self.project.length
        for lane,t in ipairs(self.project.tracks) do
            local function trigger(note,velocity,length)
                dispatched=dispatched+1
                if dispatched>128 then self:stop();return end
                local key=lane*128+note;self.counts[key]=(self.counts[key] or 0)+1
                if self.cb.on(lane,note,velocity)==false then self:stop();return end
                self.pending[#self.pending+1]={lane=lane,note=note,finish=beat+length}
            end
            if t.kind=='drum' then
                local velocity=tonumber(t.steps:sub(step+1,step+1)) or 0
                if velocity>0 then trigger(t.note,velocity/9,t.gate*self.project.bpm/60) end
            elseif t.kind=='piano' then
                for _,n in ipairs(t.notes) do if n.s==step then trigger(n.n,n.v,n.l/4) end end
            end
            if not self.playing then return end
        end
    end
end
return W
