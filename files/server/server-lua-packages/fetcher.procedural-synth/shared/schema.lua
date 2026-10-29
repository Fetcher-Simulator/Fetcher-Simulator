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
p('driveType',0,3,0,true);p('drive',0,36,12);p('driveTone',200,16000,12000)
p('fxMix',0,1,1);p('fxOutput',0,2,1)
p('ampBass',-12,12,0);p('ampMid',-12,12,0);p('ampTreble',-12,12,0);p('cabinet',0,3,0,true)
function M.valid(name, value)
    local s = M.specs[name]
    return s and type(value) == 'number' and value == value and value >= s.min and value <= s.max
        and (not s.discrete or value % 1 == 0)
end
function M.sample(input)
    if type(input)~='table' then return nil end
    local fields={path=true,offset=true,duration=true,root=true,oneShot=true,hash=true}
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
    return {path=path,offset=math.floor(input.offset*1000+0.5)/1000,
        duration=math.floor(input.duration*1000+0.5)/1000,root=input.root,oneShot=input.oneShot,hash=input.hash}
end
function M.isBank(sample) return sample and sample.path:sub(-4)=='.fsb' or false end
function M.sampleKey(sample)
    if not sample then return '' end
    return table.concat({sample.path,sample.offset,sample.duration,sample.root,tostring(sample.oneShot),sample.hash},'|')
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
    'Hip-hop / Trap / Lo-fi','DnB / Dubstep','Pop / Synthwave / Disco','Ambient / Cinematic','Chiptune','Orchestral / Cinematic','Acoustic / Classical','Drum Kit','Personal'}
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
for _,entry in ipairs{{'sampled-electric-clean','Sampled Electric Clean',0,0,1,.7,27},
    {'sampled-electric-crunch','Sampled Electric Crunch',1,22,2,.6,29},
    {'sampled-electric-metal','Sampled Electric Metal',2,29,3,.5,30}} do
    add(entry[1],entry[2],'Guitar','CC Sampled Instruments',{attack=.002,decay=.2,sustain=1,release=.12,
        cutoff=16000,driveType=entry[3],drive=entry[4],cabinet=entry[5],volume=entry[6],polyphony=8,
        sample={path='sound/fetcher/banks/electric-di-v1.fsb',offset=0,duration=1,root=60,oneShot=false,hash='969e921873764a7fb7a112d9574d3dfe'}})
    M.builtins[#M.builtins].midiProgram=entry[7]
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
return M
