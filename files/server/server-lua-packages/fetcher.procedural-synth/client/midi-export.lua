-- Standard MIDI format 1. Sound patches and gameplay routing stay in the project.
local M={division=960,maxNotes=29000}
local function be(n,count) local out={};for i=count,1,-1 do out[i]=string.char(n%256);n=math.floor(n/256) end;return table.concat(out) end
local function vlq(n)
    assert(n>=0 and n<=268435455,'MIDI delta exceeds four bytes')
    local out={string.char(n%128)};n=math.floor(n/128)
    while n>0 do table.insert(out,1,string.char(n%128+128));n=math.floor(n/128) end;return table.concat(out)
end
function M.encode(project,song,MIDI,format,mode)
    local Sequence=require('scripts.multiplayer.fetcher.procedural-synth.shared.sequence')
    local p=assert(format.project(project),'Invalid Studio project');mode=mode or ((p.timeline or song) and 'song' or 'clips')
    if p.composition then p.composition.loop=false;p.composition.loopStart=1;p.composition.loopEnd=p.composition.bars end
    local plan=Sequence.compile(p,song,MIDI,mode,format)
    assert(#plan.bytes/11<=M.maxNotes,'MIDI export supports at most 29000 expanded notes')
    local tempos=mode=='song' and ((p.timeline or song).tempos or {}) or {{beat=0,tempo=60000000/p.bpm}}
    local map={};local seconds,beat,tempo=0,0,500000
    for _,entry in ipairs(tempos) do
        seconds=seconds+(entry.beat-beat)*tempo/1000000;beat=entry.beat;tempo=entry.tempo
        map[#map+1]={beat=beat,time=seconds,tempo=tempo}
    end
    if #map==0 or map[1].beat>0 then table.insert(map,1,{beat=0,time=0,tempo=500000}) end
    local function tick(time)
        local low,high=1,#map
        while low<high do local mid=math.floor((low+high+1)/2);if map[mid].time<=time then low=mid else high=mid-1 end end
        local t=map[low];return math.max(0,math.floor((t.beat+(time-t.time)*1000000/t.tempo)*M.division+.5))
    end
    local finish=tick(plan.length/Sequence.rate);local tracks={{}};local order=0
    local function event(track,at,bytes,priority)
        order=order+1;tracks[track][#tracks[track]+1]={at=at,bytes=bytes,priority=priority or 0,order=order}
    end
    event(1,0,string.char(255,3)..vlq(#p.name)..p.name,-3)
    for _,t in ipairs(map) do event(1,math.floor(t.beat*M.division+.5),string.char(255,81,3)..be(math.floor(t.tempo+.5),3),-2) end
    if p.composition then
        local power=0;local d=p.composition.denominator;while d>1 do d=d/2;power=power+1 end
        event(1,0,string.char(255,88,4,p.composition.numerator,power,24,8),-1)
    end
    local channels={};local melodic=0
    for n,t in ipairs(p.tracks) do
        tracks[n+1]={};local role=t.sourceRole
        local drum=t.kind=='drum' or ({kick=true,snare=true,hat=true,openhat=true,tom=true})[role] or t.part:match('^%d+:9:')~=nil
        local channel,port=9,0
        if not drum then local index=melodic%15;channel=index>=9 and index+1 or index;port=math.floor(melodic/15);melodic=melodic+1 end
        channels[n]=channel
        event(n+1,0,string.char(255,3)..vlq(#t.name)..t.name,-3)
        event(n+1,0,string.char(255,33,1,port),-2)
        local program=song and song.programs and song.programs[t.part]
        if program then event(n+1,0,string.char(192+channel,program),-1) end
    end
    local function le(at) local a,b,c,d=plan.bytes:byte(at,at+3);return a+b*256+c*65536+d*16777216 end
    for at=1,#plan.bytes,11 do
        local frame,gate=le(at),le(at+4);local lane,note,velocity=plan.bytes:byte(at+8,at+10)
        local first,last=tick(frame/Sequence.rate),tick((frame+gate)/Sequence.rate);last=math.max(first+1,last);finish=math.max(finish,last)
        event(lane+1,first,string.char(144+channels[lane],note,velocity),1)
        event(lane+1,last,string.char(128+channels[lane],note,0),0)
    end
    local chunks={'MThd'..be(6,4)..be(1,2)..be(#tracks,2)..be(M.division,2)}
    for _,events in ipairs(tracks) do
        table.sort(events,function(a,b) return a.at<b.at or a.at==b.at and (a.priority<b.priority or a.priority==b.priority and a.order<b.order) end)
        local bytes,previous={},0
        for _,e in ipairs(events) do if e.at<=finish then bytes[#bytes+1]=vlq(e.at-previous)..e.bytes;previous=e.at end end
        bytes[#bytes+1]=vlq(finish-previous)..string.char(255,47,0);local body=table.concat(bytes)
        chunks[#chunks+1]='MTrk'..be(#body,4)..body
    end
    local result=table.concat(chunks);assert(#result<=512*1024,'MIDI export exceeds 512 KiB')
    MIDI.parse(result) -- also enforces the importer's event budget before writing
    return result
end
return M
