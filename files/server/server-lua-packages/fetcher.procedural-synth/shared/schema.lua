-- Versioned wire/persistence format. Kept identical to the package shared/schema.lua.
local M = { version = 1, specs = {}, order = {} }
local function p(name, low, high, default, discrete)
    M.specs[name] = { min = low, max = high, default = default, discrete = discrete }
    M.order[#M.order + 1] = name
end
p('wave1',0,4,0,true); p('volume1',0,1,0.8); p('tune1',-24,24,0); p('fine1',-100,100,0)
p('enabled2',0,1,0,true); p('wave2',0,4,2,true); p('volume2',0,1,0.4)
p('tune2',-24,24,0); p('fine2',-100,100,7)
p('attack',0.002,5,0.01); p('decay',0.002,5,0.2); p('sustain',0,1,0.7); p('release',0.005,8,0.3)
p('cutoff',20,18000,8000); p('resonance',0,0.95,0.1); p('filterAmount',-8,8,0)
p('filterAttack',0.002,5,0.01); p('filterDecay',0.002,5,0.3); p('filterSustain',0,1,0); p('filterRelease',0.005,8,0.3)
p('lfoRate',0.05,20,4); p('lfoDepth',0,1,0); p('lfoTarget',0,2,0,true)
p('volume',0,1,0.7); p('gain',0,2,1); p('polyphony',1,32,16,true)
p('pitchAmount',-48,48,0); p('pitchDecay',0.005,2,0.08)
p('drumType',0,4,0,true)
p('bodyFrequency',30,1000,180); p('bodyDecay',0.01,2,0.14); p('bodyLevel',0,1,0.55)
p('noiseDecay',0.005,3,0.22); p('noiseLevel',0,1,0.75)
p('noiseHighpass',20,16000,1500); p('noiseLowpass',200,18000,11000)
p('drumPitchAmount',0,48,8); p('drumPitchDecay',0.005,0.5,0.025)
p('drumClick',0,1,0.12); p('drumDrive',1,8,1.3); p('ampCurve',0.25,4,1)
p('instrumentType',0,1,0,true);p('stringDecay',0.1,10,3);p('stringDamping',0,1,0.35)
p('stringMute',0,1,0);p('stringPick',0.05,0.95,0.23)
p('driveType',0,5,0,true);p('drive',0,36,12);p('driveTone',200,16000,12000)
p('fxMix',0,1,1);p('fxOutput',0,2,1)
p('ampBass',-12,12,0);p('ampMid',-12,12,0);p('ampTreble',-12,12,0);p('cabinet',0,6,0,true)
p('preHighpass',0,500,0);p('cabinetHighpass',0,500,0);p('cabinetLowpass',0,18000,0)
p('stringArticulation',0,3,0,true);p('pickAttack',0,1,0);p('pickDecay',.001,.05,.006)
p('stringResonance',0,1,0);p('pickPitch',-2,2,0);p('pickVariation',0,1,0);p('pickDirection',0,2,0,true)
p('parallelMix',0,1,0);p('parallelClean',0,1,.35);p('parallelDirty',0,1,.75);p('parallelNoise',0,1,0)
p('parallelDrive',0,48,18);p('parallelShape',0,3,0,true);p('parallelComb',0,1,0);p('parallelDelay',.5,20,3.5)
p('parallelFeedback',-.85,.85,.35);p('parallelRate',0,12,.5);p('parallelDepth',0,1,0);p('parallelLift',0,18,0)
p('stringLegato',0,2,0,true);p('stringSlideTime',0,1,.08)
p('stringLayout',0,6,0,true);p('stringSelect',0,7,0,true)
p('stringTouch',0,1,0);p('stringReleaseLevel',0,1,0)
p('captureTightness',0,1,1)
function M.valid(name, value)
    local s = M.specs[name]
    return s and type(value) == 'number' and value == value and value >= s.min and value <= s.max
        and (not s.discrete or value % 1 == 0)
end
M.strikeOrder={'stringArticulation','stringSelect','pickDirection'}
function M.strike(input)
    if type(input)~='table' then return nil end
    local out={};local allowed={stringArticulation=true,stringSelect=true,pickDirection=true};local count=0
    for name,value in pairs(input) do
        count=count+1;if count>3 or not allowed[name] or not M.valid(name,value) then return nil end
        out[name]=value
    end
    return out
end
-- Mixed radix: absent/4 articulations x absent/8 selections x absent/3 picks.
-- Zero means no per-note override; 1..179 are complete or partial snapshots.
function M.strikeByte(input)
    if input==nil then return 0 end
    local e=M.strike(input);if not e then return nil end
    return (e.stringArticulation and e.stringArticulation+1 or 0)
        +5*(e.stringSelect and e.stringSelect+1 or 0)+45*(e.pickDirection and e.pickDirection+1 or 0)
end
function M.byteStrike(value)
    if type(value)~='number' or value<0 or value>179 or value%1~=0 then return nil end
    local a=value%5;local s=math.floor(value/5)%9;local d=math.floor(value/45)
    return {stringArticulation=a>0 and a-1 or nil,stringSelect=s>0 and s-1 or nil,pickDirection=d>0 and d-1 or nil}
end
M.attackOrder={'open','short','dead'}
function M.sample(input,leaf)
    if type(input)~='table' then return nil end
    local fields={path=true,offset=true,duration=true,root=true,oneShot=true,hash=true,gain=true,attacks=not leaf}
    for key in pairs(input) do if not fields[key] then return nil end end
    local path=input.path
    if type(path)~='string' or #path>240 or not (path:sub(1,6)=='sound/' or path:sub(1,6)=='music/')
        or path~=path:lower() or path:find('[%c\\:]') or path:find('..',1,true) or path:find('//',1,true)
        or not ({mp3=true,wav=true,ogg=true,flac=true,opus=true,fsb=true})[path:match('%.([a-z0-9]+)$')] then return nil end
    local function number(n,low,high) return type(n)=='number' and n==n and n>=low and n<=high end
    if not number(input.offset,0,600) or not number(input.duration,0.01,30)
        or not number(input.root,0,127) or input.root%1~=0 or type(input.oneShot)~='boolean'
        or type(input.hash)~='string' or #input.hash~=32 or input.hash:find('[^0-9a-f]') then return nil end
    if path:sub(-4)=='.fsb' and (path:sub(1,20)~='sound/fetcher/banks/' or input.offset~=0
        or input.duration~=1 or input.root~=60 or input.oneShot) then return nil end
    local result={path=path,offset=math.floor(input.offset*1000+0.5)/1000,
        duration=math.floor(input.duration*1000+0.5)/1000,root=input.root,oneShot=input.oneShot,hash=input.hash}
    if input.gain~=nil then
        if not number(input.gain,.25,8) then return nil end
        if input.gain~=1 then result.gain=math.floor(input.gain*1000+.5)/1000 end
    end
    if input.attacks~=nil then
        if leaf or path:sub(-4)~='.fsb' or type(input.attacks)~='table' then return nil end
        local allowed={open=true,short=true,dead=true};local count=0;result.attacks={}
        for key,value in pairs(input.attacks) do
            if not allowed[key] then return nil end
            local ref=M.sample(value,true);if not ref or ref.path:sub(-4)~='.fsb' then return nil end
            result.attacks[key]=ref;count=count+1
        end
        if count==0 then return nil end
    end
    return result
end
function M.isBank(sample) return sample and sample.path:sub(-4)=='.fsb' or false end
function M.sampleKey(sample)
    if not sample then return '' end
    local key=table.concat({sample.path,sample.offset,sample.duration,sample.root,tostring(sample.oneShot),sample.hash},'|')
    if sample.gain then key=key..'|gain='..sample.gain end
    for _,attack in ipairs(M.attackOrder) do if sample.attacks and sample.attacks[attack] then
        key=key..'|'..attack..'='..M.sampleKey(sample.attacks[attack])
    end end
    return key
end
function M.state(input)
    if type(input) ~= 'table' or (input.version ~= nil and input.version ~= 1) then return nil end
    local count, result = 0, { version = 1 }
    for key, value in pairs(input) do
        count = count + 1
        if count > #M.order+2 or (key ~= 'version' and key~='sample' and not M.valid(key, value)) then return nil end
    end
    -- Missing fields in old version-1 saves receive stable defaults.
    for name, spec in pairs(M.specs) do result[name] = input[name] or spec.default end
    if input.sample~=nil then result.sample=M.sample(input.sample);if not result.sample then return nil end end
    return result
end
M.builtins = {
    {id='b:pad', name='Soft Pad', state={wave1=2, enabled2=1, wave2=2, fine2=12, attack=0.9, release=2, cutoff=1800}},
    {id='b:pluck', name='Plucked String', state={wave1=1, attack=0.002, decay=0.4, sustain=0, release=0.2, cutoff=3000, filterAmount=2}},
    {id='b:lead', name='Retro Lead', state={wave1=2, enabled2=1, wave2=3, volume2=0.2, cutoff=6500, lfoDepth=0.08}},
    {id='b:bass', name='Bass', state={wave1=3, tune1=-12, cutoff=600, resonance=0.3, filterAmount=3, decay=0.18, sustain=0.5}},
    {id='b:flute', name='Flute-like', state={wave1=0, enabled2=1, wave2=0, tune2=12, volume2=0.1, attack=0.08, cutoff=3500, lfoDepth=0.08, lfoRate=5}},
    {id='b:kick',name='Synth Kick',state={drumType=1,bodyFrequency=55,bodyDecay=0.45,bodyLevel=0.95,noiseLevel=0.02,noiseDecay=0.025,noiseHighpass=500,noiseLowpass=4000,drumPitchAmount=30,drumPitchDecay=0.022,drumClick=0.12,drumDrive=1.6,polyphony=4}},
    {id='b:snare',name='Noise Snare',state={drumType=2,bodyFrequency=190,bodyDecay=0.12,bodyLevel=0.55,noiseLevel=0.85,noiseDecay=0.24,noiseHighpass=1100,noiseLowpass=12000,drumPitchAmount=7,drumPitchDecay=0.018,drumClick=0.2,drumDrive=1.8,polyphony=4}},
    {id='b:hat',name='Closed Hat',state={drumType=3,bodyFrequency=420,bodyDecay=0.04,bodyLevel=0.18,noiseLevel=0.7,noiseDecay=0.065,noiseHighpass=6500,noiseLowpass=16000,drumPitchAmount=0,drumClick=0.02,drumDrive=1,volume=0.5,polyphony=4}},
    {id='b:openhat',name='Open Hat',state={drumType=4,bodyFrequency=420,bodyDecay=0.35,bodyLevel=0.18,noiseLevel=0.7,noiseDecay=0.65,noiseHighpass=6500,noiseLowpass=16000,drumPitchAmount=0,drumClick=0.02,drumDrive=1,volume=0.5,polyphony=4}},
}
-- Stable IDs and snapshots keep bank updates from changing saved projects.
-- Tags are catalog metadata only; the parameter validator stays strictly numeric.
M.banks={'All','Guitar','Rock / Metal','Blues / Funk / Jazz','House / Techno / Trance',
    'Hip-hop / Trap / Lo-fi','DnB / Dubstep','Pop / Synthwave / Disco','Ambient / Cinematic','Chiptune','Orchestral / Cinematic','Acoustic / Classical','Drum Kit','Experimental / Industrial','Personal'}
local function add(id,name,role,bank,state)
    M.builtins[#M.builtins+1]={id='b:'..id,name=name,role=role,bank=bank,state=state}
end
local function guitar(id,name,bank,driveType,drive,cabinet,mute,damping)
    add(id,name,'Guitar',bank,{instrumentType=1,stringDecay=4,stringDamping=damping or 0.3,
        stringMute=mute or 0,stringPick=0.18,attack=0.002,decay=0.2,sustain=1,release=0.09,
        cutoff=14000,driveType=driveType,drive=drive,cabinet=cabinet,driveTone=9000,
        ampBass=-2,ampMid=1,ampTreble=-1,volume=driveType==0 and 0.8 or (driveType==1 and (drive<8 and 0.8 or 0.6) or 0.55),polyphony=8})
end
guitar('guitar-clean','Clean Electric','Blues / Funk / Jazz',0,0,1,0,0.22)
guitar('guitar-warm','Warm Jazz Guitar','Blues / Funk / Jazz',1,3,1,0,0.7)
guitar('guitar-muted','Muted Funk Guitar','Blues / Funk / Jazz',1,6,2,0.65,0.35)
guitar('guitar-blues','Blues Breakup','Blues / Funk / Jazz',1,13,2,0,0.28)
guitar('guitar-crunch','Crunch Rhythm','Rock / Metal',1,22,2,0,0.25)
guitar('guitar-metal','Metal Palm Mute','Rock / Metal',2,32,3,0.7,0.18)
guitar('guitar-lead','Singing Guitar Lead','Rock / Metal',2,26,3,0,0.2)
guitar('guitar-fuzz','Garage Fuzz','Rock / Metal',3,24,2,0,0.3)
add('rock-bass','Driven Rock Bass','Bass','Rock / Metal',{wave1=2,tune1=-12,cutoff=1600,filterAmount=1.5,attack=0.003,decay=0.3,sustain=0.7,release=0.08,driveType=1,drive=18,driveTone=5000,cabinet=2,ampBass=3,volume=0.45,polyphony=8})
add('jazz-bass','Round Finger Bass','Bass','Blues / Funk / Jazz',{instrumentType=1,tune1=-12,stringDecay=3,stringDamping=0.65,stringPick=0.3,attack=0.003,sustain=1,release=0.1,cutoff=1800,volume=0.95,fxOutput=1.8})
add('house-bass','House Sub Bass','Bass','House / Techno / Trance',{wave1=0,tune1=-12,enabled2=1,wave2=1,tune2=-12,volume2=0.2,attack=0.004,decay=0.18,sustain=0.6,release=0.08,cutoff=1000,volume=0.85})
add('acid-bass','Acid Bass','Bass','House / Techno / Trance',{wave1=2,tune1=-12,attack=0.002,decay=0.18,sustain=0.15,release=0.06,cutoff=220,resonance=0.75,filterAmount=4,filterDecay=0.18,driveType=1,drive=14,driveTone=7000})
add('trance-pluck','Trance Pluck','Pluck','House / Techno / Trance',{wave1=2,enabled2=1,wave2=2,fine2=14,attack=0.002,decay=0.35,sustain=0,release=0.18,cutoff=1000,filterAmount=4,filterDecay=0.22})
add('trance-lead','Bright Trance Lead','Lead','House / Techno / Trance',{wave1=2,enabled2=1,wave2=2,fine2=18,volume2=0.65,attack=0.01,decay=0.2,sustain=0.85,release=0.15,cutoff=7000,driveType=1,drive=8,fxMix=0.3})
add('trap-808','Long 808 Bass','Bass','Hip-hop / Trap / Lo-fi',{wave1=0,tune1=-12,attack=0.003,decay=1.8,sustain=0,release=0.15,pitchAmount=12,pitchDecay=0.02,cutoff=1200,driveType=1,drive=16,driveTone=5000})
add('lofi-keys','Mellow Lo-fi Keys','Keys','Hip-hop / Trap / Lo-fi',{wave1=1,enabled2=1,wave2=0,tune2=12,volume2=0.18,attack=0.005,decay=1.2,sustain=0.1,release=0.3,cutoff=1600,driveType=1,drive=6,driveTone=4000,fxMix=0.35})
add('reese-bass','Reese Bass','Bass','DnB / Dubstep',{wave1=2,tune1=-12,enabled2=1,wave2=2,tune2=-12,fine2=22,volume2=0.75,attack=0.01,decay=0.2,sustain=0.8,release=0.12,cutoff=1600,driveType=2,drive=20,driveTone=5500,volume=0.4})
add('wobble-bass','Wobble Bass','Bass','DnB / Dubstep',{wave1=3,tune1=-12,enabled2=1,wave2=2,tune2=-12,volume2=0.6,cutoff=350,resonance=0.5,lfoTarget=1,lfoRate=2,lfoDepth=0.7,driveType=2,drive=24,driveTone=6500,volume=0.36})
add('synth-brass','Synth Brass','Keys','Pop / Synthwave / Disco',{wave1=2,enabled2=1,wave2=2,fine2=9,volume2=0.5,attack=0.035,decay=0.3,sustain=0.65,release=0.18,cutoff=1800,filterAmount=2,filterAttack=0.02,filterDecay=0.3})
add('disco-bass','Disco Pulse Bass','Bass','Pop / Synthwave / Disco',{wave1=3,tune1=-12,attack=0.003,decay=0.16,sustain=0.25,release=0.07,cutoff=700,filterAmount=2.5,filterDecay=0.12,resonance=0.2})
add('synthwave-pad','Synthwave Pad','Pad','Pop / Synthwave / Disco',{wave1=2,enabled2=1,wave2=2,fine2=15,volume2=0.65,attack=0.6,release=1.8,cutoff=2200,lfoTarget=1,lfoDepth=0.15,lfoRate=0.4})
add('ambient-pad','Slow Moving Pad','Pad','Ambient / Cinematic',{wave1=1,enabled2=1,wave2=2,tune2=12,volume2=0.2,attack=1.4,decay=1,sustain=0.75,release=3.5,cutoff=1200,lfoTarget=1,lfoDepth=0.35,lfoRate=0.12})
add('dark-drone','Dark Drone','Drone','Ambient / Cinematic',{wave1=2,tune1=-12,enabled2=1,wave2=3,tune2=-24,volume2=0.35,attack=1.8,release=4,cutoff=300,resonance=0.25,lfoTarget=1,lfoRate=0.08,lfoDepth=0.3,driveType=1,drive=8,fxMix=0.2})
add('glass-bell','Glass Bell','Bell','Ambient / Cinematic',{wave1=0,enabled2=1,wave2=0,tune2=19,volume2=0.3,attack=0.002,decay=2,sustain=0,release=1.2,cutoff=12000})
add('chip-lead','Chip Pulse Lead','Lead','Chiptune',{wave1=3,attack=0.002,decay=0.07,sustain=0.7,release=0.02,cutoff=14000,volume=0.5})
add('chip-bass','Chip Triangle Bass','Bass','Chiptune',{wave1=1,tune1=-12,attack=0.002,decay=0.08,sustain=0.7,release=0.02,cutoff=8000})
add('chip-snare','Chip Noise Snare','Drums','Chiptune',{wave1=4,attack=0.002,decay=0.08,sustain=0,release=0.01,cutoff=7500,volume=0.5})
local function drum(id,name,bank,base,changes)
    local state={};for k,v in pairs(M.builtins[base].state) do state[k]=v end
    for k,v in pairs(changes) do state[k]=v end
    add(id,name,'Drums',bank,state)
end
drum('rock-kick','Punchy Rock Kick','Rock / Metal',6,{bodyFrequency=62,bodyDecay=0.3,drumClick=0.3,noiseLevel=0.08,drumDrive=2})
drum('rock-snare','Rock Snare','Rock / Metal',7,{bodyFrequency=210,bodyDecay=0.14,noiseDecay=0.21,drumDrive=2.4})
drum('house-kick','Four Floor Kick','House / Techno / Trance',6,{bodyFrequency=50,bodyDecay=0.38,drumClick=0.1,drumDrive=1.8})
drum('house-snare','House Snare','House / Techno / Trance',7,{bodyFrequency=180,noiseDecay=0.16,noiseHighpass=1800})
drum('trap-kick','Short Trap Kick','Hip-hop / Trap / Lo-fi',6,{bodyFrequency=58,bodyDecay=0.16,drumClick=0.2})
drum('trap-snare','Trap Snare','Hip-hop / Trap / Lo-fi',7,{bodyFrequency=240,bodyDecay=0.07,noiseDecay=0.12,noiseHighpass=1800})
drum('trap-hat','Tight Trap Hat','Hip-hop / Trap / Lo-fi',8,{noiseDecay=0.035,bodyDecay=0.02,volume=0.4})
drum('dnb-kick','Tight DnB Kick','DnB / Dubstep',6,{bodyFrequency=65,bodyDecay=0.2,drumClick=0.25})
drum('dnb-snare','Cracking DnB Snare','DnB / Dubstep',7,{bodyFrequency=225,bodyDecay=0.1,noiseDecay=0.17,noiseLevel=0.95,drumDrive=2.5})
add('studio-keys','Studio Keys','Keys','General',{wave1=1,enabled2=1,wave2=0,tune2=12,volume2=0.15,attack=0.003,decay=0.9,sustain=0.16,release=0.18,cutoff=5000,volume=0.65})
add('studio-strings','Fast String Pad','Pad','General',{wave1=2,enabled2=1,wave2=1,fine2=8,volume2=0.4,attack=0.035,decay=0.3,sustain=0.72,release=0.16,cutoff=2600,volume=0.55})
add('studio-organ','Drawbar-like Organ','Keys','General',{wave1=0,enabled2=1,wave2=0,tune2=12,volume2=0.45,attack=0.006,decay=0.1,sustain=0.9,release=0.08,cutoff=9000,volume=0.6})
add('studio-brass','Soft Synth Brass','Keys','General',{wave1=2,enabled2=1,wave2=3,volume2=0.2,attack=0.018,decay=0.2,sustain=0.65,release=0.1,cutoff=2200,filterAmount=1.3,filterDecay=0.2,volume=0.55})
add('studio-reed','Mellow Synth Reed','Lead','General',{wave1=1,enabled2=1,wave2=0,tune2=12,volume2=0.14,attack=0.008,decay=0.2,sustain=0.8,release=0.1,cutoff=3600,resonance=0.12,lfoDepth=0.02,volume=0.65})
drum('studio-tom','Synth Tom','General',6,{bodyFrequency=130,bodyDecay=0.28,bodyLevel=0.8,noiseLevel=0.025,noiseDecay=0.06,drumPitchAmount=3,drumPitchDecay=0.025,drumDrive=1.3,drumClick=0.1,volume=0.6})
add('orchestra-violin','Bowed Violin','Strings','Orchestral / Cinematic',{wave1=2,enabled2=1,wave2=2,fine2=4,volume2=.22,attack=.065,decay=.25,sustain=.76,release=.18,cutoff=3300,resonance=.08,filterAmount=.3,lfoRate=5.5,lfoDepth=.025,volume=.46})
add('orchestra-cello','Bowed Cello','Strings','Orchestral / Cinematic',{wave1=2,enabled2=1,wave2=1,volume2=.35,attack=.09,decay=.3,sustain=.78,release=.22,cutoff=1900,resonance=.1,lfoRate=5,lfoDepth=.018,volume=.52})
add('orchestra-trumpet','Orchestral Trumpet','Brass','Orchestral / Cinematic',{wave1=2,enabled2=1,wave2=3,volume2=.15,attack=.028,decay=.2,sustain=.72,release=.09,cutoff=2400,resonance=.14,filterAmount=1.1,filterAttack=.05,filterDecay=.18,filterSustain=.2,volume=.45})
add('orchestra-flute','Breathy Flute','Woodwind','Orchestral / Cinematic',{wave1=0,enabled2=1,wave2=4,volume2=.035,attack=.055,decay=.2,sustain=.82,release=.12,cutoff=4200,lfoDepth=.018,lfoRate=5.2,volume=.7})
add('orchestra-clarinet','Orchestral Clarinet','Woodwind','Orchestral / Cinematic',{wave1=3,enabled2=1,wave2=0,volume2=.16,attack=.025,decay=.2,sustain=.72,release=.1,cutoff=2300,resonance=.08,lfoDepth=.014,lfoRate=5,volume=.42})
add('orchestra-timpani','Tuned Timpani','Percussion','Orchestral / Cinematic',{wave1=0,enabled2=1,wave2=0,tune2=7,volume2=.3,attack=.002,decay=1.2,sustain=0,release=.8,pitchAmount=2,pitchDecay=.015,cutoff=2000,volume=.75,polyphony=6})
-- First band-bank pass uses the existing native voices. Stable IDs preserve
-- saved snapshots when later physical modelling or multisample banks arrive.
local function string(id,name,role,bank,program,changes)
    local state={instrumentType=1,stringDecay=3,stringDamping=.3,stringMute=0,stringPick=.23,
        attack=.002,decay=.2,sustain=1,release=.12,cutoff=12000,volume=.7,polyphony=8}
    for k,v in pairs(changes) do state[k]=v end
    add(id,name,role,bank,state);M.builtins[#M.builtins].midiProgram=program
end
string('guitar-nylon','Classical Nylon','Guitar','Acoustic / Classical',24,{stringDamping=.64,stringPick=.3,stringDecay=2.8,cutoff=6200,release=.18,volume=.9})
string('guitar-steel','Acoustic Steel','Guitar','Acoustic / Classical',25,{stringDamping=.16,stringPick=.17,stringDecay=4.8,cutoff=14000,release=.22,volume=.7})
string('guitar-funk','Clean Funk Chops','Guitar','Blues / Funk / Jazz',28,{stringMute=.74,stringDamping=.35,stringPick=.14,stringDecay=1.6,cutoff=9000,cabinet=1,release=.035,volume=.9})
string('guitar-heavy','Classic Heavy Rhythm','Guitar','Rock / Metal',29,{stringPick=.21,stringDamping=.32,driveType=3,drive=18,cabinet=2,ampMid=3,ampBass=1,ampTreble=-3,driveTone=6500,release=.1,volume=.48})
string('guitar-thrash','Tight Thrash Rhythm','Guitar','Rock / Metal',30,{stringPick=.12,stringMute=.6,stringDamping=.2,driveType=2,drive=29,cabinet=3,ampBass=-3,ampMid=-2,ampTreble=1,driveTone=8500,release=.045,volume=.5})
string('guitar-altmetal','Percussive Metal Rhythm','Guitar','Rock / Metal',30,{stringPick=.16,stringMute=.45,stringDamping=.26,driveType=2,drive=25,cabinet=3,ampBass=-1,ampMid=3,ampTreble=-1,driveTone=7500,release=.055,volume=.52})
string('bass-acoustic','Acoustic Bass Pluck','Bass','Acoustic / Classical',32,{tune1=-12,stringDamping=.72,stringPick=.34,stringDecay=2.1,cutoff=2200,release=.12,volume=.95})
string('bass-finger','Electric Finger Bass','Bass','Blues / Funk / Jazz',33,{tune1=-12,stringDamping=.5,stringPick=.3,stringDecay=2.8,cutoff=3800,release=.1,volume=.9})
string('bass-picked','Picked Electric Bass','Bass','Rock / Metal',34,{tune1=-12,stringDamping=.25,stringPick=.13,stringDecay=2.2,cutoff=6000,driveType=1,drive=7,fxMix=.35,release=.07,volume=.7})
string('bass-slap','Slap Bass','Bass','Blues / Funk / Jazz',36,{tune1=-12,stringDamping=.2,stringPick=.08,stringDecay=1.2,cutoff=11000,attack=.002,release=.06,ampBass=2,ampMid=-2,volume=.8})
string('bass-pop','Bright Pop Bass','Bass','Blues / Funk / Jazz',37,{tune1=-12,stringDamping=.1,stringPick=.1,stringDecay=1.7,cutoff=14000,release=.08,volume=.75})
drum('kit-kick','Acoustic Kit Kick','Drum Kit',6,{bodyFrequency=52,bodyDecay=.26,noiseLevel=.09,noiseDecay=.025,drumClick=.18,drumPitchAmount=19,drumDrive=1.5,volume=.72})
drum('kit-snare','Acoustic Kit Snare','Drum Kit',7,{bodyFrequency=185,bodyDecay=.13,noiseLevel=.85,noiseDecay=.19,noiseHighpass=950,drumDrive=1.5,volume=.65})
drum('kit-rim','Cross Stick / Rim','Drum Kit',7,{bodyFrequency=650,bodyDecay=.025,bodyLevel=.7,noiseLevel=.12,noiseDecay=.018,noiseHighpass=2200,drumClick=.28,drumPitchAmount=0,volume=.6})
drum('kit-clap','Hand Clap','Drum Kit',7,{bodyLevel=.02,noiseLevel=.95,noiseDecay=.11,noiseHighpass=1200,noiseLowpass=9000,drumPitchAmount=0,drumClick=.08,volume=.55})
drum('kit-pedalhat','Pedal Hi-hat','Drum Kit',8,{bodyFrequency=470,bodyDecay=.045,noiseDecay=.04,noiseHighpass=5000,drumClick=.05,volume=.46})
drum('kit-crash','Crash Cymbal','Drum Kit',9,{bodyFrequency=650,bodyDecay=1.1,bodyLevel=.35,noiseDecay=1.7,noiseHighpass=3200,noiseLowpass=16000,noiseLevel=.8,drumClick=.09,drumDrive=1.1,volume=.42})
drum('kit-ride','Ride Cymbal','Drum Kit',9,{bodyFrequency=930,bodyDecay=.8,bodyLevel=.6,noiseDecay=.65,noiseLevel=.22,noiseHighpass=2700,noiseLowpass=14000,drumClick=.18,drumDrive=1.05,volume=.48})
drum('kit-ridebell','Ride Bell','Drum Kit',9,{bodyFrequency=1000,bodyDecay=.55,bodyLevel=.85,noiseDecay=.15,noiseLevel=.06,noiseHighpass=1800,drumClick=.22,volume=.5})
drum('kit-cowbell','Cowbell','Drum Kit',8,{bodyFrequency=540,bodyDecay=.12,bodyLevel=.9,noiseLevel=.015,noiseDecay=.01,noiseHighpass=500,noiseLowpass=7000,drumClick=.12,volume=.5})
drum('kit-tambourine','Tambourine','Drum Kit',8,{bodyFrequency=820,bodyDecay=.13,bodyLevel=.2,noiseDecay=.17,noiseLevel=.8,noiseHighpass=5500,drumClick=.08,volume=.48})
drum('kit-shaker','Shaker / Maracas','Drum Kit',8,{bodyLevel=0,noiseLevel=.85,noiseDecay=.07,noiseHighpass=3500,noiseLowpass=12000,drumClick=.015,volume=.45})
drum('kit-percussion','Other Percussion','Drum Kit',6,{bodyFrequency=380,bodyDecay=.09,bodyLevel=.55,noiseLevel=.22,noiseDecay=.045,drumPitchAmount=1,drumClick=.12,volume=.5})
drum('metal-kick','Metal Kick','Rock / Metal',6,{bodyFrequency=58,bodyDecay=.15,noiseLevel=.13,noiseDecay=.018,drumClick=.4,drumPitchAmount=16,drumDrive=1.8,volume=.65})
drum('metal-snare','Metal Snare','Rock / Metal',7,{bodyFrequency=230,bodyDecay=.1,noiseLevel=.9,noiseDecay=.14,drumClick=.22,drumDrive=2,volume=.58})
drum('funk-kick','Dry Funk Kick','Blues / Funk / Jazz',6,{bodyFrequency=65,bodyDecay=.16,noiseLevel=.025,drumClick=.16,drumDrive=1.4,volume=.72})
drum('funk-snare','Dry Funk Snare','Blues / Funk / Jazz',7,{bodyFrequency=195,bodyDecay=.075,noiseDecay=.105,noiseLevel=.75,drumDrive=1.4,volume=.65})
-- Recorded FreePats DI source; the same immutable bank feeds distinct amps.
M.banks[#M.banks+1]='CC Sampled Instruments'
M.sampledStrings={['sound/fetcher/banks/electric-di-v1.fsb']=true}
for _,entry in ipairs{{'sampled-electric-clean','Sampled Electric Clean',0,0,1,.7,27},
    {'sampled-electric-crunch','Sampled Electric Crunch',1,22,2,.6,29},
    {'sampled-electric-metal','Sampled Electric Metal',2,29,3,.5,30}} do
    add(entry[1],entry[2],'Guitar','CC Sampled Instruments',{attack=.002,decay=.2,sustain=1,release=.12,
        cutoff=16000,driveType=entry[3],drive=entry[4],cabinet=entry[5],volume=entry[6],polyphony=8,
        sample={path='sound/fetcher/banks/electric-di-v1.fsb',offset=0,duration=1,root=60,oneShot=false,hash='969e921873764a7fb7a112d9574d3dfe'}})
    M.builtins[#M.builtins].midiProgram=entry[7]
end
for _,entry in ipairs{
    {'sampled-bass-finger','Sampled Finger Bass','Bass','bass-finger-v1','39efa33c8b8f5daf6ac4d7dd976b2f8a',33},
    {'sampled-bass-picked','Sampled Picked Bass','Bass','bass-picked-v1','b7f0d9845a771ae849ba6a4eea3665da',34},
    {'sampled-bass-short','Sampled Short Finger Bass','Bass','bass-short-v1','f6bc8a20d8dba535d86ddae52879548b',33},
    {'sampled-bass-ghost','Sampled Ghost Finger Bass','Bass','bass-ghost-v1','3209879caee5a143b3f8e6e5906b1245',33},
    {'sampled-guitar-nylon','Sampled Classical Nylon','Guitar','guitar-nylon-v1','cd9934832ec1764a29047688618930b5',24},
    {'sampled-guitar-steel','Sampled Acoustic Steel','Guitar','guitar-steel-v1','cdbfbd10733a5c4e6a8a3eb567ef59b8',25},
    {'sampled-drum-kit','Sampled Acoustic Drum Kit','Drums','drum-kit-v1','57fde34599e1a0562d255490162fecc2'},
} do
    add(entry[1],entry[2],entry[3],'CC Sampled Instruments',{attack=.002,decay=.2,sustain=1,release=.12,
        cutoff=18000,volume=entry[4]=='guitar-nylon-v1' and .9 or .8,polyphony=8,
        gain=entry[3]=='Guitar' and 2 or 1.25,fxOutput=entry[4]=='guitar-nylon-v1' and 2 or (entry[4]=='guitar-steel-v1' and 1.5 or 1),
        sample={path='sound/fetcher/banks/'..entry[4]..'.fsb',offset=0,duration=1,root=60,oneShot=false,hash=entry[5]}})
    M.builtins[#M.builtins].midiProgram=entry[6]
    if entry[3]~='Drums' then M.sampledStrings['sound/fetcher/banks/'..entry[4]..'.fsb']=true end
end
for _,entry in ipairs{
    {'sampled-electric-muted','Sampled Tight Metal Rhythm',2,30,3,.8,.8,.5,.015,.5,28},
    {'sampled-rock-muted','Sampled Muted Rock Rhythm',1,22,2,.5,1.1,.3,.025,.6,28},
    {'sampled-funk-muted','Sampled Muted Funk Guitar',0,0,1,.6,.75,.55,.025,.85,28},
    {'sampled-electric-lead','Sampled Electric Lead',2,25,2,0,3,.35,.2,.5,30},
} do
    add(entry[1],entry[2],'Guitar','CC Sampled Instruments',{attack=.002,decay=.2,sustain=1,release=entry[9],
        cutoff=16000,driveType=entry[3],drive=entry[4],cabinet=entry[5],stringMute=entry[6],
        stringDecay=entry[7],stringDamping=entry[8],volume=entry[10],polyphony=8,
        sample={path='sound/fetcher/banks/electric-di-v1.fsb',offset=0,duration=1,root=60,oneShot=false,hash='969e921873764a7fb7a112d9574d3dfe'}})
    M.builtins[#M.builtins].midiProgram=entry[11]
end
-- Recorded patches need their own gain staging: sustained distorted DI masks
-- the short acoustic drum transients when both use the old generic defaults.
local recordedBalance={
    ['sampled-drum-kit']={volume=.95,gain=1.7,fxOutput=1.5},
    ['sampled-electric-clean']={volume=.62,cabinet=6,preHighpass=70,cabinetHighpass=60,cabinetLowpass=10000},
    ['sampled-electric-crunch']={volume=.46,drive=20,driveTone=12000,ampBass=-2,ampMid=1,ampTreble=1,release=.055,cabinet=6,preHighpass=90,cabinetHighpass=70,cabinetLowpass=10000},
    ['sampled-electric-metal']={volume=.40,drive=24,driveTone=12000,ampBass=-3,ampMid=-2,ampTreble=2,release=.035,cabinet=4,preHighpass=100,cabinetHighpass=70,cabinetLowpass=9000},
    ['sampled-electric-muted']={volume=.40,drive=25,driveTone=12000,ampBass=-3,ampMid=-2,ampTreble=2,stringMute=.7,stringDamping=.22,release=.015,cabinet=4,preHighpass=100,cabinetHighpass=70,cabinetLowpass=9000},
    ['sampled-rock-muted']={volume=.46,drive=20,driveTone=12000,ampBass=-2,ampMid=1,stringDamping=.22,cabinet=6,preHighpass=90,cabinetHighpass=70,cabinetLowpass=10000},
    ['sampled-electric-lead']={volume=.43,drive=24,driveTone=12000,ampBass=-3,ampMid=3,ampTreble=1,release=.16,cabinet=5,preHighpass=90,cabinetHighpass=70,cabinetLowpass=10000},
}
for _,preset in ipairs(M.builtins) do
    for key,value in pairs(recordedBalance[preset.id:sub(3)] or {}) do preset.state[key]=value end
end
add('sampled-thrash-rhythm','Sampled Thrash Rhythm','Guitar','CC Sampled Instruments',{
    attack=.002,decay=.2,sustain=1,release=.018,cutoff=16000,driveType=2,drive=24,driveTone=12000,
    cabinet=4,preHighpass=110,cabinetHighpass=70,cabinetLowpass=9000,ampBass=-3,ampMid=-3,ampTreble=3,stringMute=.32,stringDecay=1.6,stringDamping=.18,
    volume=.34,polyphony=8,sample={path='sound/fetcher/banks/electric-di-v1.fsb',offset=0,duration=1,root=60,
        oneShot=false,hash='969e921873764a7fb7a112d9574d3dfe'}})
M.builtins[#M.builtins].midiProgram=30
local expressions={
    ['sampled-electric-clean']={.07,.005,.12,.04,.20,0},
    ['sampled-electric-crunch']={.12,.004,.18,.08,.25,0},
    ['sampled-electric-metal']={.16,.0035,.20,.12,.25,0},
    ['sampled-electric-muted']={.18,.003,.12,.08,.30,2},
    ['sampled-rock-muted']={.14,.004,.18,.06,.30,2},
    ['sampled-funk-muted']={.12,.003,.12,.03,.50,2},
    ['sampled-electric-lead']={.08,.005,.22,.04,.15,0},
    ['sampled-thrash-rhythm']={.20,.003,.18,.12,.35,0},
    ['guitar-clean']={.20,.005,.22,.05,.20,0},
    ['guitar-crunch']={.25,.004,.24,.10,.30,0},
    ['guitar-heavy']={.28,.004,.22,.10,.30,0},
    ['guitar-thrash']={.30,.003,.25,.12,.35,0},
    ['guitar-altmetal']={.27,.004,.23,.10,.30,0},
    ['guitar-funk']={.22,.003,.18,.04,.50,2},
    ['guitar-lead']={.18,.005,.28,.05,.20,0},
    ['sampled-bass-picked']={.14,.004,.10,.03,.25,0},
    ['sampled-bass-finger']={.035,.006,.10,.02,.20,0},
    ['sampled-guitar-nylon']={.055,.006,.14,.02,.25,0},
    ['sampled-guitar-steel']={.10,.004,.18,.04,.30,0},
}
for _,preset in ipairs(M.builtins) do
    local e=expressions[preset.id:sub(3)]
    if e then
        local s=preset.state;s.pickAttack=e[1];s.pickDecay=e[2];s.stringResonance=e[3];s.pickPitch=e[4]
        s.pickVariation=e[5];s.stringArticulation=e[6];s.pickDirection=preset.role=='Bass' and 0 or 2
    end
end
-- Opt-in experimental patches; no conventional band sound set selects these.
for _,entry in ipairs{
    {'captured-guitar-clean','Captured Humbucker Clean',0,0,6,.55,0,.10,27},
    {'captured-guitar-rock','Captured Humbucker Rock',1,18,6,.42,0,.045,29},
    {'captured-guitar-rock-muted','Captured Humbucker Muted Rock',1,18,6,.42,2,.025,28},
    {'captured-guitar-thrash','Captured Humbucker Thrash',2,22,4,.38,2,.018,30},
    {'captured-guitar-funk','Captured Humbucker Funk',0,0,6,.6,2,.025,28},
    {'captured-guitar-lead','Captured Humbucker Lead',2,20,5,.4,1,.16,31},
} do
    add(entry[1],entry[2],'Guitar','CC Sampled Instruments',{attack=.002,sustain=1,release=entry[8],cutoff=16000,
        driveType=entry[3],drive=entry[4],driveTone=12000,cabinet=entry[5],volume=entry[6],stringArticulation=entry[7],
        polyphony=8,preHighpass=entry[3]==0 and 65 or 100,cabinetHighpass=65,cabinetLowpass=9500,
        ampBass=entry[3]==0 and 0 or -3,ampMid=entry[3]==2 and -1 or 1,ampTreble=entry[3]==2 and 2 or 0,
        sample={path='sound/fetcher/banks/guitar-techs-open-v1.fsb',offset=0,duration=1,root=60,oneShot=false,hash='4f971050d335c3b9a553d1904e57096f',
            attacks={short={path='sound/fetcher/banks/guitar-techs-palm-v1.fsb',offset=0,duration=1,root=60,oneShot=false,hash='a57077f8d89a1b9f71c29c952e12bafa'}}}})
    M.builtins[#M.builtins].midiProgram=entry[9]
end
M.sampledStrings['sound/fetcher/banks/guitar-techs-open-v1.fsb']=true
M.sampledStrings['sound/fetcher/banks/guitar-techs-bridge-open-v1.fsb']=true
-- Bridge DI/input calibration selected after a guitar-level-matched listening
-- comparison. Its genre/channel EQ is applied by soundsets.apply, separately
-- from the player's editable instrument/amp preset.
for _,preset in ipairs(M.builtins) do if preset.id=='b:captured-guitar-thrash' then
    preset.name='Captured Bridge Thrash'
    local s=preset.state
    s.drive=36;s.ampBass=-6;s.ampMid=6;s.ampTreble=8;s.preHighpass=130
    s.cabinetHighpass=100;s.cabinetLowpass=10000;s.fxOutput=1.219
    s.sample={path='sound/fetcher/banks/guitar-techs-bridge-open-v1.fsb',offset=0,duration=1,root=60,oneShot=false,hash='909a30a8d7715bef9ef6e59f530f712c',gain=8,
        attacks={short={path='sound/fetcher/banks/guitar-techs-bridge-palm-v1.fsb',offset=0,duration=1,root=60,oneShot=false,hash='955f9829af20fca5b6975a9b68c47363',gain=8}}}
end end

do
    local base;for _,preset in ipairs(M.builtins) do if preset.id=='b:sampled-bass-finger' then base=preset end end
    local state=assert(M.state(assert(base).state))
    state.sample.attacks={
        short={path='sound/fetcher/banks/bass-short-v1.fsb',offset=0,duration=1,root=60,oneShot=false,hash='f6bc8a20d8dba535d86ddae52879548b'},
        dead={path='sound/fetcher/banks/bass-ghost-v1.fsb',offset=0,duration=1,root=60,oneShot=false,hash='3209879caee5a143b3f8e6e5906b1245'},
    }
    state.pickAttack=0;state.stringResonance=0;state.pickPitch=0;state.pickVariation=0
    add('sampled-bass-expressive','Sampled Expressive Finger Bass','Bass','CC Sampled Instruments',state)
    M.builtins[#M.builtins].midiProgram=33
end
-- Curated band candidates use new IDs: existing personal/project snapshots and
-- the reviewed bridge-thrash recipe retain their identities.
do
    local function bank(id,hash,gain)
        return {path='sound/fetcher/banks/'..id..'.fsb',offset=0,duration=1,root=60,oneShot=false,hash=hash,gain=gain}
    end
    local slap=bank('bass-slap-v1','1bad74c58007a5d2791a13688cedb054')
    slap.attacks={short=bank('bass-slap-short-v1','3673df85f26d54fbc8fd9877d35e1a6f'),dead=bank('bass-thump-v1','bbfea9b7c59069e590a3356b74349849')}
    local pop=bank('bass-pop-v1','4b06701f0d9fc1d2f7e9d31c92f840d0')
    pop.attacks={short=bank('bass-pop-short-v1','6ca6d9e589f7de8d19400c0187dd9e92'),dead=bank('bass-thump-v1','bbfea9b7c59069e590a3356b74349849')}
    for _,e in ipairs{{'sampled-bass-slap','Recorded Thumb Slap Bass',slap,36},
        {'sampled-bass-pop','Recorded Popped Bass',pop,37}} do
        e[3].gain=3;for _,ref in pairs(e[3].attacks) do ref.gain=3 end
        add(e[1],e[2],'Bass','Band / Curated',{sample=e[3],volume=.72,gain=1,attack=.002,sustain=1,
            release=.11,cutoff=16000,preHighpass=32,ampBass=1,ampMid=-2,ampTreble=2,polyphony=8})
        M.builtins[#M.builtins].midiProgram=e[4];M.sampledStrings[e[3].path]=true
    end
    local function derive(id,name,base,changes,inputGain)
        local source;for _,p in ipairs(M.builtins) do if p.id=='b:'..base then source=p end end
        local state=assert(M.state(assert(source).state))
        for k,v in pairs(changes) do state[k]=v end
        if inputGain then state.sample.gain=inputGain;for _,ref in pairs(state.sample.attacks or {}) do ref.gain=inputGain end end
        add(id,name,source.role,'Band / Curated',state);M.builtins[#M.builtins].midiProgram=source.midiProgram
    end
    derive('band-doom-guitar','Doom / Neck Fuzz','captured-guitar-rock',{
        driveType=3,drive=26,ampBass=-3,ampMid=4,ampTreble=-2,cabinet=6,
        preHighpass=80,cabinetHighpass=65,cabinetLowpass=6500,release=.10,volume=.38,fxOutput=.5935},6)
    derive('band-modern-guitar','Modern / Tight Bridge','captured-guitar-thrash',{
        drive=30,ampBass=-5,ampMid=2,ampTreble=5,cabinet=5,preHighpass=145,
        cabinetHighpass=105,cabinetLowpass=8500,release=.016,volume=.38,fxOutput=.7038},8)
    derive('band-alternative-guitar','Alternative / Mid Crunch','captured-guitar-rock',{
        driveType=1,drive=29,ampBass=-4,ampMid=5,ampTreble=3,cabinet=4,preHighpass=115,
        cabinetHighpass=85,cabinetLowpass=9000,release=.045,volume=.38,fxOutput=.651},6)
    derive('band-thrash-ref-guitar','Thrash / Scooped Staged Amp','captured-guitar-thrash',{
        driveType=4,drive=30,ampBass=-12,ampMid=6,ampTreble=4,cabinet=4,preHighpass=65,
        cabinetHighpass=65,cabinetLowpass=7000,release=.012,volume=.38,fxOutput=.9,stringArticulation=0},6)
    derive('band-thrash-ref-muted','Thrash / Captured Palm Staged Amp','band-thrash-ref-guitar',{stringArticulation=2})
    derive('band-industrial-guitar','Industrial / Parallel Staged Amp','captured-guitar-thrash',{
        driveType=4,drive=30,ampBass=-8,ampMid=4,ampTreble=5,cabinet=5,preHighpass=55,
        cabinetHighpass=50,cabinetLowpass=8000,release=.012,volume=.38,fxOutput=1.35,stringLayout=4,
        parallelMix=.65,parallelClean=.2,parallelDirty=.8,parallelNoise=.045,parallelDrive=42,
        parallelShape=2,parallelComb=.12,parallelDelay=2.9,parallelFeedback=-.15,
        parallelDepth=.03,parallelLift=2},8)
    derive('band-industrial-bass','Industrial / Driven Pick Bass','sampled-bass-picked',{
        driveType=1,drive=12,ampBass=2,ampMid=-4,ampTreble=1,preHighpass=28,cutoff=8000,
        release=.05,volume=.65,parallelMix=.35,parallelClean=.8,parallelDirty=.25,parallelDrive=20,
        parallelShape=0,parallelComb=0,parallelLift=0})
    derive('band-clean-guitar','Clean / Warm Neck','captured-guitar-clean',{
        ampBass=-2,ampMid=0,ampTreble=3,cabinet=6,preHighpass=85,cabinetHighpass=65,
        cabinetLowpass=11500,release=.22,volume=.5,fxOutput=.717},4)
    derive('band-funk-guitar','Funk / Bridge Stabs','captured-guitar-thrash',{
        driveType=0,drive=0,ampBass=-4,ampMid=1,ampTreble=4,cabinet=6,preHighpass=150,
        cabinetHighpass=110,cabinetLowpass=12000,release=.025,volume=.5,fxOutput=1,stringArticulation=2},4)
    derive('band-blues-guitar','Blues / Edge of Breakup','captured-guitar-rock',{
        driveType=1,drive=14,ampBass=-2,ampMid=4,ampTreble=0,cabinet=6,preHighpass=80,
        cabinetHighpass=60,cabinetLowpass=8000,release=.18,volume=.46,fxOutput=.329},4)
    derive('band-jazz-guitar','Jazz / Rounded Neck','captured-guitar-clean',{
        ampBass=1,ampMid=2,ampTreble=-5,cutoff=7000,cabinet=6,preHighpass=65,
        cabinetHighpass=45,cabinetLowpass=5500,release=.26,volume=.5,fxOutput=1},4)
    derive('band-lead-guitar','Lead / Singing Neck','captured-guitar-lead',{
        drive=24,ampBass=-5,ampMid=6,ampTreble=2,cabinet=5,preHighpass=100,
        cabinetHighpass=85,cabinetLowpass=9000,release=.20,volume=.38,fxOutput=.8},6)
    derive('band-steel-guitar','Acoustic / Natural Steel','sampled-guitar-steel',{
        pickAttack=0,stringResonance=0,pickPitch=0,pickVariation=0,release=.42,cutoff=16000,
        preHighpass=75,ampBass=-2,ampMid=0,ampTreble=1,volume=.6},2.75)
    derive('band-nylon-guitar','Classical / Natural Nylon','sampled-guitar-nylon',{
        pickAttack=0,stringResonance=0,pickPitch=0,pickVariation=0,release=.5,cutoff=13000,
        preHighpass=65,ampBass=0,ampMid=1,ampTreble=-1,volume=.6},2.75)
    for _,e in ipairs{{'doom',-.5,2,-2},{'modern',-3,-1,4},{'alternative',-1,2,1},
        {'funk',-1,1,3},{'clean',0,0,0},{'blues',1,2,-3},{'acoustic',0,0,-2}} do
        derive('band-'..e[1]..'-kit','Kit / '..e[1],'sampled-drum-kit',{
            ampBass=e[2],ampMid=e[3],ampTreble=e[4],preHighpass=25,volume=.95,
            gain=e[1]=='modern' and 1.806 or 1.7,
            fxOutput=e[1]=='modern' and 2 or e[1]=='doom' and 1.626 or e[1]=='alternative' and 1.687 or 1.5},
            (e[1]=='clean' or e[1]=='blues') and 2.5 or e[1]=='acoustic' and 1.4 or nil)
    end
end
add('doom-parallel-chug','Industrial Parallel Chug','Guitar','Experimental / Industrial',{
    instrumentType=1,stringDecay=1.6,stringDamping=.25,stringMute=.65,stringArticulation=2,
    attack=.002,sustain=1,release=.06,cutoff=14000,pickAttack=.22,pickVariation=.3,
    cabinet=5,preHighpass=100,cabinetHighpass=75,cabinetLowpass=8500,fxOutput=.8,volume=.55,polyphony=8,
    parallelMix=1,parallelClean=.35,parallelDirty=.8,parallelNoise=.1,parallelDrive=32,parallelShape=2,
    parallelComb=.35,parallelDelay=3.1,parallelFeedback=.25,parallelRate=.7,parallelDepth=.12,parallelLift=6})
add('doom-parallel-saw','Industrial Parallel Saw','Synth','Experimental / Industrial',{
    wave1=2,enabled2=1,wave2=0,tune2=-12,volume2=.25,attack=.003,decay=.22,sustain=.65,release=.08,
    cutoff=7000,cabinet=4,preHighpass=85,cabinetHighpass=70,cabinetLowpass=9000,fxOutput=.75,volume=.5,polyphony=8,
    parallelMix=1,parallelClean=.3,parallelDirty=.8,parallelNoise=.16,parallelDrive=26,parallelShape=3,
    parallelComb=.55,parallelDelay=4.2,parallelFeedback=-.35,parallelRate=1.3,parallelDepth=.2,parallelLift=8})
add('doom-parallel-bass','Industrial Parallel Bass','Bass','Experimental / Industrial',{
    wave1=2,tune1=-12,enabled2=1,wave2=0,tune2=-12,volume2=.5,attack=.003,decay=.25,sustain=.8,release=.1,
    cutoff=3500,preHighpass=35,cabinetHighpass=30,cabinetLowpass=6500,fxOutput=.8,volume=.5,polyphony=8,
    parallelMix=.85,parallelClean=.65,parallelDirty=.5,parallelNoise=.04,parallelDrive=22,parallelShape=1,
    parallelComb=.18,parallelDelay=6,parallelFeedback=.2,parallelRate=.4,parallelDepth=.08,parallelLift=4})
-- Explicit mono sounds: ordinary guitar/bass sets retain polyphonic playback.
for _,entry in ipairs{{'connected-guitar-lead','Legato Electric Lead','b:guitar-lead'},
    {'connected-bass','Legato Finger Bass','b:bass-finger'},
    {'connected-sampled-lead','Legato Sampled Lead','b:sampled-electric-lead'}} do
    local base;for _,preset in ipairs(M.builtins) do if preset.id==entry[3] then base=preset;break end end
    local state={};for k,value in pairs(assert(base).state) do state[k]=value end
    state.stringLegato=1;state.stringSlideTime=.08
    add(entry[1],entry[2],base.role,base.bank,state)
end
M.banks[#M.banks+1]='Band / Captured Rig'
-- Reviewed full-rig capture, independent of the legacy clipping/cabinet modes.
-- It already includes a cabinet; paired sources are two real recorded players.
do
    for _,entry in ipairs{{'capture-heavy','Captured Rig / Heavy',0,.018,30},
        {'capture-heavy-muted','Captured Rig / Palm Mutes',2,.018,28},
        {'capture-heavy-lead','Captured Rig / Lead',1,.16,31}} do
        add(entry[1],entry[2],'Guitar','Band / Captured Rig',{
            attack=.002,decay=.2,sustain=1,release=entry[4],cutoff=18000,resonance=0,
            driveType=5,drive=12,driveTone=16000,fxMix=1,fxOutput=.556,volume=.38,
            stringArticulation=entry[3],polyphony=8,pickAttack=0,pickPitch=0,
            stringResonance=0,pickVariation=0,cabinet=0,
            sample={path='sound/fetcher/banks/guitar-techs-paired-open-v2.fsb',offset=0,duration=1,
                root=60,oneShot=false,hash='76c796b8970164868193ff017429b3e9',gain=1,
                attacks={short={path='sound/fetcher/banks/guitar-techs-paired-palm-v2.fsb',offset=0,duration=1,
                    root=60,oneShot=false,hash='eae43415c32d16cbe2b872ad10d278f2',gain=1}}}})
        M.builtins[#M.builtins].midiProgram=entry[5]
    end
    M.sampledStrings['sound/fetcher/banks/guitar-techs-paired-open-v2.fsb']=true
end

-- Dedicated recorded harmonic sounds. Opt-in only: ordinary program/sound-set
-- assignment keeps its existing picked or palm-muted bank.
M.banks[#M.banks+1]='Band / Recorded Harmonics'
for _,entry in ipairs{
    {'guitar-natural-harmonics','Recorded / Natural Harmonics','natural','a702007eefb7b94d4fbf2e7e227a1594',false},
    {'guitar-pinch-harmonics','Captured Rig / Pinch Harmonics','pinch','a62c7b59b13f83fa7142747a8165be01',true},
} do
    local path='sound/fetcher/banks/guitar-harmonics-'..entry[3]..'-v1.fsb'
    add(entry[1],entry[2],'Guitar','Band / Recorded Harmonics',{
        attack=.003,decay=.2,sustain=1,release=entry[5] and .10 or .32,cutoff=18000,resonance=0,
        driveType=entry[5] and 5 or 0,drive=entry[5] and 12 or 0,driveTone=16000,
        fxMix=entry[5] and 1 or 0,fxOutput=.556,volume=entry[5] and .32 or .5,
        stringArticulation=0,polyphony=8,pickAttack=0,pickPitch=0,stringResonance=0,pickVariation=0,cabinet=0,
        sample={path=path,offset=0,duration=1,root=60,oneShot=false,hash=entry[4],gain=entry[5] and 1 or 2.6}})
    M.sampledStrings[path]=true
end

M.banks[#M.banks+1]='Band / Performance'
do
    local function derive(id,name,base,changes)
        local original;for _,p in ipairs(M.builtins) do if p.id=='b:'..base then original=p end end
        local state=assert(M.state(assert(original).state));for k,v in pairs(changes or {}) do state[k]=v end
        add(id,name,original.role,'Band / Performance',state)
    end
    derive('perf-finger-bass','Performance / Natural Finger Bass','sampled-bass-expressive',{
        sample={path='sound/fetcher/banks/bass-finger-release-v1.fsb',offset=0,duration=1,root=60,oneShot=false,hash='c42963239914200bfcb068d03bbde668',attacks={
            short={path='sound/fetcher/banks/bass-short-v1.fsb',offset=0,duration=1,root=60,oneShot=false,hash='f6bc8a20d8dba535d86ddae52879548b'},
            dead={path='sound/fetcher/banks/bass-ghost-v1.fsb',offset=0,duration=1,root=60,oneShot=false,hash='3209879caee5a143b3f8e6e5906b1245'}}},
        stringReleaseLevel=.35,stringTouch=.25})
    derive('perf-legato-bass','Performance / Connected Finger Bass','perf-finger-bass',{
        stringLegato=1,stringLayout=6,stringSlideTime=.015,stringTouch=.35})
    derive('perf-connected-guitar','Performance / Connected Clean Guitar','band-clean-guitar',{
        sample={path='sound/fetcher/banks/guitar-green-gestures-v1.fsb',offset=0,duration=1,root=60,oneShot=false,hash='9fbacc2fc7da5ae6f3e3a65aad8be1ef',gain=4},
        stringLegato=1,stringLayout=1,stringSlideTime=.008,stringTouch=.25,stringReleaseLevel=.35,
        pickAttack=0,pickVariation=0,pickPitch=0,stringResonance=0,release=.16})
    derive('perf-connected-lead','Performance / Connected Captured Lead','perf-connected-guitar',{
        driveType=5,drive=12,fxMix=1,fxOutput=.556,volume=.38,cabinet=0,
        stringLayout=0,stringSlideTime=.008,stringTouch=.35,release=.10})
    derive('perf-thrash-rhythm','Performance / Tight Thrash','capture-heavy',{
        release=.012,preHighpass=90,cabinetHighpass=65,cabinetLowpass=10000,captureTightness=.9,volume=.5})
    derive('perf-thrash-muted','Performance / Tight Thrash Palm','perf-thrash-rhythm',{stringArticulation=2})
    derive('perf-modern-rhythm','Performance / Modern Rhythm','capture-heavy',{
        release=.015,preHighpass=135,cabinetHighpass=95,cabinetLowpass=11000,ampMid=1,volume=.5})
    derive('perf-alt-rhythm','Performance / Alternative Rhythm','capture-heavy',{
        drive=10,release=.055,preHighpass=95,cabinetHighpass=70,cabinetLowpass=9500,ampMid=2,captureTightness=.35})
    derive('perf-doom-rhythm','Performance / Heavy Doom Rhythm','capture-heavy',{
        release=.14,preHighpass=65,cabinetHighpass=50,cabinetLowpass=7200,ampBass=1,ampMid=2,captureTightness=0})
    derive('perf-industrial-rhythm','Performance / Industrial Rhythm','capture-heavy',{
        release=.012,preHighpass=55,cabinetHighpass=45,cabinetLowpass=9500,stringLayout=4,
        parallelMix=.42,parallelClean=.25,parallelDirty=.75,parallelNoise=.025,parallelDrive=32,
        parallelShape=2,parallelComb=.08,parallelDelay=2.9,parallelFeedback=-.15,parallelDepth=.03,parallelLift=3,captureTightness=.2,volume=.45})
    M.sampledStrings['sound/fetcher/banks/bass-finger-release-v1.fsb']=true
    M.sampledStrings['sound/fetcher/banks/guitar-green-gestures-v1.fsb']=true
end

for _, preset in ipairs(M.builtins) do
    preset.state = assert(M.state(preset.state),'Invalid built-in: '..preset.id)
    preset.bank=preset.bank or 'General';preset.role=preset.role or (preset.state.drumType>0 and 'Drums' or 'Synth')
end
function M.inBank(preset,bank)
    return not bank or bank=='All' or preset.bank==bank
        or (bank=='Guitar' and preset.role=='Guitar') or (bank=='Personal' and preset.id:sub(1,2)=='p:')
end
M.kits={
    {name='General',ids={'b:kick','b:snare','b:hat','b:openhat'}},
    {name='Rock',ids={'b:rock-kick','b:rock-snare','b:hat','b:openhat'}},
    {name='House',ids={'b:house-kick','b:house-snare','b:hat','b:openhat'}},
    {name='Trap',ids={'b:trap-kick','b:trap-snare','b:trap-hat','b:openhat'}},
    {name='DnB',ids={'b:dnb-kick','b:dnb-snare','b:hat','b:openhat'}},
}


local kitNotes={{'Kick',36,1},{'Snare',38,2},{'Closed hat',42,3},{'Open hat',46,4},
    {'Pedal hat',44,'b:kit-pedalhat'},{'High tom',50,'b:studio-tom'},{'Mid tom',47,'b:studio-tom'},
    {'Floor tom',43,'b:studio-tom'},{'Ride',51,'b:kit-ride'},{'Crash',49,'b:kit-crash'}}
for _,entry in ipairs{{'Full Acoustic','b:kit-kick','b:kit-snare'},{'Full Metal','b:metal-kick','b:metal-snare'},{'Full Funk','b:funk-kick','b:funk-snare'}} do
    local kit={name=entry[1],ids={entry[2],entry[3],'b:hat','b:openhat'},lanes={}}
    for _,lane in ipairs(kitNotes) do kit.lanes[#kit.lanes+1]={name=lane[1],note=lane[2],id=type(lane[3])=='number' and kit.ids[lane[3]] or lane[3]} end
    M.kits[#M.kits+1]=kit
end
local sampledKit={name='Recorded Acoustic',ids={'b:sampled-drum-kit','b:sampled-drum-kit','b:sampled-drum-kit','b:sampled-drum-kit'},lanes={}}
for _,lane in ipairs(kitNotes) do sampledKit.lanes[#sampledKit.lanes+1]={name=lane[1],note=lane[2],id='b:sampled-drum-kit'} end
M.kits[#M.kits+1]=sampledKit
return M
