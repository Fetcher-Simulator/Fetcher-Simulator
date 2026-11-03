local root='files/server/server-lua-packages/fetcher.procedural-synth/';local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=dofile(root..'shared/schema.lua');local factory=dofile(root..'shared/project.lua');local F=factory(schema)
local MIDI=dofile(root..'client/midi.lua');local Sequence=dofile(root..'shared/sequence.lua')
package.loaded[prefix..'shared.schema']=schema;package.loaded[prefix..'shared.project']=factory;package.loaded[prefix..'shared.sequence']=Sequence
package.loaded[prefix..'client.midi']=MIDI;package.loaded[prefix..'client.soundsets']=dofile(root..'client/soundsets.lua')
package.loaded[prefix..'client.arrangement']=dofile(root..'client/arrangement.lua')
local W=dofile(root..'client/workstation.lua');local R=dofile(root..'client/recorder.lua');local Export=dofile(root..'client/midi-export.lua')
local Tools=dofile(root..'client/guitar-tools.lua')
local checks=0;local function check(ok,msg) checks=checks+1;assert(ok,msg) end
local function copy(t) if type(t)~='table' then return t end;local out={};for k,v in pairs(t) do out[k]=copy(v) end;return out end
local function le(s,at) local a,b,c,d=s:byte(at,at+3);return a+b*256+c*65536+d*16777216 end
local function pack(n) local out={};for i=1,4 do out[i]=string.char(n%256);n=math.floor(n/256) end;return table.concat(out) end
local function record(frame,gate,lane,note,velocity) return pack(frame)..pack(gate)..string.char(lane,note,velocity or 100) end
local function attacks(plan)
    local out={};local pending
    for at=1,#plan.bytes,11 do local frame,gate=le(plan.bytes,at),le(plan.bytes,at+4);local lane,n,v=plan.bytes:byte(at+8,at+10)
        if n<128 and v==130 then pending={e=schema.byteStrike(gate),n=n,lane=lane,frame=frame}
        elseif n<128 then
            if pending then check(pending.n==n and pending.frame==frame and pending.lane==lane,'companion stays immediately beside its matching note') end
            out[#out+1]={n=n,frame=frame,gate=gate,e=pending and pending.e};pending=nil
        end
    end
    check(not pending,'no orphan companion');return out
end
for code=0,179 do
    local e=assert(schema.byteStrike(code));check(schema.strikeByte(e)==code,'all partial snapshots round trip through one byte')
    local notes={{n=45,s=0,l=1,v=.7,e=e}};local saved=assert(F.encodeNotes(notes,960));local decoded=assert(F.decodeNotes(saved))
    check(schema.strikeByte(decoded[1].e)==code and #saved.bytes==10,'expression does not change compact note count/timing bytes')
    check(code==0 and saved.expression==nil or saved.expression==string.char(code),'empty/default old notes keep their original representation')
end
local plain=assert(F.encodeNotes({{n=60,s=0,l=1,v=1}},960));check(plain.expression==nil and #plain.bytes==10,'legacy encoding has no added field')
for _,bad in ipairs{{division=960,bytes=plain.bytes,expression=''}, {division=960,bytes=plain.bytes,expression=string.char(180)},
    {division=960,bytes=plain.bytes,expression=false}, {division=960,bytes=plain.bytes,expression='aa'},
    {division=960,bytes=plain.bytes,bad=true}} do check(not F.decodeNotes(bad),'malformed sidecars/fields reject') end
check(not F.encodeNotes({{n=60,s=0,l=1,v=1,e={cutoff=100}}},960),'unknown per-note expression rejects')

local now=100;local messages,heard,store={},{},{}
local w=W.new{configure=function() end,panic=function() end,now=function() return now end,message=function(s) messages[#messages+1]=s end,
    on=function(l,n,v,e) heard[#heard+1]={n=n,e=e};return true end,off=function() end,progress=function() return true,0,0 end,
    schedule=function() return true end,stopSequence=function() end}
assert(w:newSong(120,4,4,8));w.snap=1
local r=R.new(w,{}, {getRealTime=function() return now end},{},{get=function(_,k) return store[k] end,set=function(_,k,v) store[k]=v end},function(s) messages[#messages+1]=s end)
w.recorder=r;w.cb.recordOptions=function() return {countIn=0,latency=0,untilStop=true} end
w.cb.recordStart=function(t) return r:start(t) end;w.cb.recordEnd=function() r:ended() end
assert(r:setMapping('enabled',true));assert(r:setMapping('switches',true));assert(r:setMapping('stringCC',21));assert(r:setMapping('pickCC',22))
local function event(status,a,b) assert(r:event{status=status,channel=1,a=a,b=b,age=0}) end
local history=#w.undoStack;assert(w:startTake(1))
event(144,25,100);event(128,25,0);event(176,21,16);event(176,22,0);event(144,45,100)
now=100.01;event(144,27,100);event(128,27,0);event(176,21,32);event(176,22,127);event(144,47,90)
now=100.2;event(128,47,0);now=100.3;event(128,45,0);assert(w:endTake(false))
local notes=w:noteData(1)
check(#notes==2 and notes[1].n==45 and notes[2].n==47 and notes[1].s==notes[2].s,'same-grid chord retains attack order despite reversed release order')
check(notes[1].e.stringArticulation==1 and notes[1].e.stringSelect==1 and notes[1].e.pickDirection==0,'first note keeps Open/low string/down snapshot')
check(notes[2].e.stringArticulation==3 and notes[2].e.stringSelect==2 and notes[2].e.pickDirection==2,'second same-time note keeps Dead/second string/alternate snapshot')
check(heard[1].e.stringArticulation==1 and #heard==2,'keyswitches are not recorded or heard and later gestures do not mutate earlier attacks')
check(#w.undoStack==history+1,'notes and expression commit as one take')
assert(w:undo());check(#w:noteData(1)==0,'Undo removes the whole expressive take');assert(w:redo())
local saved=assert(F.project(w.project));check(saved.tracks[1].edited.expression~=nil,'strict project persistence retains the compact expression sidecar')
local plan=Sequence.compile(w.project,nil,MIDI,'song',F);local rendered=attacks(plan)
check(#rendered==2 and rendered[1].e.stringArticulation==1 and rendered[2].e.stringArticulation==3,'same-time attacks compile with distinct attached expressions')
local exported=MIDI.parse(Export.encode(w.project,nil,MIDI,F,'song'))
check(#MIDI.editableSpans(exported,MIDI.parts(exported)[1].id)==2,'MIDI export ignores companions and emits the original two musical notes')

w:selectAllNotes(1);assert(w:captureClip(1));local clip=w.project.tracks[1].arrangement.clips[1]
check(clip.notes.expression~=nil and #F.decodeNotes(clip.notes)==2,'captured reusable source carries expression')
assert(w:editClip(1,1));now=101;assert(w:startTake(1));event(144,26,100);event(128,26,0);event(176,21,48);event(144,52,100)
now=101.2;event(128,52,0);assert(w:endTake(false))
check(#w:noteData(1)==3 and w:noteData(1)[3].e.stringArticulation==2,'recording directly into a shared source keeps the new attack expression')
w:selectNote(1,1);local oldBytes=w:clipSource(1).notes.bytes;history=#w.undoStack
assert(w:editNoteExpression(1,'stringArticulation',2))
check(w:clipSource(1).notes.bytes==oldBytes and #w.undoStack==history+1 and w:noteData(1)[1].e.stringArticulation==2,'expression-only edit commits even when timing/pitch bytes are identical')
assert(w:editNoteExpression(1,nil));check(not w:noteData(1)[1].e,'clear note expression restores track/patch control');assert(w:undo());assert(w:editClip(1,1))
check(w:noteData(1)[1].e.stringArticulation==2,'Undo restores expression without changing timing')
w:selectNote(1,1);assert(w:copyNotes(1));assert(w:pasteNotes(1,8));check(w:noteData(1)[4].e.stringArticulation==2,'copy/paste retains saved expression')
assert(w:undo());assert(w:leaveClip());assert(w:placeClip(1,1,16));assert(w:changeRegion(1,2,{transpose=12}))
rendered=attacks(Sequence.compile(w.project,nil,MIDI,'song',F))
check(#rendered==6 and rendered[4].n==57 and rendered[4].e.stringArticulation==2,'linked repeat/transposed placement keeps per-note snapshots')
assert(w:changeRegion(1,2,{offset=1}));local offsetNotes=attacks(Sequence.compile(w.project,nil,MIDI,'song',F))
check(#offsetNotes==9 and offsetNotes[4].e.stringArticulation==2 and offsetNotes[7].e.stringSelect==1,'source offset/chased crossing notes and repeats retain their own snapshots')
assert(w:changeRegion(1,2,{offset=0}))
assert(w:addPart('keys'));assert(w:copyClip(1,1,2));check(w.project.tracks[2].arrangement.clips[1].notes.expression~=nil,'independent source copy preserves its sidecar')
local reload=W.new(w.cb);assert(reload:load(assert(F.project(w.project))))
check(#attacks(Sequence.compile(reload.project,nil,MIDI,'song',F))==6,'source-free project reload reproduces every expressive placement')

local legacy=W.new(w.cb);assert(legacy:add('piano'));assert(legacy:addNote(1,45,0));legacy:selectNote(1,1)
assert(legacy:editNoteExpression(1,'stringArticulation',2));check(F.project(legacy.project).tracks[1].notes[1].e.stringArticulation==2,'legacy piano patterns preserve validated expression tables')
local lplan=Sequence.compile(legacy.project,nil,MIDI,'clips',F);check(#attacks(lplan)==1,'legacy pattern expression uses the same native format')
local clock={quantum=1/960,seconds=function(s) return s*.125 end,shift=function(s,delay) return s+delay*8 end}
local tool=assert(Tools.plan({{n=45,s=0,l=4,v=1,e={stringSelect=1,stringArticulation=2}}},{spacing=0},clock))
check(tool.notes[1].e.stringSelect==1 and tool.rows[1].string==6,'guitar authoring respects a saved explicit low string and retains articulation')

local good=record(0,57,1,45,130)..record(0,480,1,45)
check(Sequence.validate(good,48000,false),'bounded companion/note pair validates')
for _,bad in ipairs{record(0,57,1,45,130),record(0,0,1,45,130)..record(0,480,1,45),record(0,180,1,45,130)..record(0,480,1,45),
    record(0,57,1,45,130)..record(0,480,1,47),record(0,57,1,45,130)..record(1,480,1,45),
    record(0,57,1,45,130)..record(0,480,2,45),record(0,57,1,45,130)..good,
    record(0,57,1,45,130)..record(0,100,1,141,0)..record(0,480,1,45)} do
    check(not Sequence.validate(bad,48000,false),'orphaned/invalid/interleaved companion rejects')
end
local burst={};for i=1,512 do burst[#burst+1]=record(0,1,1,(i-1)%128,130)..record(0,64,1,(i-1)%128) end
check(Sequence.validate(table.concat(burst),48000,false),'metadata does not double-count the 512-note burst limit')
local loop={};for i=0,255 do loop[#loop+1]=record(i*187,1,1,i%128,130)..record(i*187,50,1,i%128) end
check(Sequence.validate(table.concat(loop),48000,true),'metadata is excluded from loop boundary note-rate accounting')
print('PASS expression codec/migration, recorded same-time attacks, per-note editing, linked source capture/repeat/copy, save/Undo, MIDI export and bounded companion scores; '..checks..' assertions')
