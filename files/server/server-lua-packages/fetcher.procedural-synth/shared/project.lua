-- Bounded, versioned DAW document shared by client and server.
-- Kept as a factory so both runtimes supply their own synth schema module.
return function(schema)
local M={version=1,maxTracks=16,maxNotes=512,maxSteps=256,maxMembers=8,maxEditedNotes=8192,maxPartNotes=4096,maxSongBeats=65536}
M.maxClips=8;M.maxRegions=64;M.maxProjectRegions=256;M.maxExpandedNotes=60000
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
-- Times are Studio steps (four per quarter note); absent lanes leave the patch alone.
M.automationOrder={};M.automationSpecs={};M.maxAutomation=8192
for i,name in ipairs(schema.order) do
    local source=schema.specs[name];local spec={min=source.min,max=source.max,default=source.default,discrete=source.discrete,code=127+i}
    if name=='polyphony' then spec.max=8;spec.default=8 end
    M.automationOrder[#M.automationOrder+1]=name;M.automationSpecs[name]=spec
end
for _,entry in ipairs{{'pitchBend',-2,2,0,183},{'modulation',0,1,0,184},{'expression',0,1,1,185},
    {'midiVolume',0,1,1,186},{'pedal',0,1,0,187,true},{'program',0,127,0,nil,true}} do
    local name=entry[1];M.automationOrder[#M.automationOrder+1]=name
    M.automationSpecs[name]={min=entry[2],max=entry[3],default=entry[4],code=entry[5],discrete=entry[6]}
end
function M.automation(input)
    if input==nil then return {} end
    if not keys(input,M.automationSpecs,61) then return nil end
    local out={};local count=0
    for name,points in pairs(input) do
        local spec=M.automationSpecs[name];if not array(points,1024) then return nil end
        local previous=-1;out[name]={}
        for _,point in ipairs(points) do
            if not keys(point,{s=true,v=true,linear=true},3) or not num(point.s,0,M.maxSongBeats*4) or point.s<=previous
                or not num(point.v,spec.min,spec.max) or spec.discrete and point.v%1~=0
                or type(point.linear)~='boolean' or spec.discrete and point.linear then return nil end
            previous=point.s;count=count+1;if count>M.maxAutomation then return nil end
            out[name][#out[name]+1]={s=point.s,v=point.v,linear=point.linear}
        end
    end
    return out,count
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
-- Compact saved MIDI notes: LE u32 onset/gate ticks, pitch and MIDI velocity.
-- Preserve source PPQ rather than quantizing imported timing to the editor grid.
local function pack32(n)
    local a=n%256;n=math.floor(n/256);local b=n%256;n=math.floor(n/256)
    return string.char(a,b,n%256,math.floor(n/256))
end
local function read32(s,at) local a,b,c,d=s:byte(at,at+3);return a+b*256+c*65536+d*16777216 end
function M.decodeNotes(data)
    if not keys(data,{division=true,bytes=true},2) or not int(data.division,1,32767) or type(data.bytes)~='string'
        or #data.bytes%10~=0 or #data.bytes>M.maxPartNotes*10 then return nil end
    local out={};local maximum=M.maxSongBeats*data.division
    for at=1,#data.bytes,10 do
        local s,l=read32(data.bytes,at),read32(data.bytes,at+4);local n,v=data.bytes:byte(at+8,at+9)
        if s>=maximum or s+l>maximum or n>127 or v<1 or v>127 then return nil end
        out[#out+1]={n=n,s=s/data.division*4,l=l/data.division*4,v=v/127}
    end
    return out
end
function M.encodeNotes(notes,division)
    if not array(notes,M.maxPartNotes) or not int(division,1,32767) then return nil end
    local bytes={}
    for _,n in ipairs(notes) do
        if not keys(n,{n=true,s=true,l=true,v=true},4) or not int(n.n,0,127) or not num(n.s,0,M.maxSongBeats*4)
            or not num(n.l,0,M.maxSongBeats*4) or not num(n.v,0.001,1) then return nil end
        local s,l=math.floor(n.s*division/4+0.5),math.max(0,math.floor(n.l*division/4+0.5))
        bytes[#bytes+1]=pack32(s)..pack32(l)..string.char(n.n,math.max(1,math.floor(n.v*127+0.5)))
    end
    local data={division=division,bytes=table.concat(bytes)}
    return M.decodeNotes(data) and data or nil
end
function M.timeline(value)
    if not keys(value,{beats=true,tempos=true,meters=true},3) or not num(value.beats,0,M.maxSongBeats) or not array(value.tempos,1024) then return nil end
    local out={beats=value.beats,tempos={}};local previous=-1
    for _,t in ipairs(value.tempos) do
        if not keys(t,{beat=true,tempo=true},2) or not num(t.beat,0,M.maxSongBeats) or t.beat<previous or not int(t.tempo,1,16777215) then return nil end
        out.tempos[#out.tempos+1]={beat=t.beat,tempo=t.tempo};previous=t.beat
    end
    if value.meters~=nil then
        if not array(value.meters,128) then return nil end
        out.meters={};local previous=-1
        for _,m in ipairs(value.meters) do
            if not keys(m,{beat=true,numerator=true,denominator=true},3) or not num(m.beat,0,M.maxSongBeats)
                or m.beat<=previous or previous>=0 and m.beat-previous<1e-7 or m.beat>0 and m.beat<1e-7 or not M.validMeter(m.numerator,m.denominator) then return nil end
            out.meters[#out.meters+1]={beat=m.beat,numerator=m.numerator,denominator=m.denominator};previous=m.beat
        end
    end
    if M.seconds(out,out.beats)>1800 then return nil end
    for _,t in ipairs(out.tempos) do if M.seconds(out,t.beat)>1800 then return nil end end
    for _,m in ipairs(out.meters or {}) do if M.seconds(out,m.beat)>1800 then return nil end end
    return out
end
local clocks=setmetatable({},{__mode='k'})
function M.seconds(timeline,beat)
    local clock=clocks[timeline]
    if not clock then
        clock={{beat=0,time=0,tempo=500000}};local previous=clock[1]
        for _,t in ipairs(timeline.tempos or {}) do
            local entry={beat=t.beat,tempo=t.tempo,time=previous.time+(t.beat-previous.beat)*previous.tempo/1000000}
            clock[#clock+1]=entry;previous=entry
        end
        clocks[timeline]=clock
    end
    local low,high=1,#clock
    while low<high do local middle=math.floor((low+high+1)/2);if clock[middle].beat<=beat then low=middle else high=middle-1 end end
    local t=clock[low];return t.time+(beat-t.beat)*t.tempo/1000000
end
local roles={keys=true,mallet=true,organ=true,guitar=true,bass=true,strings=true,brass=true,reed=true,pipe=true,lead=true,pad=true,fx=true,pluck=true,kick=true,snare=true,hat=true,openhat=true,tom=true}
-- Authored songs use the same saved PPQ notes as converted MIDI parts.
-- Bars and loop markers are independent of the sixteen-bar clip overlay.
function M.composition(value)
    if not keys(value,{bars=true,numerator=true,denominator=true,loopStart=true,loopEnd=true,loop=true},6)
        or not int(value.bars,1,256) or not M.validMeter(value.numerator,value.denominator)
        or not int(value.loopStart,1,value.bars) or not int(value.loopEnd,value.loopStart,value.bars)
        or type(value.loop)~='boolean' then return nil end
    return {bars=value.bars,numerator=value.numerator,denominator=value.denominator,
        loopStart=value.loopStart,loopEnd=value.loopEnd,loop=value.loop}
end
-- A meter change starts a new bar, including changes inside a source MIDI bar.
-- Notes/automation keep quarter-note positions. Cache only immutable map objects.
function M.validMeter(n,d) return int(n,1,32) and ({[1]=true,[2]=true,[4]=true,[8]=true,[16]=true,[32]=true,[64]=true})[d]==true end
local grids=setmetatable({},{__mode='k'})
function M.meterMap(p)
    local key=p.timeline or p.composition or p;local cached=grids[key];if cached then return cached end
    local s=p.composition;local n,d=s and s.numerator or 4,s and s.denominator or 4
    local out={{beat=0,bar=1,numerator=n,denominator=d,length=n*4/d}}
    for _,m in ipairs(p.timeline and p.timeline.meters or {}) do
        local previous=out[#out]
        local bar=previous.bar+math.ceil((m.beat-previous.beat)/previous.length-1e-9)
        local entry={beat=m.beat,bar=bar,numerator=m.numerator,denominator=m.denominator,length=m.numerator*4/m.denominator}
        if m.beat==0 then out[1]=entry else out[#out+1]=entry end
    end
    grids[key]=out;return out
end
local function segment(map,key,value)
    local low,high=1,#map
    while low<high do local mid=math.floor((low+high+1)/2);if map[mid][key]<=value+1e-9 then low=mid else high=mid-1 end end
    return map[low]
end
function M.barBeats(p,beat) return segment(M.meterMap(p),'beat',beat or 0).length end
function M.barStart(p,bar)
    local m=segment(M.meterMap(p),'bar',bar);return m.beat+(bar-m.bar)*m.length
end
function M.barAt(p,beat)
    local m=segment(M.meterMap(p),'beat',math.max(0,beat));return m.bar+math.floor((math.max(0,beat)-m.beat)/m.length+1e-9)
end
function M.barPosition(p,beat)
    local bar=M.barAt(p,beat);local first,last=M.barStart(p,bar),M.barStart(p,bar+1)
    return bar+(beat-first)/(last-first)
end
function M.meterAt(p,beat) local m=segment(M.meterMap(p),'beat',beat or 0);return m.numerator,m.denominator end
function M.tempoAt(p,beat)
    local tempo=500000;for _,t in ipairs(p.timeline and p.timeline.tempos or {}) do if t.beat>beat then break end;tempo=t.tempo end
    return tempo
end
function M.songRange(p)
    local s=p.composition;if not s then return end
    return M.barStart(p,s.loop and s.loopStart or 1),M.barStart(p,(s.loop and s.loopEnd or s.bars)+1)
end
function M.barBoundaries(p,first,last,maximum)
    local out={};local bar=M.barAt(p,first)
    if M.barStart(p,bar)<first-1e-8 then bar=bar+1 end
    while M.barStart(p,bar)<=last+1e-8 do
        if #out>=(maximum or 4096) then return nil end
        out[#out+1]={beat=M.barStart(p,bar),bar=bar};bar=bar+1
    end
    return out
end
function M.arrangement(value)
    if not keys(value,{clips=true,regions=true},2) or not array(value.clips,M.maxClips) or not array(value.regions,M.maxRegions) then return nil end
    local out={clips={},regions={}};local pitches={}
    for _,clip in ipairs(value.clips) do
        if not keys(clip,{name=true,length=true,notes=true},3) or not text(clip.name,32) or not num(clip.length,0.25,M.maxSteps) then return nil end
        local notes=M.decodeNotes(clip.notes);if not notes then return nil end
        local low,high=127,0
        for _,n in ipairs(notes) do
            if n.s>=clip.length or n.s+n.l>clip.length+1e-8 then return nil end
            low=math.min(low,n.n);high=math.max(high,n.n)
        end
        pitches[#pitches+1]={low,high}
        out.clips[#out.clips+1]={name=clip.name,length=clip.length,notes={division=clip.notes.division,bytes=clip.notes.bytes}}
    end
    for _,r in ipairs(value.regions) do
        if not keys(r,{clip=true,s=true,l=true,transpose=true,mute=true},5) or not int(r.clip,1,#out.clips)
            or not num(r.s,0,M.maxSongBeats*4) or not num(r.l,0.25,M.maxSongBeats*4) or r.s+r.l>M.maxSongBeats*4
            or not int(r.transpose,-48,48) or type(r.mute)~='boolean' then return nil end
        local bounds=pitches[r.clip]
        if bounds[1]+r.transpose<0 or bounds[2]+r.transpose>127 then return nil end
        out.regions[#out.regions+1]={clip=r.clip,s=r.s,l=r.l,transpose=r.transpose,mute=r.mute}
    end
    return out
end
function M.savedNotes(p)
    local count=0
    for _,t in ipairs(p.tracks) do
        count=count+(t.edited and #t.edited.bytes/10 or 0)
        for _,clip in ipairs(t.arrangement and t.arrangement.clips or {}) do count=count+#clip.notes.bytes/10 end
    end
    return count
end
-- Count before expanding: a tiny source repeated over a long region must not
-- create an unbounded compile loop, including for empty or muted sources.
function M.regionNoteCount(t)
    local a=t.arrangement;if not a then return 0 end
    local count=0;local cache={}
    for _,r in ipairs(a.regions) do if not r.mute then
        local clip=a.clips[r.clip];local notes=cache[r.clip]
        if not notes then notes=M.decodeNotes(clip.notes);cache[r.clip]=notes end
        local cycles=math.floor(r.l/clip.length);count=count+cycles*#notes
        local rest=r.l-cycles*clip.length
        for _,n in ipairs(notes) do if n.s<rest-1e-8 then count=count+1 end end
        if count>M.maxExpandedNotes then return nil end
    end end
    return count
end
function M.eachRegionNote(t,emit)
    assert(M.regionNoteCount(t),'Arrangement exceeds 60,000 expanded notes.')
    local a=t.arrangement;if not a then return end
    local cache={}
    for _,r in ipairs(a.regions) do if not r.mute then
        local clip=a.clips[r.clip];local notes=cache[r.clip]
        if not notes then notes=M.decodeNotes(clip.notes);cache[r.clip]=notes end
        if #notes>0 then for cycle=0,math.ceil(r.l/clip.length)-1 do
            local offset=cycle*clip.length
            for _,n in ipairs(notes) do if offset+n.s<r.l-1e-8 then
                emit(n.n+r.transpose,r.s+offset+n.s,math.min(n.l,r.l-offset-n.s),n.v)
            end end
        end end
    end end
end
local trackKeys={name=true,kind=true,part=true,presetId=true,params=true,level=true,mute=true,solo=true,note=true,gate=true,steps=true,notes=true,output=true,mixer=true,edited=true,sourceRole=true,arrangement=true,automation=true,followPrograms=true}
function M.track(t,length)
    if not keys(t,trackKeys,20) or not text(t.name,48) or (t.kind~='midi' and t.kind~='piano' and t.kind~='drum')
        or not text(t.part or '',48) or not text(t.presetId or '',32) or not num(t.level,0,1)
        or type(t.mute)~='boolean' or type(t.solo)~='boolean' or not int(t.note,0,127) or not num(t.gate,0.01,1)
        or type(t.steps)~='string' or (#t.steps<length or #t.steps>M.maxSteps or #t.steps%16~=0) or t.steps:find('[^0-9]')
        or not array(t.notes,128) or not keys(t.output,{kind=true,slot=true},2) or t.output.kind~='keyboard' or not int(t.output.slot or 0,0,M.maxMembers) then return nil end
    local mixer=M.mixer(t.mixer);if not mixer then return nil end
    local params=schema.state(t.params);if not params then return nil end;params.polyphony=math.min(8,params.polyphony)
    local out={name=t.name,kind=t.kind,part=t.part or '',presetId=t.presetId or '',params=params,level=t.level,
        mute=t.mute,solo=t.solo,note=t.note,gate=t.gate,steps=t.steps..string.rep('0',M.maxSteps-#t.steps),notes={},mixer=mixer,output={kind='keyboard',slot=t.output.slot or 0}}
    if t.sourceRole~=nil then if t.kind~='midi' or not roles[t.sourceRole] then return nil end;out.sourceRole=t.sourceRole end
    if t.edited~=nil then
        if t.kind~='midi' or not M.decodeNotes(t.edited) then return nil end
        out.edited={division=t.edited.division,bytes=t.edited.bytes}
    end
    if t.arrangement~=nil then
        if not out.edited then return nil end
        out.arrangement=M.arrangement(t.arrangement);if not out.arrangement then return nil end
    end
    if t.automation~=nil then out.automation=M.automation(t.automation);if not out.automation then return nil end end
    if t.followPrograms~=nil then
        if type(t.followPrograms)~='boolean' or t.followPrograms and params.sample then return nil end
        out.followPrograms=t.followPrograms
    end
    for _,n in ipairs(t.notes) do
        if not keys(n,{n=true,s=true,l=true,v=true},4) or not int(n.n,0,127) or not num(n.s,0,M.maxSteps-0.000001)
            or not num(n.l,1/960,M.maxSteps) or not num(n.v,0.001,1) then return nil end
        out.notes[#out.notes+1]={n=n.n,s=n.s,l=n.l,v=n.v}
    end
    return out
end
function M.project(p)
    if not keys(p,{version=true,name=true,bpm=true,swing=true,length=true,loop=true,midi=true,tracks=true,master=true,effects=true,soundSet=true,band=true,timeline=true,composition=true},14)
        or not num(p.master or 0.7,0,1) or p.version~=1 or not text(p.name,64) or not num(p.bpm,40,240) or not num(p.swing,0,0.6)
        or not int(p.length,16,M.maxSteps) or p.length%16~=0 or type(p.loop)~='boolean' or not text(p.midi,192)
        or not text(p.soundSet or '',32) or not array(p.tracks,M.maxTracks) then return nil end
    if p.midi~='' and (not p.midi:match('^midi/') or p.midi:find('..',1,true) or not p.midi:lower():match('%.mid[i]?$')) then return nil end
    local effects=M.effects(p.effects);local band=M.band(p.band);if not effects or not band then return nil end
    local out={band=band,soundSet=p.soundSet or '',effects=effects,version=1,name=p.name,bpm=p.bpm,swing=p.swing,length=p.length,loop=p.loop,midi=p.midi,master=p.master or 0.7,tracks={}}
    if p.timeline~=nil then out.timeline=M.timeline(p.timeline);if not out.timeline then return nil end end
    if p.composition~=nil then
        out.composition=M.composition(p.composition)
        if not out.composition or p.midi~='' or not out.timeline or math.abs(out.timeline.beats-M.barStart(out,out.composition.bars+1))>1e-7
            or not out.timeline.tempos[1] or out.timeline.tempos[1].beat~=0
            or out.timeline.tempos[1].tempo~=math.floor(60000000/p.bpm+0.5)
            or out.composition.numerator~=select(1,M.meterAt(out,0)) or out.composition.denominator~=select(2,M.meterAt(out,0)) then return nil end
    end
    local notes,edited,regions,expanded,automation=0,0,0,0,0
    for _,t in ipairs(p.tracks) do
        local track=M.track(t,p.length);if not track then return nil end
        if out.composition and track.kind=='midi' and not track.edited then return nil end
        notes=notes+#track.notes;if notes>M.maxNotes then return nil end
        if track.edited then
            if not out.timeline then return nil end
            edited=edited+#track.edited.bytes/10;if edited>M.maxEditedNotes then return nil end
            for _,n in ipairs(M.decodeNotes(track.edited)) do if M.seconds(out.timeline,(n.s+n.l)/4)>1800 then return nil end end
            expanded=expanded+#track.edited.bytes/10
        end
        if track.arrangement then
            for _,clip in ipairs(track.arrangement.clips) do edited=edited+#clip.notes.bytes/10 end
            regions=regions+#track.arrangement.regions
            local count=M.regionNoteCount(track);if not count then return nil end;expanded=expanded+count
            for _,r in ipairs(track.arrangement.regions) do if M.seconds(out.timeline,(r.s+r.l)/4)>1800 then return nil end end
        end
        if edited>M.maxEditedNotes or regions>M.maxProjectRegions or expanded>M.maxExpandedNotes then return nil end
        for _,points in pairs(track.automation or {}) do
            automation=automation+#points;if automation>M.maxAutomation then return nil end
            for _,point in ipairs(points) do
                local seconds=out.timeline and M.seconds(out.timeline,point.s/4) or point.s/4*60/out.bpm
                if seconds>1800 then return nil end
            end
        end
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
