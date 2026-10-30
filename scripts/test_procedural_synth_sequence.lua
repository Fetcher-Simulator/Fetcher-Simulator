local root='files/server/server-lua-packages/fetcher.procedural-synth/'
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=dofile(root..'shared/schema.lua');local format=dofile(root..'shared/project.lua')(schema)
local Sequence=dofile(root..'shared/sequence.lua');local MIDI=dofile(root..'client/midi.lua')
package.loaded[prefix..'shared.schema']=schema
package.loaded[prefix..'client.soundsets']=dofile(root..'client/soundsets.lua');package.loaded[prefix..'shared.project']=dofile(root..'shared/project.lua')
package.loaded[prefix..'shared.sequence']=Sequence;package.loaded[prefix..'client.midi']=MIDI
local checks=0;local function check(ok,msg) checks=checks+1;assert(ok,msg) end
local function number(bytes,at) local a,b,c,d=bytes:byte(at,at+3);return a+b*256+c*65536+d*16777216 end
local function record(bytes,index)
    local at=(index-1)*11+1;return {frame=number(bytes,at),gate=number(bytes,at+4),lane=bytes:byte(at+8),pitch=bytes:byte(at+9)}
end
local p=format.new();p.tracks[1]=format.newTrack('piano','Piano',16);p.tracks[1].notes={{n=60,s=0,l=32,v=0.8},{n=64,s=1,l=1,v=0.5}}
p.swing=0.5
local plan=Sequence.compile(p,nil,MIDI,'clips')
check(plan.loop and plan.length==96000 and #plan.bytes==22,'one bar compiles to a looping two-onset score')
check(record(plan.bytes,1).frame==0 and record(plan.bytes,1).gate==192000,'long gates retain their cross-loop duration')
check(record(plan.bytes,2).frame==9000,'swing is encoded at its exact sample position')
check(Sequence.validate(plan.bytes,plan.length,true,{[1]=true}),'valid configured score accepted')
check(not Sequence.validate(plan.bytes,plan.length,true,{[2]=true}),'unconfigured lanes rejected')
check(not Sequence.validate('bad',plan.length,true) and not Sequence.validate(plan.bytes,0,true),'malformed bytes and length rejected')
check(not Sequence.validate(plan.bytes:sub(12)..plan.bytes:sub(1,11),plan.length,true),'nonmonotonic onsets rejected')
do
    local function pack(n) return string.char(n%256,math.floor(n/256)%256,math.floor(n/65536)%256,math.floor(n/16777216)%256) end
    local function note(frame) return pack(frame)..pack(1)..string.char(1,60,100) end
    local dense={};for n=0,639 do dense[#dense+1]=note(n*750) end
    check(Sequence.validate(table.concat(dense),480000,false) and Sequence.validate(table.concat(dense),480000,true),'64 notes/sec across an entire song no longer hits the live-note rate')
    local burst={};for n=1,513 do burst[n]=note(0) end
    check(not Sequence.validate(table.concat(burst),48000,false),'instantaneous flood remains capped at 512 notes')
    local flood={};for n=0,4799 do flood[#flood+1]=note(n*120) end
    check(not Sequence.validate(table.concat(flood),576000,false),'sustained prerecorded flood remains capped at 256 notes/sec')
    local edge={};for n=1,300 do edge[#edge+1]=note(0) end;for n=1,300 do edge[#edge+1]=note(235200) end
    check(Sequence.validate(table.concat(edge),240000,false) and not Sequence.validate(table.concat(edge),240000,true),'loop boundaries retain burst protection')
end
p.loop=false;local finite=Sequence.compile(p,nil,MIDI,'clips')
check(record(finite.bytes,1).gate==96000,'finite pattern truncates a gate at its end')
local song={events={{kind='on',channel=0,part='1:0',note=60,velocity=1,time=0,tick=0},
    {kind='control',channel=0,part='1:0',note=64,velocity=127,time=0.2,tick=40},
    {kind='off',channel=0,part='1:0',note=60,time=0.5,tick=96},
    {kind='control',channel=0,part='1:0',note=64,velocity=0,time=1.5,tick=192}},duration=2,beats=3,division=96,
    tempos={{beat=0,time=0,tempo=500000},{beat=1,time=0.5,tempo=1000000}}}
p=format.new();p.tracks[1]=format.newTrack('midi','Part',16);p.tracks[1].part='1:0'
p.tracks[2]=format.newTrack('piano','Clip',16);p.tracks[2].notes={{n=67,s=8,l=2,v=0.8}}
local midi=Sequence.compile(p,song,MIDI,'song')
check(not midi.loop and record(midi.bytes,1).gate==72000,'sustain pedal extends the native gate until release')
check(record(midi.bytes,2).frame==72000 and record(midi.bytes,2).gate==24000,'clips follow the imported tempo map')
local f=assert(io.open('apps/openmw-server/scripts/procedural_synth_sequence.lua','rb'));local mirror=f:read('*a');f:close()
f=assert(io.open(root..'shared/sequence.lua','rb'));check(mirror==f:read('*a'),'server/client sequence format byte parity');f:close()
local W=dofile(root..'client/workstation.lua');local queued,stopped,nativeTime,nativePlaying=0,0,0,true
local w=W.new{configure=function() end,panic=function() end,message=function(msg) error(msg) end,
    schedule=function(score,replace) queued=queued+1;check(score.bytes and type(replace)=='boolean','native replacement metadata');return true end,
    stopSequence=function() stopped=stopped+1 end,progress=function() return nativePlaying,nativeTime,0 end}
w:add('piano');w:toggleNote(1,60,0);check(w:play('clips'),'native playback starts without frame note dispatch')
w.awaiting=false;nativeTime=2;w:frame(2)
check(w.playing and w.native and w.time==2,'long game frame reads native position without stopping transport')
w:toggleNote(1,64,4);nativeTime=2.3;w:frame(0.3)
check(queued==2 and w.playing,'live clip edits schedule a quantized replacement')
nativePlaying=false;w:frame(0.1);check(not w.playing,'native completion reaches the UI')
w:play('clips');w.project.loop=false;w:touch()
check(not w.playing and not w.native,'changing loop mode stops the native program before recompiling its clock domain')
w:play('clips');w:stop(true);check(stopped>0 and not w.native,'Stop cancels the native transport')
print('PASS score compilation, sustain, tempo, binary validation and native workstation timing; '..checks..' assertions')
