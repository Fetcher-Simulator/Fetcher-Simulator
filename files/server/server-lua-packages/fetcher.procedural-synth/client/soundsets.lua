-- Fixed per-part MIDI arrangements, not a General MIDI sample bank.
local schema=require('scripts.multiplayer.fetcher.procedural-synth.shared.schema')
local M={}
local function set(id,name,kit,keys,bass,guitar,strings,lead,pad,mallet,brass,reed,organ)
    return {id=id,name=name,kit=kit,patches={keys=keys,bass=bass,guitar=guitar,strings=strings,lead=lead,pad=pad,
        mallet=mallet,brass=brass,reed=reed,pipe=id=='chiptune' and 'chip-lead' or (id=='electronic' and 'trance-pluck' or 'flute'),organ=organ,pluck=guitar,fx=pad,tom='studio-tom'}}
end
M.entries={
    set('balanced','Balanced',1,'studio-keys','jazz-bass','guitar-clean','studio-strings','lead','pad','glass-bell','studio-brass','studio-reed','studio-organ'),
    set('rock','Rock',2,'studio-organ','rock-bass','guitar-crunch','studio-strings','guitar-lead','synthwave-pad','studio-keys','synth-brass','studio-reed','studio-organ'),
    set('metal','Metal',2,'studio-keys','rock-bass','guitar-metal','studio-strings','guitar-lead','dark-drone','glass-bell','synth-brass','guitar-crunch','studio-organ'),
    set('electronic','Electronic',3,'trance-pluck','house-bass','trance-pluck','synthwave-pad','trance-lead','synthwave-pad','glass-bell','synth-brass','lead','studio-organ'),
    set('lofi','Lo-fi / Jazz',4,'lofi-keys','jazz-bass','guitar-warm','studio-strings','studio-reed','pad','glass-bell','studio-brass','studio-reed','studio-organ'),
    set('chiptune','Chiptune',5,'chip-lead','chip-bass','chip-lead','chip-lead','chip-lead','pad','glass-bell','chip-lead','chip-lead','chip-lead'),
    set('ambient','Ambient',1,'glass-bell','house-bass','guitar-clean','ambient-pad','flute','ambient-pad','glass-bell','studio-brass','flute','studio-organ'),
    set('orchestra','Orchestra',1,'studio-keys','orchestra-cello','guitar-clean','orchestra-violin','orchestra-trumpet','orchestra-violin','glass-bell','orchestra-trumpet','orchestra-clarinet','studio-organ'),
}
M.entries[#M.entries].patches.pipe='orchestra-flute'
for _,s in ipairs{
    set('classic-rock','Classic Heavy Rock',2,'studio-organ','bass-picked','guitar-heavy','studio-strings','guitar-lead','synthwave-pad','studio-keys','studio-brass','studio-reed','studio-organ'),
    set('thrash','Thrash Metal',7,'studio-keys','bass-picked','guitar-thrash','studio-strings','guitar-lead','dark-drone','studio-keys','studio-brass','studio-reed','studio-organ'),
    set('alternative-metal','Alternative Metal',7,'studio-keys','bass-picked','guitar-altmetal','studio-strings','guitar-lead','dark-drone','studio-keys','studio-brass','studio-reed','studio-organ'),
    set('funk','Funk',8,'studio-organ','bass-finger','guitar-funk','studio-strings','guitar-clean','synthwave-pad','studio-keys','studio-brass','studio-reed','studio-organ'),
    set('acoustic','Acoustic / Classical',6,'studio-keys','bass-acoustic','guitar-steel','studio-strings','guitar-nylon','pad','studio-keys','studio-brass','studio-reed','studio-organ'),
} do M.entries[#M.entries+1]=s end
for _,entry in ipairs{{'recorded-clean','Recorded Band / Clean','sampled-electric-clean'},
    {'recorded-rock','Recorded Band / Rock','sampled-electric-crunch'},
    {'recorded-metal','Recorded Band / Metal','sampled-electric-metal'},
    {'recorded-thrash','Recorded Band / Thrash','sampled-thrash-rhythm'},
    {'recorded-funk','Recorded Band / Funk','sampled-funk-muted'}} do
    local s=set(entry[1],entry[2],9,'studio-keys','sampled-bass-finger',entry[3],'studio-strings','sampled-electric-lead','pad','glass-bell','studio-brass','studio-reed','studio-organ')
    s.recorded=true;s.muted=(entry[1]=='recorded-metal' or entry[1]=='recorded-thrash') and 'sampled-electric-muted' or (entry[1]=='recorded-rock' and 'sampled-rock-muted' or 'sampled-funk-muted')
    -- Explicit genre reinterpretation: many rock MIDIs mark rhythm guitars as
    -- GM clean. The other recorded sets continue to honor that clean identity.
    s.electricRhythm=entry[1]=='recorded-thrash'
    M.entries[#M.entries+1]=s
end
local recordedDrums={};for _,note in ipairs{35,36,37,38,40,41,42,43,44,45,46,47,48,49,50,51,52,53,57,59} do recordedDrums[note]=true end
local builtins={};for _,p in ipairs(schema.builtins) do builtins[p.id]=p end
local percussion={
    [35]='kick',[36]='kick',[37]='rim',[38]='snare',[39]='clap',[40]='snare',
    [41]='tom',[42]='hat',[43]='tom',[44]='pedalhat',[45]='tom',[46]='openhat',[47]='tom',[48]='tom',[50]='tom',
    [49]='crash',[52]='crash',[55]='crash',[57]='crash',[51]='ride',[59]='ride',[53]='ridebell',
    [54]='tambourine',[56]='cowbell',[69]='shaker',[70]='shaker',
}
M.tomFrequencies={[41]=85,[43]=105,[45]=130,[47]=155,[48]=185,[50]=220}
local percussionRoles={percussion=true};for _,role in pairs(percussion) do percussionRoles[role]=true end
function M.find(id) for _,s in ipairs(M.entries) do if s.id==id then return s end end end
local named={{'bass','bass'},{'guitar','guitar'},{'string','strings'},{'violin','strings'},{'cello','strings'},
    {'organ','organ'},{'brass','brass'},{'trumpet','brass'},{'flute','pipe'},{'sax','reed'},{'clarinet','reed'},
    {'pad','pad'},{'lead','lead'},{'bell','mallet'},{'piano','keys'}}
function M.role(part)
    if part.channel==9 then
        return percussion[part.note] or 'percussion'
    end
    local p=part.program
    if type(p)=='number' and p>=0 and p<=127 then
        return ({'keys','mallet','organ','guitar','bass','strings','strings','brass','reed','pipe','lead','pad','fx','pluck','mallet','fx'})[math.floor(p/8)+1]
    end
    local name=(part.name or ''):lower()
    for _,entry in ipairs(named) do if name:find(entry[1],1,true) then return entry[2] end end
    return 'keys'
end
function M.assignment(id,role,midi,note,instrument,program,name)
    local s=M.find(id);if not s then return end
    local drum={kick=1,snare=2,hat=3,openhat=4}
    local presetId=drum[role] and schema.kits[s.kit].ids[drum[role]] or 'b:'..(s.patches[role] or s.patches.keys)
    local piece=({rim='rim',clap='clap',pedalhat='pedalhat',crash='crash',ride='ride',ridebell='ridebell',cowbell='cowbell',tambourine='tambourine',shaker='shaker',percussion='percussion'})[role]
    if piece then presetId='b:kit-'..piece end
    -- GM programs are zero-based. Acoustic families stay acoustic across genre
    -- changes; electric overdrive/distortion uses the selected band's character.
    if role=='guitar' and type(program)=='number' then
        if program==24 then presetId='b:guitar-nylon'
        elseif program==25 then presetId='b:guitar-steel'
        elseif program==26 then presetId='b:guitar-warm'
        elseif program==27 then presetId='b:guitar-clean'
        elseif program==28 then presetId=(id=='metal' or id=='thrash' or id=='alternative-metal') and 'b:'..s.patches.guitar or 'b:guitar-funk'
        elseif program==29 or program==30 then
            presetId='b:'..((id=='balanced' or id=='acoustic') and (program==29 and 'guitar-crunch' or 'guitar-metal') or s.patches.guitar)
        elseif program==31 then presetId='b:guitar-lead' end
    elseif role=='bass' and type(program)=='number' then
        local bass={[32]='bass-acoustic',[33]='bass-finger',[34]='bass-picked',[35]='jazz-bass',[36]='bass-slap',[37]='bass-pop'}
        if bass[program] then presetId='b:'..bass[program] end
    end
    if id=='chiptune' and role=='snare' then presetId='b:chip-snare' end
    -- Physical prop overrides are independent of the MIDI sound identity. Keep
    -- the legacy prop fallback only when no source program is available.
    local soundInstrument=instrument
    if type(program)=='number' then
        soundInstrument=({strings='violin',brass='trumpet',reed='clarinet',pipe='flute'})[role]
        if not percussionRoles[role] then
            if program==42 or program==43 then soundInstrument='cello'
            elseif program==47 then soundInstrument='timpani' end
        end
    end
    if not percussionRoles[role] and id=='orchestra' and ({violin=true,cello=true,trumpet=true,flute=true,clarinet=true,timpani=true})[soundInstrument] then presetId='b:orchestra-'..soundInstrument end
    -- Timpani is pitched percussion despite sharing GM's broad strings group.
    -- Preserve it across genres rather than substituting a sustained string pad.
    if program==47 and not percussionRoles[role] then presetId='b:orchestra-timpani' end
    if s.recorded then
        if percussionRoles[role] and recordedDrums[note] then presetId='b:sampled-drum-kit'
        elseif role=='guitar' then
            local recorded={[24]='sampled-guitar-nylon',[25]='sampled-guitar-steel',[27]=s.electricRhythm and s.patches.guitar or (id=='recorded-funk' and 'sampled-funk-muted' or 'sampled-electric-clean'),[28]=s.muted,
                [29]=s.patches.guitar,[30]=s.patches.guitar,[31]='sampled-electric-lead'}
            if recorded[program] then presetId='b:'..recorded[program] end
            local label=(name or ''):lower()
            if (program==29 or program==30) and (label:find('solo',1,true) or label:find('lead',1,true)) then presetId='b:sampled-electric-lead' end
        elseif role=='bass' and (program==33 or program==34) then
            presetId=program==33 and 'b:sampled-bass-finger' or 'b:sampled-bass-picked'
        elseif role=='bass' and (program==38 or program==39) then
            presetId=program==38 and 'b:house-bass' or 'b:acid-bass'
        end
    end
    local p=builtins[presetId];if not p then return end
    local state=schema.state(p.state);state.polyphony=math.min(8,state.polyphony)
    -- MIDI bass notes already encode the written octave. Performance presets
    -- lower the keyboard by an octave for manual playing; undo that here.
    if midi and role=='bass' and p.role=='Bass' and not state.sample then state.tune1=state.tune1+12;state.tune2=state.tune2+12 end
    if role=='tom' then state.bodyFrequency=M.tomFrequencies[note] or 130 end
    return assert(schema.state(state)),presetId
end
function M.trackProgram(track,part)
    if part and part.program~=nil then return part.program end
    if track.sourceProgram~=nil then return track.sourceProgram end
    local p=builtins[track.presetId];return p and p.midiProgram
end
function M.trackRole(track,part)
    if part then return M.role(part) end
    if track.kind=='midi' and track.sourceRole then return track.sourceRole end
    if track.kind=='drum' then return M.role{channel=9,note=track.note} end
    local role=M.role{name=track.name};if role~='keys' then return role end
    local p=builtins[track.presetId];local tag=p and p.role
    return ({Bass='bass',Guitar='guitar',Pad='pad',Lead='lead',Bell='mallet',Pluck='pluck'})[tag] or (track.presetId=='b:bass' and 'bass' or 'keys')
end
function M.programPatch(project,track,program)
    if track.kind=='drum' or percussionRoles[track.sourceRole] or (track.part or ''):match('^%d+:9:') then
        return M.assignment(M.find(project.soundSet) and project.soundSet or 'balanced',percussionRoles[track.sourceRole] and track.sourceRole or M.role{channel=9,note=track.note},true,track.note,track.instrument)
    end
    local role=M.role{program=program}
    local instrument=({strings='violin',brass='trumpet',reed='clarinet',pipe='flute'})[role]
    if program==42 or program==43 then instrument='cello' elseif program==47 then instrument='timpani' end
    return M.assignment(M.find(project.soundSet) and project.soundSet or 'balanced',role,true,nil,instrument,program,track.name)
end
return M
