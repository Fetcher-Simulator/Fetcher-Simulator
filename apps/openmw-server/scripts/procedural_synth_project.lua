-- Bounded, versioned DAW document shared by client and server.
-- Kept as a factory so both runtimes supply their own synth schema module.
return function(schema)
local M={version=1,maxTracks=16,maxNotes=512,maxSteps=256,maxMembers=8}
local function num(v,a,b) return type(v)=='number' and v==v and v>=a and v<=b end
local function int(v,a,b) return num(v,a,b) and v%1==0 end
local function text(v,n) return type(v)=='string' and #v<=n and not v:find('%c') end
local function keys(t,allowed,max)
    if type(t)~='table' then return false end
    local n=0;for k in pairs(t) do n=n+1;if n>max or not allowed[k] then return false end end
    return true
end
local function array(t,max)
    if type(t)~='table' then return false end
    local n=0;for k in pairs(t) do if not int(k,1,max) then return false end;n=n+1 end
    return n==#t and n<=max
end
M.mixerSpecs={eqEnabled={0,1,0},highpass={20,1000,20},bass={-18,18,0},mid={-18,18,0},midFrequency={200,6000,1000},treble={-18,18,0},
    compEnabled={0,1,0},threshold={-60,0,-18},ratio={1,20,4},attack={0.1,200,10},release={10,2000,150},makeup={0,24,0},delaySend={0,1,0},reverbSend={0,1,0}}
M.mixerOrder={'eqEnabled','highpass','bass','mid','midFrequency','treble','compEnabled','threshold','ratio','attack','release','makeup','delaySend','reverbSend'}
function M.mixer(t)
    if t==nil then t={} end
    if not keys(t,M.mixerSpecs,14) then return nil end
    local out={}
    for k,s in pairs(M.mixerSpecs) do
        local value=t[k];if value==nil then value=s[3] end
        if not num(value,s[1],s[2]) or ((k=='eqEnabled' or k=='compEnabled') and value%1~=0) then return nil end
        out[k]=value
    end
    return out
end
M.effectsSpecs={delayEnabled={0,1,1},delayTime={40,1500,250},delayFeedback={0,0.85,0.35},delayReturn={0,1,0.35},
    reverbEnabled={0,1,1},reverbDecay={0.2,8,1.8},reverbDamping={200,16000,6000},reverbReturn={0,1,0.25}}
M.effectsOrder={'delayEnabled','delayTime','delayFeedback','delayReturn','reverbEnabled','reverbDecay','reverbDamping','reverbReturn'}
function M.effects(t)
    if t==nil then t={} end
    if not keys(t,M.effectsSpecs,8) then return nil end
    local out={}
    for k,s in pairs(M.effectsSpecs) do
        local value=t[k];if value==nil then value=s[3] end
        if not num(value,s[1],s[2]) or ((k=='delayEnabled' or k=='reverbEnabled') and value%1~=0) then return nil end
        out[k]=value
    end
    return out
end
M.instruments={keyboard='fetcher_procedural_keyboard_v1',lute='misc_de_lute_01',drum='misc_de_drum_01'}
function M.target(value)
    if type(value)~='table' then return nil end
    local kind=value.kind or 'keyboard'
    if kind=='keyboard' then
        if not keys(value,{kind=true,mpNum=true,cell=true,refId=true},4) or not int(value.mpNum,1,4294967295)
            or not text(value.cell,128) or #value.cell==0 or not text(value.refId,128)
            or (value.refId~='fetcher_procedural_synth_v1' and value.refId~='fetcher_procedural_keyboard_v1') then return nil end
        return {mpNum=value.mpNum,cell=value.cell,refId=value.refId}
    elseif kind=='player' then
        if not keys(value,{kind=true,characterId=true,instrument=true},3) or not int(value.characterId,1,9007199254740991)
            or not M.instruments[value.instrument] then return nil end
        return {kind='player',characterId=value.characterId,instrument=value.instrument}
    elseif kind=='npc' then
        if not keys(value,{kind=true,mpNum=true,refId=true,instrument=true},4) or not int(value.mpNum,1,4294967295)
            or not text(value.refId,128) or not value.refId:match('^fetcher_synth_bard_%d+_%d+$') or not M.instruments[value.instrument] then return nil end
        return {kind='npc',mpNum=value.mpNum,refId=value.refId,instrument=value.instrument}
    end
end
function M.targetKey(t)
    if not t then return '' end
    return table.concat({t.kind or 'keyboard',t.characterId or t.mpNum or 0,t.refId or '',t.cell or '',t.instrument or ''},':')
end
function M.band(value)
    if value==nil then value={} end
    if not array(value,M.maxMembers) then return nil end
    local out={}
    for n=1,M.maxMembers do
        local m=value[n] or {name='Member '..n,level=1,mute=false}
        if not keys(m,{name=true,level=true,mute=true,target=true},4) or not text(m.name,48)
            or not num(m.level,0,1) or type(m.mute)~='boolean' then return nil end
        local target=m.target and M.target(m.target)
        if m.target and not target then return nil end
        out[n]={name=m.name,level=m.level,mute=m.mute,target=target}
    end
    return out
end
function M.routes(value)
    if not array(value,M.maxTracks) then return nil end
    local out={};for n,slot in ipairs(value) do if not int(slot,0,M.maxMembers) then return nil end;out[n]=slot end
    return out
end
local trackKeys={name=true,kind=true,part=true,presetId=true,params=true,level=true,mute=true,solo=true,note=true,gate=true,steps=true,notes=true,output=true,mixer=true}
function M.track(t,length)
    if not keys(t,trackKeys,15) or not text(t.name,48) or (t.kind~='midi' and t.kind~='piano' and t.kind~='drum')
        or not text(t.part or '',48) or not text(t.presetId or '',32) or not num(t.level,0,1)
        or type(t.mute)~='boolean' or type(t.solo)~='boolean' or not int(t.note,0,127) or not num(t.gate,0.01,1)
        or type(t.steps)~='string' or (#t.steps<length or #t.steps>M.maxSteps or #t.steps%16~=0) or t.steps:find('[^0-9]')
        or not array(t.notes,128) or not keys(t.output,{kind=true,slot=true},2) or t.output.kind~='keyboard' or not int(t.output.slot or 0,0,M.maxMembers) then return nil end
    local mixer=M.mixer(t.mixer);if not mixer then return nil end
    local params=schema.state(t.params);if not params then return nil end;params.polyphony=math.min(8,params.polyphony)
    local out={name=t.name,kind=t.kind,part=t.part or '',presetId=t.presetId or '',params=params,level=t.level,
        mute=t.mute,solo=t.solo,note=t.note,gate=t.gate,steps=t.steps..string.rep('0',M.maxSteps-#t.steps),notes={},mixer=mixer,output={kind='keyboard',slot=t.output.slot or 0}}
    for _,n in ipairs(t.notes) do
        if not keys(n,{n=true,s=true,l=true,v=true},4) or not int(n.n,0,127) or not int(n.s,0,M.maxSteps-1)
            or not int(n.l,1,M.maxSteps) or not num(n.v,0.01,1) then return nil end
        out.notes[#out.notes+1]={n=n.n,s=n.s,l=n.l,v=n.v}
    end
    return out
end
function M.project(p)
    if not keys(p,{version=true,name=true,bpm=true,swing=true,length=true,loop=true,midi=true,tracks=true,master=true,effects=true,soundSet=true,band=true},12)
        or not num(p.master or 0.7,0,1) or p.version~=1 or not text(p.name,64) or not num(p.bpm,40,240) or not num(p.swing,0,0.6)
        or not int(p.length,16,M.maxSteps) or p.length%16~=0 or type(p.loop)~='boolean' or not text(p.midi,192)
        or not text(p.soundSet or '',32) or not array(p.tracks,M.maxTracks) then return nil end
    if p.midi~='' and (not p.midi:match('^midi/') or p.midi:find('..',1,true) or not p.midi:lower():match('%.mid[i]?$')) then return nil end
    local effects=M.effects(p.effects);local band=M.band(p.band);if not effects or not band then return nil end
    local out={band=band,soundSet=p.soundSet or '',effects=effects,version=1,name=p.name,bpm=p.bpm,swing=p.swing,length=p.length,loop=p.loop,midi=p.midi,master=p.master or 0.7,tracks={}}
    local notes=0
    for _,t in ipairs(p.tracks) do
        local track=M.track(t,p.length);if not track then return nil end
        notes=notes+#track.notes;if notes>M.maxNotes then return nil end
        out.tracks[#out.tracks+1]=track
    end
    return out
end
function M.new() return {band=M.band(),soundSet='balanced',effects=M.effects(),version=1,name='New project',bpm=120,swing=0,length=16,loop=true,midi='',master=0.7,tracks={}} end
function M.newTrack(kind,name,length)
    return {kind=kind,name=name,part='',presetId='',params=schema.state({polyphony=8}),level=0.8,mute=false,solo=false,
        note=60,gate=0.12,steps=string.rep('0',M.maxSteps),notes={},mixer=M.mixer(),output={kind='keyboard',slot=0}}
end
return M
end
