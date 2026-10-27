local root='files/server/server-lua-packages/fetcher.procedural-synth/';local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=dofile(root..'shared/schema.lua');local factory=dofile(root..'shared/project.lua');local f=factory(schema)
local MIDI=dofile(root..'client/midi.lua');local Sequence=dofile(root..'shared/sequence.lua')
package.loaded[prefix..'shared.schema']=schema;package.loaded[prefix..'shared.project']=factory
package.loaded[prefix..'client.midi']=MIDI;package.loaded[prefix..'client.soundsets']=dofile(root..'client/soundsets.lua')
local W=dofile(root..'client/workstation.lua');local R=dofile(root..'client/recorder.lua')
local checks=0;local function check(ok,msg) checks=checks+1;assert(ok,msg) end
local now=100;local messages,ons,offs,created={},{},{},{}
local store={};local section={get=function(_,k) return store[k] end,set=function(_,k,v) store[k]=v end}
local function engineClock()
    local a={playing=false,audiblePosition=0,closed=false}
    function a:configure(_,p) check(schema.state(p),'click patch validates');return true end
    function a:effects() return true end
    function a:loadScore(bytes,length,loop) self.bytes=bytes;self.length=length;self.loop=loop;return #bytes==0 or Sequence.validate(bytes,length,loop) end
    function a:start(delay,offset) self.playing=true;self.delay=delay;self.offset=offset;return true end
    function a:close() self.closed=true;self.playing=false end
    function a:keepAlive() end
    created[#created+1]=a;return a
end
local progress=0;local w=W.new{configure=function() end,panic=function() end,now=function() return now end,
    on=function(l,n,v) ons[#ons+1]={l,n,v};return true end,off=function(l,n) offs[#offs+1]={l,n} end,
    progress=function() return true,progress,0 end,schedule=function() return true end,stopSequence=function() end,message=function(m) messages[#messages+1]=m end}
local engine={createMonitor=engineClock,midiDevices=function() return {{id=0,name='QA keyboard'}} end}
local r=R.new(w,engine,{getRealTime=function() return now end},{},section,function(m) messages[#messages+1]=m end)
w.recorder=r;w.cb.recordOptions=function() return R.options(r.settings) end;w.cb.recordStart=function(t) return r:start(t) end;w.cb.recordEnd=function() r:ended() end
assert(w:newSong(120,4,4,8));w.snap=0
check(r.settings.countIn==1 and not r.settings.metronome and r.settings.latency==0,'recording defaults are local and click is opt-in')
check(r.settings.countClick,'count-in clicks default on independently of continuous metronome')
assert(w:startTake(1));check(#r.clock.bytes/11==4,'default count-in has four audible clicks without song metronome');w:endTake(true)
assert(r:set('countClick',false));assert(w:startTake(1));check(#r.clock.bytes==0,'explicit silent count-in remains available');w:endTake(true)
assert(r:set('countClick',true))
local gen=w.generation;assert(r:set('countIn',2));assert(r:set('metronome',true));assert(r:set('latency',100))
check(w.generation==gen and not r:set('countIn',3) and not r:set('latency',math.huge),'settings avoid document history and reject unsupported values')
check(w:startTake(1) and math.abs(w.recording.starts-now-4.15)<1e-7,'two-bar native count-in includes scheduled lead: '..table.concat(messages,' / '))
local a=r.clock;check(a and #a.bytes/11==40 and a.delay==.15,'count-in and song click are one native schedule')
check(not r:set('latency',0),'settings cannot move an armed take clock')
now=104.25;a.audiblePosition=4.1
check(w:recordOn(60,.8) and math.abs(w.recording.held[60].raw)<1e-6,'positive compensation clamps to capture origin')
now=104.65;a.audiblePosition=4.5
w:recordOff(60,nil,.1);check(w:endTake(false) and #w:noteData(1)==1,'timestamped off keeps the captured take')
check(math.abs(w:noteData(1)[1].l-2.4)<.01 and a.closed,'event age and compensation are applied once; Keep closes click')
assert(r:set('countIn',0));assert(r:set('latency',0));now=200;assert(w:startTake(1));a=r.clock
check(not w:recordOn(62,1),'zero-count take still waits for native start lead')
now=200.3;a.audiblePosition=.15;check(not w:recordOn(62,1,.2),'queued note from before native start cannot leak into take')
check(w:recordOn(62,1,.05) and math.abs(w.recording.held[62].raw-.8)<1e-6,'input timestamp removes Lua frame delay from onset')
w:endTake(true);check(a.closed,'Cancel stops local recording clock')
-- Playback metronome anchors to the existing audible song position, with loop modulo.
w.playing=true;w.native=true;w.mode='song';w.project.composition.loop=true;progress=40
r:frame(false);a=r.clock;check(a and math.abs(a.offset-8.15)<1e-6,'playback click joins current loop phase rather than restarting song')
w:stop(true);r:frame(false);check(a.closed,'Stop closes playback click')
-- Mixed tempo/meter click schedule remains native-time aligned.
assert(w:editTempo(8,60));assert(w:editMeter(8,3,4))
local p=R.plan(w,'song',0,14,false,nil,true)
local frames={};for at=1,#p.bytes,11 do local x,y,z,q=p.bytes:byte(at,at+3);frames[#frames+1]=x+y*256+z*65536+q*16777216 end
check(p.length==480000 and frames[9]==192000 and frames[10]==240000,'metronome beat positions integrate saved tempo map')
check(Sequence.validate(p.bytes,p.length,false),'click score respects bounded native upload format')
-- MIDI channel filter, duplicate pitches across channels and sustain release.
w.page='record';w.selected=1;assert(r:set('channel',2));local before=#ons
local function event(status,ch,n,v,age) return r:event{status=status,channel=ch,a=n,b=v,age=age or 0} end
event(144,1,60,100);check(#ons==before,'MIDI channel filter ignores other channels')
event(144,2,60,100);event(144,2,60,100);check(#ons==before+1,'hardware key repeat does not duplicate monitoring')
event(176,2,64,127);event(128,2,60,0);before=#offs
check(r.counts[60]==1,'sustain retains released hardware gate')
event(176,2,64,0);check(not r.counts[60] and #offs==before+1,'pedal-up releases sustained gate')
assert(r:set('channel',0));before=#ons;event(144,1,64,100);event(144,2,64,80)
check(#ons==before+1 and r.counts[64]==2,'omni duplicate pitch uses reference-counted monitoring')
event(144,1,64,0);check(r.counts[64]==1,'velocity-zero note-on releases only matching channel')
event(176,2,123,0);check(not r.counts[64],'all-notes-off releases remaining channel gate')
check(not event(144,1,65,100,10),'stale timestamps reject')
-- Native overflow/disconnect cannot leave held voices behind.
local closed=false;engine.openMidiInput=function() return {close=function() closed=true end,poll=function() return {connected=true,dropped=2,events={}} end} end
assert(r:connect(0));event(144,1,67,127);before=#offs;r:frame('record')
check(closed and not r.input and not r.counts[67] and #offs==before+1,'overflow closes port and releases held notes')
engine.openMidiInput=function() return {close=function() closed=true end,poll=function() return {connected=true,dropped=0,events={}} end} end
assert(r:connect(0));event(144,1,68,127);r:frame(false);check(not r.input and not r.counts[68],'Hide closes input and releases monitoring')
-- A connected device remains playable across normal pages, with explicit Synth targets.
local batch={};closed=false
engine.openMidiInput=function() return {close=function() closed=true end,poll=function()
    local events=batch;batch={};return {connected=true,dropped=0,events=events}
end} end
assert(r:connect(0));local input=r.input;local generation=w.generation
for _,page in ipairs{'piano','drums','mixer','recordsetup','record','song','samples'} do
    batch={{status=144,channel=1,a=60,b=90,age=0}};before=#ons;r:frame(page,1)
    check(r.input==input and not closed and #ons==before+1 and ons[#ons][1]==1,'normal '..page..' page retains MIDI connection and monitors selected track')
    batch={{status=144,channel=1,a=60,b=0,age=0}};r:frame(page,1)
    check(not r.counts[60],'normal '..page..' page handles velocity-zero release')
end
batch={{status=144,channel=1,a=61,b=100,age=0}};r:frame('synth',0)
check(ons[#ons][1]==0 and r.inputTrack==0,'manual Synth monitors channel zero rather than the selected project part')
before=#offs;r:frame('piano',1)
check(#offs==before+1 and offs[#offs][1]==0 and not r.counts[61] and r.input==input,'destination switch releases manual gate without disconnecting')
check(w.generation==generation,'unarmed playing does not insert notes or edit history')
r:frame(false);check(closed and not r.input,'Hide still closes device after normal-page monitoring')
-- Tap calibration computes timing bias robustly, with explicit Apply and local persistence.
local bias,jitter=R.calculation{80,81,79,82,80,81,79,190};check(bias==80.5 and jitter==1,'median/MAD calibration resists an isolated bad tap')
check(not R.calculation{10,11},'too few calibration taps cannot produce a result')
local calibrationGeneration=w.generation;assert(r:beginCalibration());a=r.clock
for beat=2,9 do a.audiblePosition=beat*.5+.08;assert(r:tap()) end
check(r:applyCalibration() and r.settings.latency==80 and store.latency==80 and a.closed,'eight taps apply local compensation and stop calibration')
local reload=R.new(w,engine,{},{},section,function() end);check(reload.settings.latency==80 and reload.settings.countIn==0,'recording preferences survive local recreation')
check(w.generation==calibrationGeneration,'calibration does not change musical document history')
now=300;assert(w:startTake(1));r.clock.closed=true;r:frame('record')
check(not w.recording and not r.clock,'failed native clock during count-in cannot leave an armed take stuck')
local imported=f.new();imported.timeline={beats=16,tempos={{beat=0,tempo=500000}}};imported.tracks[1]=f.newTrack('midi','Converted',16);imported.tracks[1].edited=f.encodeNotes({},960)
assert(w:load(imported));now=400;assert(r:set('countIn',1))
check(w:startTake(1) and r.clock.length<=86400000 and w.recording.limit<14400,'converted MIDI stopped take leaves native clock room for count-in')
w:endTake(true)
r:close();check(not r.clock and not r.input,'session close leaves no local clock or port')
print('PASS native count-in/click plan, compensation/timestamps, MIDI sustain/filter/refcounts/cleanup and robust local calibration; '..checks..' assertions')
