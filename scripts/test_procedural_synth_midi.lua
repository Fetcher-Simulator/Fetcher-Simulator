local midi=dofile('files/server/server-lua-packages/fetcher.procedural-synth/client/midi.lua')
local count=0
local function check(value,message) count=count+1;assert(value,message) end
local function be(n,size) local b={};for i=size,1,-1 do b[i]=string.char(n%256);n=math.floor(n/256) end;return table.concat(b) end
local function smf(tracks,division,format)
    local b={'MThd',be(6,4),be(format or (#tracks==1 and 0 or 1),2),be(#tracks,2),be(division or 96,2)}
    for _,track in ipairs(tracks) do b[#b+1]='MTrk'..be(#track,4)..track end
    return table.concat(b)
end
local function bytes(...) return string.char(...) end
local tempo=bytes(0,255,81,3,7,161,32,96,255,81,3,15,66,64,96,255,47,0)
local notes=bytes(0,144,60,100,0,64,80,96,128,60,0,96,64,0,0,255,47,0)
local song=midi.parse(smf{tempo,notes})
check(#song.events==4 and song.events[1].note==60 and song.events[2].note==64,'running status/track merge')
check(song.events[3].time==0.5 and song.events[4].time==1.5,'tempo map in seconds')
check(song.duration==1.5,'track-end duration')
local assigned=midi.parse(smf{bytes(0,144,60,90,48,192,32,48,128,60,0,0,255,47,0),bytes(0,192,24,96,255,47,0)})
check(midi.parts(assigned)[1].program==24,'program from a separate setup track at the same tick selects the initial instrument')
check(#assigned.events==2,'program metadata does not add playable note events or change part identity')
local calls={}
local function callbacks() return {on=function(n,v) calls[#calls+1]={'on',n,v} end,
    off=function(n) calls[#calls+1]={'off',n} end,panic=function() calls[#calls+1]={'panic'} end} end
local p=midi.player(song,callbacks())
p:update(0);check(#calls==2 and calls[1][3]==100/127,'chord velocity')
for _=1,6 do p:update(0.25) end
check(not p.playing and calls[#calls][1]=='panic','song end releases audio')
local sustain=midi.parse(smf{bytes(0,144,60,127,0,176,64,127,48,128,60,0,48,176,64,0,0,255,47,0)})
calls={};p=midi.player(sustain,callbacks());p:update(0.25)
check(#calls==1,'sustain retains released key')
p:update(0.25);check(calls[2][1]=='off','pedal up releases note')
local overlapping=midi.parse(smf{bytes(0,144,60,100,0,145,60,100,24,128,60,0,24,129,60,0,0,255,47,0)})
calls={};p=midi.player(overlapping,callbacks());p:update(0.125)
check(#calls==2,'another channel keeps a shared pitch alive')
p:update(0.125);check(calls[3][1]=='off','last channel releases shared pitch')
local repeated=midi.parse(smf{bytes(0,144,60,100,0,144,60,100,24,128,60,0,24,128,60,0,0,255,47,0)})
calls={};p=midi.player(repeated,callbacks());p:update(0.125)
check(#calls==2,'overlapping notes on one channel remain held')
p:update(0.125);check(calls[3][1]=='off','last same-channel note-off releases pitch')
calls={};p=midi.player(song,callbacks());p:update(0);p:update(1)
check(not p.playing and calls[#calls][1]=='panic','frame stall stops instead of catch-up flood')
local percussion=midi.parse(smf{bytes(0,153,36,100,96,137,36,0,0,255,47,0)})
calls={};p=midi.player(percussion,callbacks());p:update(0);check(#calls==0,'percussion channel skipped')
for _,bad in ipairs{'',smf({notes},32768),smf({notes},96,2),'MThd'..be(6,4)..be(0,2)..be(1,2)..be(96,2)..'MTrk'..be(999,4),smf{bytes(0,60,100)},smf{bytes(128,128,128,128,0)}} do
    check(not pcall(midi.parse,bad),'malformed MIDI accepted')
end
local checkpoints=0
local big={}
for _=1,500 do big[#big+1]=bytes(0,144,60,100,1,128,60,0) end
local parsed=midi.parse(smf{table.concat(big)},function() checkpoints=checkpoints+1 end)
check(checkpoints>=6 and #parsed.events==1000,'bounded import checkpoints')
-- Multitimbral mode keeps equal pitches on separate SMF parts independent.
local namedA=bytes(0,255,3,4)..'Bass'..bytes(0,192,33,0,144,60,100,24,128,60,0,0,255,47,0)
local namedB=bytes(0,255,3,4)..'Lead'..bytes(0,144,60,80,48,128,60,0,0,255,47,0)
local multi=midi.parse(smf{namedA,namedB});local parts=midi.parts(multi)
check(#parts==2 and parts[1].name=='Bass / Ch 1' and parts[1].program==33,'track names, channel and program metadata retained')
calls={}
p=midi.player(multi,{on=function(n,v,ch,part) calls[#calls+1]={'on',n,part} end,
    off=function(n,ch,part) calls[#calls+1]={'off',n,part} end,panic=function() end},{multitimbral=true,percussion=true})
p:update(0.125)
check(#calls==3 and calls[3][1]=='off' and calls[3][3]=='1:0','first part note-off stays on its own route')
p:update(0.125);check(calls[4][3]=='2:0','second part sustains until its own note-off')
local spans=midi.noteSpans(multi)
check(spans['1:0'][1].s==0 and spans['1:0'][1].l==1 and spans['2:0'][1].l==2,'piano roll retains beat positions and duration')
local drumSong=midi.parse(smf{bytes(0,153,36,100,0,153,42,90,24,137,36,0,24,137,42,0,0,255,47,0)})
local drumParts=midi.parts(drumSong)
check(#drumParts==2 and drumParts[1].note==36 and drumParts[2].note==42,'percussion pitches split into independently assignable lanes')
calls={};p=midi.player(drumSong,{on=function(n,v,c,part) calls[#calls+1]=part end,off=function() end,panic=function() end},{multitimbral=true,percussion=true})
p:update(0);check(calls[1]=='1:9:36' and calls[2]=='1:9:42','drum lane identity survives scheduling')
-- Parse the user's installed Bardcraft repertoire without redistributing it.
if arg[1] then
    local f=assert(io.open(arg[1],'rb'));local data=f:read('*a');f:close()
    local real=midi.parse(data)
    check(#real.events>0 and real.duration>0,'Bardcraft MIDI import')
    print(string.format('Bardcraft MIDI: %d events, %.2fs, %d tracks',#real.events,real.duration,real.tracks))
end
print('PASS MIDI parser, tempo map, scheduler, sustain, overlap, cleanup and malformed input; '..count..' assertions')
