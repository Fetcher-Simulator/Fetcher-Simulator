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
function W:clampPages(reset)
    local drums=0;for _,t in ipairs(self.project.tracks) do if t.kind=='drum' then drums=drums+1 end end
    for _,key in ipairs{'songPage','mixerPage','arrangePage','bandPage','memberPage','drumPage'} do
        local count=key=='drumPage' and drums or #self.project.tracks
        self[key]=reset and 0 or math.max(0,math.min(self[key] or 0,math.ceil(count/8)-1))
    end
end
function W:restore(project)
    self:stop(true)
    self.clipEdit=nil;self.regionIndex=nil;self.regionSelection=nil
    if self.cb.documentChanged then self.cb.documentChanged() end
    if self.project.midi~=project.midi then self.song=nil end
    self.project=copy(project);self.checkpoint=copy(project);self.lastEditKey=nil;self:clampPages()
    self.selected=math.max(1,math.min(self.selected,#project.tracks));self.bar=math.min(self.bar,project.length/16-1)
    self.noteSelection={};self.noteCache={};self:cacheSpans();self:configureAll();self.dirty=true;self.revision=self.revision+1;self.generation=self.generation+1
    if self:needsMidi() and not self.song then self.cb.importMidi(project.midi) end
end
function W:newSong(bpm,numerator,denominator,bars)
    if self.recording or self.failedTake then self.cb.message('Keep or cancel the take before creating a song.');return false end
    local p=format.new();p.name='New song';p.bpm=bpm or 120
    p.composition={bars=bars or 8,numerator=numerator or 4,denominator=denominator or 4,loopStart=1,loopEnd=bars or 8,loop=false}
    local s=format.composition(p.composition);if not s or type(p.bpm)~='number' or p.bpm~=p.bpm or p.bpm<40 or p.bpm>240 then return false end
    p.timeline={beats=s.bars*s.numerator*4/s.denominator,tempos={{beat=0,tempo=math.floor(60000000/p.bpm+0.5)}}}
    -- Keep the physical roster while starting a fresh musical document.
    p.band=copy(self.project.band)
    p.tracks[1]=format.newTrack('midi','Piano 1',p.length);p.tracks[1].edited=format.encodeNotes({},960);p.tracks[1].sourceRole='keys'
    p.tracks[1].params,p.tracks[1].presetId=SoundSets.assignment('balanced','keys',true)
    p=format.project(p);if not p then return false end
    self:stop(true);if self.cb.documentChanged then self.cb.documentChanged() end
    if self.library then self.library:detach() end
    self.song=nil;self.spans={};self.project=p;self.noteCache={};self.noteSelection={};self.selected=1;self:clampPages(true)
    self.clipEdit=nil;self.regionIndex=nil;self.regionSelection=nil
    self.rollStart=0;self.bar=0;self.base=60;self.time=0;self.beat=0;self.step=-1;self.page='song'
    self.lastEditKey=nil;self:configureAll();self:touch();return true
end
function W:timingProject()
    local p=copy(self.project)
    if not p.timeline then
        if not self.song then return end
        local tempos={};for _,t in ipairs(self.song.tempos or {}) do tempos[#tempos+1]={beat=t.beat,tempo=t.tempo} end
        p.timeline={beats=self.song.beats,tempos=tempos,meters=copy(self.song.meters or {})}
    end
    return p
end
function W:commitTiming(candidate)
    if self.recording or self.failedTake then self.cb.message('Keep or cancel the take before editing timing.');return false end
    if candidate.composition then
        candidate.timeline.beats=format.barStart(candidate,candidate.composition.bars+1)
        candidate.composition.numerator,candidate.composition.denominator=format.meterAt(candidate,0)
    end
    candidate.bpm=math.max(40,math.min(240,60000000/format.tempoAt(candidate,0)))
    local valid=format.project(candidate)
    if not valid then self.cb.message('Timing map exceeds supported meter, event or 30-minute limits; previous map kept.');return false end
    self:stop(true);self.project=valid;self.noteCache={};self.lastEditKey=nil;self:touch();return true
end
function W:setComposition(changes)
    if not self.project.composition or self.recording or self.failedTake then return false end
    local p=copy(self.project);for k,value in pairs(changes) do p.composition[k]=value end
    if not format.composition(p.composition) then return false end
    if changes.numerator or changes.denominator then
        local meters=copy(p.timeline.meters or {})
        for i=#meters,1,-1 do if meters[i].beat==0 then table.remove(meters,i) end end
        table.insert(meters,1,{beat=0,numerator=p.composition.numerator,denominator=p.composition.denominator});p.timeline.meters=meters
    end
    return self:commitTiming(p)
end
function W:editTempo(beat,bpm,remove)
    local p=self:timingProject();if not p or type(beat)~='number' or beat~=beat or beat<0 or beat>format.maxSongBeats then return false end
    if remove and beat==0 or not remove and (type(bpm)~='number' or bpm~=bpm or bpm<40 or bpm>240) then return false end
    local found=false;for _,t in ipairs(p.timeline.tempos) do if math.abs(t.beat-beat)<1e-7 then found=true end end
    if remove and not found then return false end
    local points={};for _,t in ipairs(p.timeline.tempos) do if math.abs(t.beat-beat)>1e-7 then points[#points+1]=t end end
    if not remove then points[#points+1]={beat=beat,tempo=math.floor(60000000/bpm+0.5)} end
    table.sort(points,function(a,b) return a.beat<b.beat end)
    if not points[1] or points[1].beat>0 then table.insert(points,1,{beat=0,tempo=500000}) end
    p.timeline.tempos=points;return self:commitTiming(p)
end
function W:editMeter(beat,n,d,remove)
    local p=self:timingProject();if not p or type(beat)~='number' or beat~=beat or beat<0 or beat>format.maxSongBeats then return false end
    if remove and beat==0 or not remove and not format.validMeter(n,d) then return false end
    local found=false;for _,m in ipairs(p.timeline.meters or {}) do if math.abs(m.beat-beat)<1e-7 then found=true end end
    if remove and not found then return false end
    local points={};for _,m in ipairs(p.timeline.meters or {}) do if math.abs(m.beat-beat)>1e-7 then points[#points+1]=m end end
    if not remove then points[#points+1]={beat=beat,numerator=n,denominator=d} end
    table.sort(points,function(a,b) return a.beat<b.beat end);p.timeline.meters=points
    return self:commitTiming(p)
end
function W:setTempo(value)
    if self.playing or self.recording or type(value)~='number' or value~=value or value<40 or value>240 then return false end
    if self.project.timeline or self.song then return self:editTempo(0,value) end
    self.project.bpm=value;self:touch('bpm',true);return true
end
function W:addPart(role)
    if not self.project.timeline or #self.project.tracks>=format.maxTracks or self.recording then return false end
    local allowed={keys=true,bass=true,guitar=true,pad=true,kick=true,snare=true,hat=true,openhat=true,tom=true,strings=true,brass=true,reed=true,pipe=true,mallet=true,organ=true,lead=true,pluck=true,fx=true}
    if not allowed[role] then return false end
    self:stop(true);self.clipEdit=nil
    local index=#self.project.tracks+1;local t=format.newTrack('midi',role..' '..index,self.project.length)
    t.part='';t.sourceRole=role;t.instrument=format.instrumentFor(role);t.edited=format.encodeNotes({},960)
    t.note=({kick=36,snare=38,hat=42,openhat=46,tom=45})[role] or 60
    t.params,t.presetId=SoundSets.assignment(SoundSets.find(self.project.soundSet) and self.project.soundSet or 'balanced',role,true,t.note,t.instrument)
    self.project.tracks[index]=t;self.selected=index;self.noteSelection={};self.noteCache={}
    self.base=math.max(0,math.min(116,math.floor(t.note/12)*12));self:configureAll();self:touch();return true
end
function W:undo()
    self:endAutomationWrite()
    if self.recording then self.cb.message('End or cancel the take before undo.');return false end
    local project=table.remove(self.undoStack);if not project then return false end
    self.redoStack[#self.redoStack+1]=copy(self.project);self:restore(project);return true
end
function W:redo()
    self:endAutomationWrite()
    if self.recording then self.cb.message('End or cancel the take before redo.');return false end
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
function W:automationValue(index,name,step)
    local t=self.project.tracks[index];local spec=format.automationSpecs[name];if not t or not spec then return end
    local value=t.params[name] or spec.default;local points=t.automation and t.automation[name] or {}
    for i,p in ipairs(points) do
        if p.s>step then break end
        value=p.v;local next=points[i+1]
        if p.linear and next and step<next.s then return value+(next.v-value)*(step-p.s)/(next.s-p.s) end
    end
    return value
end
function W:setAutomation(index,name,step,value,linear,remove,from)
    if self.recording then self.cb.message('End the keyboard take before editing automation.');return false end
    local t=self.project.tracks[index];local spec=format.automationSpecs[name]
    if not t or not spec or type(step)~='number' or step~=step or step<0 then return false end
    local lanes=copy(t.automation or {});local points=lanes[name] or {};lanes[name]=points
    for i=#points,1,-1 do if math.abs(points[i].s-step)<1e-7 or from and math.abs(points[i].s-from)<1e-7 then table.remove(points,i) end end
    if not remove then points[#points+1]={s=step,v=value,linear=not spec.discrete and linear==true} end
    table.sort(points,function(a,b) return a.s<b.s end)
    if #points==0 then lanes[name]=nil end
    local candidate=copy(self.project);candidate.tracks[index].automation=lanes
    if not format.project(candidate) then self.cb.message('Automation value, time or point budget exceeded.');return false end
    t.automation=lanes;self:touch(nil,true);return true
end
function W:followPrograms(index,enabled)
    local t=self.project.tracks[index];if not t or t.params.sample or type(enabled)~='boolean' then return false end
    t.followPrograms=enabled;self:touch('sequence');return true
end
function W:importControls(index)
    local t=self.project.tracks[index];if not t or not self.song or t.kind~='midi' then return false end
    local part;for _,p in ipairs(MIDI.parts(self.song)) do if p.id==t.part then part=p end end
    if not part then return false end
    local ok,lanes=pcall(MIDI.automation,self.song,part);if not ok then self.cb.message(tostring(lanes));return false end
    -- Replace only MIDI performance lanes, preserving all synth-parameter curves.
    local automation=copy(t.automation or {})
    for _,name in ipairs{'pitchBend','modulation','expression','midiVolume','pedal','program'} do automation[name]=lanes[name] end
    local candidate=copy(self.project);candidate.tracks[index].automation=automation
    if not candidate.timeline then
        local tempos={};for _,tempo in ipairs(self.song.tempos or {}) do tempos[#tempos+1]={beat=tempo.beat,tempo=tempo.tempo} end
        candidate.timeline=format.timeline{beats=self.song.beats,tempos=tempos,meters=copy(self.song.meters or {})}
        if not candidate.timeline then self.cb.message('MIDI tempo map exceeds editable limits.');return false end
    end
    if not format.project(candidate) then self.cb.message('Imported controls exceed the project budget.');return false end
    self:stop(true);self.project.timeline=candidate.timeline;t.automation=automation;self:touch('sequence');return true
end
function W:endAutomationWrite()
    if not self.automationWriting then return end
    self.automationWriting=false;self.editDepth=math.max(0,(self.editDepth or 1)-1)
    if self.editDepth==0 and self.transactionTouched then self.transactionTouched=false;self:touch('sequence') end
end
function W:beginAutomationWrite()
    if not self.native or not self.playing or self.awaiting or self.recording or self.mode=='pattern' then return false end
    if not self.automationWriting then self.lastEditKey=nil;self.automationWriting=true;self.editDepth=(self.editDepth or 0)+1 end
    return true
end
function W:recordAutomation(index,name,value)
    if not self.native or not self.playing or self.awaiting then return false end
    local step=math.max(0,math.floor(self.beat*4*4+0.5)/4)
    if self.mode~='song' then step=step%self.project.length end
    return self:setAutomation(index,name,step,value,self.automationLinear~=false)
end
function W:configureBand()
    if self.cb.band then
        local routes={};for n,t in ipairs(self.project.tracks) do routes[n]=t.output.slot or 0 end
        self.cb.band(self.project.band,routes)
    end
end
function W:setupNpcBand(restage)
    if self.recording or self.failedTake then self.cb.message('Keep or cancel the take before setting up NPCs.');return false end
    if not self.cb.setupBand then return false end
    if self.bandSetup then self.cb.message('NPC stage setup is still running.');return false end
    if not restage and #self.project.tracks==0 then self.cb.message('Create or import song parts first.');return false end
    self:stop(true);self.bandSetupSerial=(self.bandSetupSerial or 0)+1
    self.bandSetup={requestId=self.bandSetupSerial,generation=self.generation,document=self.project,at=self.cb.now(),restage=restage==true}
    self.cb.setupBand(self.bandSetupSerial,restage==true,not restage and format.project(self.project) or nil)
    self.revision=self.revision+1;return true
end
function W:receiveBandSetup(data)
    local pending=self.bandSetup
    if not pending or data.requestId~=pending.requestId then return false end
    self.bandSetup=nil;self.revision=self.revision+1
    if data.error then self.cb.message(tostring(data.error));return false end
    if data.restage~=pending.restage then return false end
    if pending.restage then self.cb.message('NPC formation faces your captured leader position.');return true end
    if pending.generation~=self.generation or pending.document~=self.project then
        self.cb.message('The song changed during NPC setup. Hired performers remain yours; run Auto NPC band again.');return false
    end
    if type(data.targets)~='table' or #data.targets~=#self.project.tracks then return false end
    local candidate=copy(self.project);candidate.band=format.band();local seen={}
    for index,track in ipairs(candidate.tracks) do
        local target=format.target(data.targets[index])
        if not target or target.kind~='npc' or seen[target.mpNum] then return false end
        seen[target.mpNum]=true;candidate.band[index].name=track.name;candidate.band[index].target=target
        track.output={kind='keyboard',slot=index}
    end
    if not require(prefix..'client.arrangement').commitProject(self,candidate) then return false end
    self.memberPage=0;self.bandPage=0;self:configureAll()
    self.cb.message('NPC band ready: '..#candidate.tracks..' parts. Routing supports Undo; hired NPCs remain in your roster.')
    return true
end
function W:setOutput(index,slot)
    local t=self.project.tracks[index]
    if not t or type(slot)~='number' or slot%1~=0 or slot<0 or slot>format.maxMembers then return false end
    self:stop(true);t.output={kind='keyboard',slot=slot};self:configureBand();self:touch('output:'..index);return true
end
function W:setStageInstrument(index,instrument)
    if not self.project.tracks[index] or not format.instruments[instrument] then return false end
    local p=copy(self.project);local t=p.tracks[index];t.instrument=instrument
    local member=p.band[t.output.slot or 0]
    if member and member.target and (member.target.kind=='npc' or member.target.kind=='player') then member.target.instrument=instrument end
    if not require(prefix..'client.arrangement').commitProject(self,p) then return false end
    self:stop(true);self:configureAll();return true
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
function W:setSample(index,reference)
    local t=self.project.tracks[index];reference=schema.sample(reference)
    if not t or not reference then return false end
    if self.cb.sampleStatus then
        local info=self.cb.sampleStatus(reference)
        if info.status~='Ready' then self.cb.message(info.error or info.status);return false end
    end
    local params=t.params.sample and schema.state(t.params) or schema.state{attack=0.002,sustain=1,
        release=0.08,cutoff=18000,resonance=0,polyphony=8}
    params.sample=reference;t.followPrograms=false;t.params=params;t.presetId='';self.project.soundSet=''
    if t.kind=='drum' then
        if t.note~=reference.root and self.native and self.playing then self.sequenceDirty=true end
        t.note=reference.root
    end
    self:configure(index);self:touch('sound:sample:'..index);return true
end
function W:prepareSamples(index)
    if not self.cb.sampleStatus then return true end
    for n,t in ipairs(self.project.tracks) do if (not index or index==n) and t.params.sample then
        local info=self.cb.sampleStatus(t.params.sample)
        if info.status=='Loading' then
            self.cb.message('Part '..n..': '..info.status..'. '..(info.error or 'Wait for the sample, then Play again.'));return false
        end
    end end
    return true
end
function W:preset(index,id)
    local t=self.project.tracks[index];if not t then return false end
    for _,p in ipairs(self.catalog) do
        if p.id==id then
            local params=schema.state(p.state);if not params then return false end
            params.polyphony=math.min(8,params.polyphony)
            local wasSample=t.params.sample~=nil
            t.params=params;t.presetId=id;self.project.soundSet=''
            if t.kind=='drum' and params.sample then
                if t.note~=params.sample.root and self.native and self.playing then self.sequenceDirty=true end
                t.note=params.sample.root
            elseif t.kind=='drum' and wasSample and params.drumType>0 then
                local note=({36,38,42,46})[params.drumType]
                if t.note~=note and self.native and self.playing then self.sequenceDirty=true end
                t.note=note
            end
            self:configure(index);self:touch();return true
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
        local role=SoundSets.trackRole(t,parts[t.part]);local state,presetId=SoundSets.assignment(id,role,t.kind=='midi',parts[t.part] and parts[t.part].note or t.note,t.instrument)
        if not state then return false end
        changes[n]={state,presetId}
    end
    for n,t in ipairs(self.project.tracks) do t.params=changes[n][1];t.presetId=changes[n][2] end
    self.project.soundSet=id;self:configureAll();self:touch('soundset:'..self.generation)
    if self.native and self.playing then for _,t in ipairs(self.project.tracks) do if t.followPrograms then self.sequenceDirty=true end end end
    return true
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
    if t.params.sample then return 'Sample: '..(t.params.sample.path:match('([^/]+)$') or '') end
    return 'Custom sound'
end
function W:add(kind,presetId,name,note)
    if #self.project.tracks>=format.maxTracks then self.cb.message('Track limit: '..format.maxTracks..'.');return false end
    self:stop(true);self.clipEdit=nil
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
    if self.recording or self.failedTake then self.cb.message('Keep, retry or cancel the take before deleting a track.');return false end
    self:stop(true);self.clipEdit=nil;self.regionIndex=nil;self.regionSelection=nil;table.remove(self.project.tracks,index);self.selected=math.max(1,math.min(index,#self.project.tracks))
    self:clampPages();self:configureAll();self:touch();return true
end
function W:duplicateTrack(index) return require(prefix..'client.pattern-tools').duplicate(self,index) end
function W:clearDrumBar() return require(prefix..'client.pattern-tools').clearBar(self) end
function W:deletePatternBar() return require(prefix..'client.pattern-tools').deleteBar(self) end
function W:capturePattern() return require(prefix..'client.pattern-tools').capture(self) end
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
function W:renameTrack(index,name)
    local t=self.project.tracks[index]
    if not t or type(name)~='string' or #name>48 or name:find('%c') or not name:find('%S') then return false end
    if t.name==name then return true end
    t.name=name;self:touch('trackName:'..index);return true
end
function W:exportMidi()
    if not self.cb.exportMidi then self.cb.message('This client does not support MIDI export.');return false end
    local exporter=require(prefix..'client.midi-export')
    local ok,bytes=pcall(exporter.encode,self.project,self.song,MIDI,format)
    if not ok then self.cb.message('MIDI export: '..tostring(bytes));return false end
    local written,path=pcall(self.cb.exportMidi,self.project.name,bytes)
    if not written then self.cb.message('MIDI export: '..tostring(path));return false end
    self.cb.message('MIDI exported: '..path);return true
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
function W:duplicateDrumBar()
    local first=self.bar*16;local target=first+16
    if self.bar<0 or self.bar%1~=0 or first>=self.project.length then return false end
    if target>=format.maxSteps then self.cb.message('The drum pattern is already at its 16-bar limit.');return false end
    local found=false;for _,t in ipairs(self.project.tracks) do if t.kind=='drum' then found=true;break end end
    if not found then self.cb.message('Add a drum lane before duplicating a bar.');return false end
    self:stop(true)
    for _,t in ipairs(self.project.tracks) do if t.kind=='drum' then
        t.steps=t.steps:sub(1,target)..t.steps:sub(first+1,first+16)..t.steps:sub(target+17)
    end end
    self.project.length=math.max(self.project.length,target+16);self.bar=self.bar+1
    self:touch();return true
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
function W:clipSource(index)
    if not self.clipEdit then return end
    return require(prefix..'client.arrangement').source(self,index or self.selected)
end
function W:newClip(index) return require(prefix..'client.arrangement').empty(self,index) end
function W:captureClip(index) return require(prefix..'client.arrangement').capture(self,index) end
function W:editClip(index,id) return require(prefix..'client.arrangement').edit(self,index,id) end
function W:leaveClip() return require(prefix..'client.arrangement').leave(self) end
function W:placeClip(index,id,step) return require(prefix..'client.arrangement').place(self,index,id,step) end
function W:changeRegion(index,id,changes) return require(prefix..'client.arrangement').change(self,index,id,changes) end
function W:duplicateRegion(index,id) return require(prefix..'client.arrangement').duplicate(self,index,id) end
function W:removeRegion(index,id) return require(prefix..'client.arrangement').remove(self,index,id) end
function W:setClip(index,id,changes) return require(prefix..'client.arrangement').clip(self,index,id,changes) end
function W:deleteClip(index,id) return require(prefix..'client.arrangement').deleteClip(self,index,id) end
function W:selectRegion(index,id,toggle) return require(prefix..'client.arrangement').select(self,index,id,toggle) end
function W:selectedRegions() return require(prefix..'client.arrangement').selection(self) end
function W:selectAllRegions() return require(prefix..'client.arrangement').selectAll(self) end
function W:batchRegions(action,value) return require(prefix..'client.arrangement').batch(self,action,value) end
function W:moveRegions(bars) return require(prefix..'client.arrangement').moveBars(self,bars) end
function W:copyClip(index,id,target) return require(prefix..'client.arrangement').copyTo(self,index,id,target) end
function W:noteData(index)
    local t=self.project.tracks[index];if not t then return {} end
    if t.edited then
        local clip=self:clipSource(index);local data=clip and clip.notes or t.edited
        self.noteCache=self.noteCache or {};local cached=self.noteCache[index]
        if not cached or cached.data~=data then cached={data=data,notes=assert(format.decodeNotes(data))};self.noteCache[index]=cached end
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
    local timeline={beats=self.song.beats,tempos={},meters=copy(self.song.meters or {})}
    for _,tempo in ipairs(self.song.tempos or {}) do timeline.tempos[#timeline.tempos+1]={beat=tempo.beat,tempo=tempo.tempo} end
    timeline=self.project.timeline or format.timeline(timeline)
    if not timeline then self.cb.message('This MIDI tempo map exceeds the editable song limits.');return false end
    local notes=t.automation and t.automation.pedal and MIDI.editableSpans(self.song,t.part,true) or MIDI.editableSpans(self.song,t.part);local data=format.encodeNotes(notes,self.song.division)
    local total=#notes+format.savedNotes(self.project)
    if not data or total>format.maxEditedNotes then self.cb.message('Editing supports 4,096 notes per part and 8,192 per project. Original part preserved.');return false end
    self:stop(true);self.project.timeline=timeline;t.edited=data;self.noteCache={};self.noteSelection={};self.selectionTrack=index
    for _,part in ipairs(MIDI.parts(self.song)) do if part.id==t.part then
        t.sourceRole=SoundSets.role(part);t.note=part.note or t.note;t.instrument=t.instrument or format.instrumentFor(t.sourceRole,part.program)
    end end
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
    if self.recording then self.cb.message('End or cancel the take before editing notes.');return false end
    local t=self.project.tracks[index];if not self:canEditNotes(index) then return false end
    if t.edited then
        local clip=self:clipSource(index);local old=clip and clip.notes or t.edited
        local data=format.encodeNotes(notes,old.division);local total=format.savedNotes(self.project)-#old.bytes/10+#notes
        if not data or total>format.maxEditedNotes then self.cb.message('Edited note limit reached; change cancelled.');return false end
        for _,n in ipairs(notes) do if format.seconds(self.project.timeline,(n.s+n.l)/4)>1800 then self.cb.message('Song limit: 30 minutes.');return false end end
        if data.bytes==old.bytes then return true end
        if clip or t.arrangement then
            local candidate=copy(self.project)
            if clip then candidate.tracks[index].arrangement.clips[self.clipEdit.clip].notes=data else candidate.tracks[index].edited=data end
            if not format.project(candidate) then self.cb.message('Notes exceed clip length, transposed pitch or expanded-score limits; edit cancelled.');return false end
        end
        if clip then clip.notes=data else t.edited=data end
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
    if #parts+#extra>format.maxTracks then self.cb.message('MIDI has '..#parts..' parts; at most '..format.maxTracks..' tracks fit. Remove clips or choose a smaller MIDI.');return false end
    local timeline=self.project.midi==path and self.project.timeline or nil
    if not timeline and song.meters and #song.meters>0 then
        local tempos={};for _,t in ipairs(song.tempos or {}) do tempos[#tempos+1]={beat=t.beat,tempo=t.tempo} end
        timeline=format.timeline{beats=song.beats,tempos=tempos,meters=copy(song.meters)}
        if not timeline then self.cb.message('MIDI timing map exceeds supported event or 30-minute limits; previous song kept.');return false end
    end
    if self.library and self.project.midi~=path then self.library:detach() end
    self:stop(true);local tracks={}
    for _,p in ipairs(parts) do
        local t=old[p.id] or format.newTrack('midi',p.name:sub(1,48),self.project.length)
        if not old[p.id] then
            t.instrument=format.instrumentFor(SoundSets.role(p),p.program)
            t.params,t.presetId=SoundSets.assignment(SoundSets.find(self.project.soundSet) and self.project.soundSet or 'balanced',SoundSets.role(p),true,p.note,t.instrument)
        end
        t.part=p.id;tracks[#tracks+1]=t
        if not t.edited then
            t.sourceRole=SoundSets.role(p);t.note=p.note or t.note
            t.instrument=t.instrument or format.instrumentFor(t.sourceRole,p.program)
        end
    end
    for _,t in ipairs(extra) do tracks[#tracks+1]=t end
    if self.project.midi~=path and not SoundSets.find(self.project.soundSet) then self.project.soundSet='balanced' end
    if self.project.midi~=path then self.project.timeline=nil;self.project.composition=nil end
    self.song=song
    self.project.timeline=timeline
    self:cacheSpans();self.project.midi=path;self.project.tracks=tracks;self.selected=1;self.noteSelection={};self.noteCache={}
    self.clipEdit=nil;self.regionIndex=nil;self.regionSelection=nil
    if song.tempos and song.tempos[1] and song.tempos[1].beat==0 then self.project.bpm=math.max(40,math.min(240,60000000/song.tempos[1].tempo)) end
    self:configureAll();self:touch();return true
end
function W:cacheSpans() self.spans=self.song and MIDI.noteSpans(self.song) or {} end
function W:load(project)
    local valid=format.project(project);if not valid then return false end
    self:endTake(true);self:stop(true);if self.cb.documentChanged then self.cb.documentChanged() end;if self.project.midi~=valid.midi or valid.composition then self.song=nil end
    self.clipEdit=nil;self.regionIndex=nil;self.regionSelection=nil
    if self.library then self.library:detach() end
    self.project=valid;self.checkpoint=copy(valid);self.undoStack={};self.redoStack={};self.lastEditKey=nil;self.noteSelection={};self.noteCache={};self:cacheSpans();self.selected=1;self.bar=0;self.rollStart=0;self:clampPages(true);self.dirty=false;self.revision=self.revision+1;self.generation=self.generation+1;self:configureAll()
    if self:needsMidi() and not self.song then self.cb.importMidi(valid.midi) end
    return true
end
function W:save()
    self:endAutomationWrite()
    if self.recording then self.cb.message('Keep or cancel the take before saving.');return false end
    self.lastEditKey=nil
    local p=format.project(self.project)
    if not p then self.cb.message('Project contains unsupported data; save cancelled.');return false end
    return self.cb.save(p,self.generation)~=false
end
function W:audition(index)
    local t=self.project.tracks[index];if not t or t.kind=='midi' then return false end
    local key=index*128+t.note;self.counts[key]=(self.counts[key] or 0)+1
    if self.cb.on(index,t.note,self.velocity)==false then return false end
    self.preview[#self.preview+1]={lane=index,note=t.note,finish=self.wall+t.gate}
    return true
end
function W:stop(force)
    self:endAutomationWrite()
    self:endTake(false)
    if self.cb.stopSequence and (self.native or force) then self.cb.stopSequence() end
    if self.native then self.native=false;self.awaiting=false;self.sequenceDirty=false end
    local midi=self.midi;self.midi=nil
    local active=self.playing or #self.preview>0;self.playing=false;self.pending={};self.preview={};self.counts={}
    if midi and midi.playing then midi:stop() elseif active or force then self.cb.panic() end
end
function W:play(mode)
    if self.bandSetup then self.cb.message('Wait for NPC stage setup before playing.');return false end
    self:stop(true)
    if not self:prepareSamples() then return false end
    if mode=='song' and not self:hasSong() then self.cb.message('Load the original MIDI file for unedited parts first.');return false end
    if mode=='song' and not self.cb.schedule then
        for _,t in ipairs(self.project.tracks) do if t.edited then self.cb.message('Edited MIDI requires native Studio playback.');return false end end
    end
    self:configureAll();self.time=0;self.beat=0;self.step=-1;self.pending={};self.counts={};self.playing=true;self.mode=mode
    if self.cb.schedule then
        local Sequence=require(prefix..'shared.sequence')
        local ok,plan=pcall(Sequence.compile,self.project,self.song,MIDI,mode,format,SoundSets.programPatch)
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
function W:compilePlan()
    local Sequence=require(prefix..'shared.sequence')
    if self.mode~='pattern' then return Sequence.compile(self.project,self.song,MIDI,self.mode,format,SoundSets.programPatch) end
    local clip=assert(self:clipSource(),'Select a clip source first.')
    local p=copy(self.project);p.composition=nil;p.timeline=nil;p.length=clip.length;p.swing=0;p.loop=true
    for _,t in ipairs(p.tracks) do t.kind='midi';t.edited=nil;t.arrangement=nil;t.automation=nil;t.followPrograms=false end
    p.tracks[self.selected].kind='piano';p.tracks[self.selected].notes=format.decodeNotes(clip.notes)
    return Sequence.compile(p,nil,MIDI,'clips',format,SoundSets.programPatch)
end
function W:playClip()
    if not self:clipSource() or not self.cb.schedule then return false end
    if not self:prepareSamples(self.selected) then return false end
    self:stop(true);self:configureAll();self.mode='pattern';self.time=0;self.beat=0;self.step=-1
    local ok,plan=pcall(self.compilePlan,self)
    if not ok then self.cb.message(tostring(plan));return false end
    self.playing=true;self.native=true;self.nativeStarted=false;self.awaiting=true;self.sequenceDirty=false
    if self.cb.schedule(plan,false)==false then self:stop(true);return false end
    return true
end
function W:beatAt(time,mode)
    if (mode or self.mode)~='song' then return time*self.project.bpm/60 end
    local beat,seconds,tempo=0,0,500000
    local timeline=self.project.timeline or self.song
    for _,t in ipairs(timeline.tempos or {}) do
        local at=self.project.timeline and seconds+(t.beat-beat)*tempo/1000000 or t.time
        if at>time then break end;beat,seconds,tempo=t.beat,at,t.tempo
    end
    return beat+(time-seconds)*1000000/tempo
end
function W:playbackBeat(time)
    local first,last=format.songRange(self.project)
    if self.mode~='song' or not first then return self:beatAt(time) end
    local offset=format.seconds(self.project.timeline,first)
    if self.project.composition.loop then time=time%(format.seconds(self.project.timeline,last)-offset) end
    return self:beatAt(time+offset,'song')
end
function W:recordClock(age)
    local r=self.recording;if not r then return end
    age=type(age)=='number' and math.max(0,math.min(2,age)) or 0
    local latency=r.options and r.options.latency/1000 or 0
    if r.overdub then
        local playing,time=self.cb.progress()
        if not playing or self.awaiting then return nil end
        return self:playbackBeat(math.max(0,time-age-latency))*4
    end
    local now=self.cb.now and self.cb.now() or self.wall
    if now-age<r.starts then return nil end
    local elapsed;if r.clock then elapsed=r.clock() else elapsed=now-r.starts end
    if elapsed==nil or elapsed-age<0 then return nil end
    return self:beatAt(r.seconds+math.max(0,elapsed-age-latency),r.mode)*4
end
function W:startTake(index)
    self:endAutomationWrite()
    if self.failedTake then self.cb.message('Retry or cancel the recovered take before recording again.');return false end
    if self.playing and (not self.native or not self.cb.progress) then self.cb.message('Native playback is required for overdub.');return false end
    if self.recording or not self:canEditNotes(index) then self.cb.message('Choose a piano clip or convert the MIDI part with Edit MIDI first.');return false end
    local t=self.project.tracks[index]
    local clip=self:clipSource(index)
    if clip and self.playing then self.cb.message('Stop playback before recording into a shared clip source.');return false end
    if t.edited and self.playing and self.mode~='song' then self.cb.message('Play song, or stop playback before recording this MIDI part.');return false end
    self:configure(index)
    if self.cb.canMonitor and not self.cb.canMonitor(index) then self.cb.message('The assigned instrument must be Ready and loaded before recording.');return false end
    local capacity=t.edited and format.maxPartNotes-#self:noteData(index) or 128-#t.notes
    local remaining=t.edited and format.maxEditedNotes or format.maxNotes
    if t.edited then remaining=remaining-format.savedNotes(self.project)
    else for _,track in ipairs(self.project.tracks) do remaining=remaining-#track.notes end end
    capacity=math.min(capacity,remaining)
    if capacity<=0 then self.cb.message('No free note capacity for this take.');return false end
    local now=self.cb.now and self.cb.now() or self.wall
    local mode=self.playing and self.mode or (t.edited and 'song' or 'clips')
    if clip then mode='clips' end
    local seconds=self.playing and self.time or (t.edited and format.seconds(self.project.timeline,(self.rollStart or self.bar*16)/4) or (self.rollStart or self.bar*16)/4*60/self.project.bpm)
    if clip then seconds=(self.rollStart or 0)/4*60/self.project.bpm end
    local step=self:beatAt(seconds,mode)*4
    if self.playing and self.cb.progress then local _,time=self.cb.progress();step=self:playbackBeat(time)*4 end
    local origin=t.edited and 0 or (self.playing and math.floor(step/self.project.length)*self.project.length or 0)
    local limit=t.edited and math.min(format.maxSongBeats*4,self:beatAt(1800,'song')*4) or origin+self.project.length
    local minimum=0
    if t.edited and self.project.composition then
        local first,last=format.songRange(self.project);limit=last*4;minimum=self.playing and first*4 or 0
        if not self.playing then limit=self.project.timeline.beats*4 end
    end
    if clip then limit=clip.length;minimum=0 end
    if step>=limit then self.cb.message('Move the view inside the playback length before recording.');return false end
    local snap=math.min(self.snap>0 and self.snap or (t.edited and 4/t.edited.division or 1/240),limit-origin-minimum)
    local countBeats=t.edited and not clip and format.barBeats(self.project,step/4) or 4
    local countTempo=t.edited and not clip and format.tempoAt(self.project,step/4)/1000000 or 60/self.project.bpm
    local options=self.cb.recordOptions and self.cb.recordOptions() or {countIn=1,latency=0,metronome=false}
    local _,denominator=4,4
    if t.edited and not clip then _,denominator=format.meterAt(self.project,step/4) end
    self.recording={index=index,track=t,clip=clip,mode=mode,overdub=self.playing,origin=origin,limit=limit,notes={},held={},capacity=capacity,
        starts=now+countBeats*countTempo*options.countIn,countTempo=countTempo,countBeats=countBeats,countDenominator=denominator,
        countDuration=countBeats*countTempo*options.countIn,startStep=step,options=options,seconds=seconds,snap=snap,minimum=minimum}
    if self.cb.recordStart and self.cb.recordStart(self.recording)==false then self.recording=nil;return false end
    self.revision=self.revision+1;return true
end
function W:recordOn(note,velocity,age)
    local r=self.recording;local step=self:recordClock(age)
    if r and step and r.overdub and self.project.composition and self.project.composition.loop and r.last and step<r.last then self:endTake(false,r.limit);return false end
    if not r or not step or step>=r.limit or type(note)~='number' or note%1~=0 or note<0 or note>127 or r.held[note] then return false end
    velocity=velocity or self.velocity
    if type(velocity)~='number' or velocity~=velocity or velocity<0.01 or velocity>1 then return false end
    local count=#r.notes;for _ in pairs(r.held) do count=count+1 end
    if count>=r.capacity then self.cb.message('Take note limit reached; keep or cancel this take.');return false end
    if self.cb.on(r.index,note,velocity)==false then return false end
    if self.recording~=r then return false end
    r.held[note]={n=note,raw=step,v=velocity};r.last=step;return true
end
function W:recordOff(note,step,age)
    local r=self.recording;local held=r and r.held[note];if not held then return false end
    step=math.min(step or self:recordClock(age) or r.last or held.raw,r.limit)
    if r.overdub and self.project.composition and self.project.composition.loop and step<held.raw then step=r.limit end
    self.cb.off(r.index,note);r.held[note]=nil
    local s=math.floor((held.raw-r.origin)/r.snap+0.5)*r.snap
    local finish=math.floor((step-r.origin)/r.snap+0.5)*r.snap
    local limit=r.limit-r.origin
    s=math.max(r.minimum or 0,math.min(limit-r.snap,s));finish=math.max(s+r.snap,math.min(limit,finish))
    r.notes[#r.notes+1]={n=note,s=s,l=finish-s,v=held.v};r.last=step;return true
end
function W:endTake(cancel,finish)
    local r=self.recording
    if not r then if cancel then self.failedTake=nil;self.revision=self.revision+1 end;return false end
    if r.ending then return false end;r.ending=true
    local step=finish or self:recordClock() or r.last
    if step and r.overdub and self.project.composition and self.project.composition.loop and r.last and step<r.last then step=r.limit end
    local held={};for note in pairs(r.held) do held[#held+1]=note end
    for _,note in ipairs(held) do self:recordOff(note,step) end
    self.recording=nil;if self.cb.recordEnd then self.cb.recordEnd(r) end;self.revision=self.revision+1
    if cancel or #r.notes==0 then return true end
    if self.project.tracks[r.index]~=r.track or self:clipSource(r.index)~=r.clip then self.cb.message('Recording destination changed; take discarded.');return false end
    local notes=copy(self:noteData(r.index));local first=#notes+1
    for _,n in ipairs(r.notes) do notes[#notes+1]=n end
    if not self:storeNotes(r.index,notes) then self.failedTake={index=r.index,track=r.track,clip=r.clip,notes=copy(r.notes)};self.cb.message('Take not committed. Free note capacity, then Retry take; Cancel discards recovery.');return false end
    self:selectNote(r.index);for id=first,#notes do self.noteSelection[id]=true end
    return true
end
function W:recoverTake()
    local r=self.failedTake;if not r or self.recording or self.project.tracks[r.index]~=r.track or self:clipSource(r.index)~=r.clip or not self:canEditNotes(r.index) then return false end
    local notes=copy(self:noteData(r.index));for _,n in ipairs(r.notes) do notes[#notes+1]=copy(n) end
    if not self:storeNotes(r.index,notes) then return false end
    self.failedTake=nil;return true
end
function W:recordFrame()
    local r=self.recording;if not r then return end
    if r.overdub and not self.playing and not self.awaiting then self:endTake(false);return end
    if self.cb.canMonitor and not self.cb.canMonitor(r.index) then self:endTake(false);self.cb.message('Recording stopped because its instrument became unavailable.');return end
    local step=self:recordClock()
    if step and r.overdub and self.project.composition and self.project.composition.loop and r.last and step<r.last then self:endTake(false,r.limit);return end
    if step then r.started=true;r.last=step;if step>=r.limit then self:endTake(false) end
    elseif (r.overdub or r.clock) and r.started and not self.awaiting then self:endTake(false,r.limit) end
end
function W:recordStatus()
    local r=self.recording;if not r then return 'No active take' end
    local step=self:recordClock()
    if not step then
        if r.overdub then return 'Waiting for native playback...' end
        local now=self.cb.now and self.cb.now() or self.wall
        return 'Count in: '..math.max(1,math.ceil((r.starts-now)/(r.countTempo*4/(r.countDenominator or 4))))
    end
    local held=0;for _ in pairs(r.held) do held=held+1 end
    return string.format('RECORD  |  Beat %.2f  |  %d notes + %d held',step/4+1,#r.notes,held)
end
function W:frame(dt)
    if self.bandSetup and self.cb.now()-self.bandSetup.at>50 then
        self.bandSetup=nil;self.revision=self.revision+1;self.cb.message('NPC setup reply timed out. Refresh the roster before retrying.')
    end
    self:recordFrame()
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
        if self.sequenceDirty and not self.automationWriting and self.wall-(self.lastSequenceEdit or -10)>0.25 then
            self.sequenceDirty=false;self.lastSequenceEdit=self.wall
            local ok,plan=pcall(self.compilePlan,self)
            if not ok or self.cb.schedule(plan,true)==false then self:stop(true);self.cb.message(tostring(plan));return end
        end
        if not self.awaiting then
            local playing,time,fault,complete=self.cb.progress()
            if playing then self.nativeStarted=true end
            self.time=time;self.beat=self:playbackBeat(time);self.step=math.floor(self.beat*4)
            if not playing and (self.nativeStarted or complete or fault and fault~=0) then self:endAutomationWrite();self.playing=false;self.native=false;if fault and fault~=0 then self.cb.message('Native transport stopped: active-note limit.');self.cb.stopSequence() end end
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
