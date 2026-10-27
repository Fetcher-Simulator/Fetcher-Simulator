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
local builtins={};for _,p in ipairs(schema.builtins) do builtins[p.id]=p end
function M.find(id) for _,s in ipairs(M.entries) do if s.id==id then return s end end end
local named={{'bass','bass'},{'guitar','guitar'},{'string','strings'},{'violin','strings'},{'cello','strings'},
    {'organ','organ'},{'brass','brass'},{'trumpet','brass'},{'flute','pipe'},{'sax','reed'},{'clarinet','reed'},
    {'pad','pad'},{'lead','lead'},{'bell','mallet'},{'piano','keys'}}
function M.role(part)
    if part.channel==9 then
        local n=part.note
        if n==35 or n==36 then return 'kick' elseif n==42 or n==44 then return 'hat' elseif n==46 then return 'openhat'
        elseif n==41 or n==43 or n==45 or n==47 or n==48 or n==50 then return 'tom' end
        return 'snare'
    end
    local p=part.program
    if type(p)=='number' and p>=0 and p<=127 then
        return ({'keys','mallet','organ','guitar','bass','strings','strings','brass','reed','pipe','lead','pad','fx','pluck','mallet','fx'})[math.floor(p/8)+1]
    end
    local name=(part.name or ''):lower()
    for _,entry in ipairs(named) do if name:find(entry[1],1,true) then return entry[2] end end
    return 'keys'
end
function M.assignment(id,role,midi,note,instrument)
    local s=M.find(id);if not s then return end
    local drum={kick=1,snare=2,hat=3,openhat=4}
    local presetId=drum[role] and schema.kits[s.kit].ids[drum[role]] or 'b:'..(s.patches[role] or s.patches.keys)
    if id=='chiptune' and role=='snare' then presetId='b:chip-snare' end
    if id=='orchestra' and ({violin=true,cello=true,trumpet=true,flute=true,clarinet=true,timpani=true})[instrument] then presetId='b:orchestra-'..instrument end
    local p=builtins[presetId];if not p then return end
    local state=schema.state(p.state);state.polyphony=math.min(8,state.polyphony)
    -- MIDI bass notes already encode the written octave. Performance presets
    -- lower the keyboard by an octave for manual playing; undo that here.
    if midi and role=='bass' and id~='orchestra' then state.tune1=state.tune1+12;state.tune2=state.tune2+12 end
    if role=='tom' then state.bodyFrequency=({[41]=85,[43]=105,[45]=130,[47]=155,[48]=185,[50]=220})[note] or 130 end
    return assert(schema.state(state)),presetId
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
    local role=M.role{program=program}
    local instrument=({strings='violin',brass='trumpet',reed='clarinet',pipe='flute'})[role]
    if program==42 or program==43 then instrument='cello' elseif program==47 then instrument='timpani' end
    return M.assignment(M.find(project.soundSet) and project.soundSet or 'balanced',role,true,nil,instrument)
end
return M
