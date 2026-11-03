local root='files/server/server-lua-packages/fetcher.procedural-synth/';local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=dofile(root..'shared/schema.lua');local factory=dofile(root..'shared/project.lua');local F=factory(schema)
package.loaded[prefix..'shared.schema']=schema;package.loaded[prefix..'shared.project']=factory
package.loaded[prefix..'client.midi']=dofile(root..'client/midi.lua')
package.loaded[prefix..'client.soundsets']=dofile(root..'client/soundsets.lua')
local Map=dofile(root..'client/controller-map.lua');package.loaded[prefix..'client.controller-map']=Map
local W=dofile(root..'client/workstation.lua');local R=dofile(root..'client/recorder.lua')
local checks=0;local function check(ok,msg) checks=checks+1;assert(ok,msg) end
local now,position=100,1;local saved,messages,configured={},{},{};local manual=schema.state{}
local w=W.new{configure=function(n,p,level,mix) configured[n]={params=p,level=level,mixer=mix} end,
    effects=function(e,m) configured.master=m end,currentParams=function() return manual end,
    manualParameter=function(n,v) manual[n]=v;return true end,
    panic=function() end,message=function(s) messages[#messages+1]=s end,on=function() return true end,off=function() end,
    now=function() return now end,progress=function() return true,position,0 end,stopSequence=function() end,schedule=function() return true end}
assert(w:newSong(120,4,4,8));assert(w:addPart('bass'));w.selected=1
local store={get=function(_,k) return saved[k] end,set=function(_,k,v) saved[k]=v end}
local r=R.new(w,{}, {getRealTime=function() return now end},{},store,function(s) messages[#messages+1]=s end);w.recorder=r
local map=r.controllers
local function event(cc,value,ch,age) return r:event{status=176,a=cc,b=value,channel=ch or 1,age=age or 0} end
check(#map.bindings==0 and not r.mapping.enabled,'old preferences gain no implicit bindings')
local binding={cc=10,target='param:cutoff',track=0,pickup=true}
local function opt(changes) local b={};for k,v in pairs(binding) do b[k]=v end;for k,v in pairs(changes or {}) do b[k]=v end;return Map.options({b},r.mapping) end
for _,bad in ipairs{{cc=64},{cc=120},{cc=-2},{cc=.5},{target='execute:evil'},{track=33},{pickup=1},{bad=true}} do check(not opt(bad),'invalid controller binding rejected') end
check(not Map.options({binding,binding},r.mapping) and not Map.options({[2]=binding},r.mapping),'duplicate and sparse binding lists rejected')
check(not Map.options({binding},{articulationCC=10}),'articulation and general bindings share unique CC allocation')
for _,target in ipairs(Map.order) do
    local spec=target.spec;local previous=-math.huge
    for raw=0,127 do local value=Map.value(raw,spec)
        check(value>=spec.min-1e-8 and value<=spec.max+1e-8 and value>=previous and (not spec.discrete or value%1==0),'every target maps monotonically inside its native bounds');previous=value
    end
    check(math.abs(Map.value(0,spec)-spec.min)<1e-8 and math.abs(Map.value(127,spec)-spec.max)<1e-8,'every target reaches both endpoints')
end
check(math.abs(Map.value(63.5,Map.targets['param:cutoff'].spec)-600)<1e-8,'cutoff uses logarithmic travel')
assert(map:edit(1,binding));local original=w.project.tracks[1].params.cutoff;event(10,127)
check(w.project.tracks[1].params.cutoff==original,'disabled mappings never change the sound')
assert(r:setMapping('enabled',true));event(10,0);event(10,40)
check(w.project.tracks[1].params.cutoff==original,'pickup waits below current cutoff')
event(10,127);check(w.project.tracks[1].params.cutoff==18000 and configured[1].params.cutoff==18000,'crossing pickup acquires and applies the real track configuration')
event(10,63);local expected=Map.value(63,Map.targets['param:cutoff'].spec)
check(w.project.tracks[1].params.cutoff==expected and w.project.tracks[1].presetId=='' and w.dirty,'acquired knob updates the saved sound and clears stale preset identity')
local gen=w.generation;event(10,63);check(w.generation==gen,'repeated identical CC values are no-op document edits')
w:setParameter(1,'cutoff',100);event(10,127)
check(w.project.tracks[1].params.cutoff==100,'external edits rearm pickup without jumping')
w.selected=2;local bass=w.project.tracks[2].params.cutoff;event(10,0)
check(w.project.tracks[2].params.cutoff==bass,'changing selected track rearms pickup')
event(10,127);check(w.project.tracks[2].params.cutoff==18000,'new target acquires independently')
assert(map:edit(2,{cc=11,target='level',track=1,pickup=false}));event(11,32)
check(w.project.tracks[1].level==32/127 and w.project.tracks[2].level~=32/127,'fixed-track fader controls its chosen track regardless of selection')
assert(map:edit(3,{cc=12,target='param:wave1',pickup=false}));event(12,127)
check(w.project.tracks[2].params.wave1==4,'discrete targets use valid even bins')
assert(map:edit(4,{cc=13,target='mixer:bass',track=1,pickup=false}));event(13,127)
check(w.project.tracks[1].mixer.bass==18 and configured[1].mixer.bass==18,'channel processing follows normal configure/replication path')
assert(map:edit(5,{cc=14,target='master',pickup=false}));event(14,32)
check(w.project.master==32/127 and configured.master==32/127,'master fader reaches shared effects path')
assert(map:edit(6,{cc=15,target='effects:reverbReturn',pickup=false}));event(15,100)
check(w.project.effects.reverbReturn==100/127,'return processing is assignable')
assert(map:edit(7,{cc=16,target='param:drive',track=32,pickup=false}));gen=w.generation;event(16,127)
check(w.generation==gen,'missing fixed tracks are ignored and never redirected')
check(not r:setMapping('articulationCC',10) and not map:edit(2,{cc=10}),'assignment conflicts reject in both directions')
assert(r:learnMapping(2));event(64,127);check(r.learn==2 and map.bindings[2].cc==11,'Learn does not swallow sustain or its binding')
event(10,127);check(r.learn==2,'conflicting learned CC leaves Learn pending')
event(18,90);check(not r.learn and map.bindings[2].cc==18,'next eligible free CC is learned and saved')
assert(r:set('channel',2));gen=w.generation;event(18,127,1);check(w.generation==gen,'channel filtering precedes general mapping')
event(18,127,2);check(w.project.tracks[1].level==1,'accepted channel can move the fixed fader')
assert(r:set('channel',0));r.monitorTrack=0;event(12,32)
check(manual.wave1==1 and w.project.tracks[2].params.wave1==4,'manual Synth mapping edits channel zero without touching a Studio sound')
event(18,64);check(w.project.tracks[1].level==64/127,'fixed faders still work from manual Synth')
assert(map:edit(3,{target='param:polyphony'}));event(12,127)
check(manual.polyphony==32,'manual Synth keeps its full native voice range')
assert(map:edit(3,{target='param:wave1'}))
r:releaseInput();check(not next(map.state),'Hide/disconnect releases pickup state')
local reload=R.new(w,{}, {},{},store,function() end)
check(#reload.controllers.bindings==7 and reload.controllers.bindings[2].cc==18 and not next(reload.controllers.state),'only validated bindings persist locally; pickup state does not')
-- A busy controller poll preserves CC/note order but creates only one checkpoint.
r.monitorTrack=nil;w.selected=1;local before=#w.undoStack;local gen=w.generation
w.lastEditKey=nil
r:events({{status=176,a=18,b=20,channel=1,age=0},{status=176,a=13,b=100,channel=1,age=0},
    {status=176,a=18,b=90,channel=1,age=0}},true)
check(#w.undoStack==before+1 and w.generation==gen+1 and w.project.tracks[1].level==90/127,'one poll checkpoints simultaneous controller edits once')
assert(w:undo());check(w.project.tracks[1].level==64/127,'poll Undo restores the starting fader value')
assert(w:redo())
-- Explicit automation capture groups note-independent controls into one Undo and compensates timestamps.
r.monitorTrack=nil;w.selected=2;w.playing=true;w.native=true;w.mode='song';w.beat=2
assert(r:set('latency',50));assert(map:edit(1,{pickup=false}));local history=#w.undoStack
assert(r:writeMappings());position=1.25;event(10,20,1,.075);event(12,64,1,.075);event(18,100,1,.075)
check(not map:edit(2,{cc=19}) and not r:learnMapping(2),'binding configuration remains frozen while writing')
assert(r:writeMappings());check(#w.undoStack==history+1 and not w.automationWriting,'all captured and live fader edits commit as one Undo transaction')
local t=w.project.tracks[2];local point
for _,p in ipairs(t.automation.cutoff) do if p.s==9 then point=p end end
check(point and point.linear==true and point.v==Map.value(20,Map.targets['param:cutoff'].spec),'continuous curve records the compensated event in native units')
check(t.automation.wave1[#t.automation.wave1].linear==false,'discrete mapped targets record stepped automation')
check(w.project.tracks[1].automation.trackLevel[3].s==9 and w.project.tracks[1].automation.trackLevel[3].v==100/127,'fixed mixer faders capture one age-compensated absolute-level lane')
local project=assert(F.project(w.project));check(project.tracks[2].automation.cutoff and project.master==32/127,'result survives strict client/server document persistence')
assert(w:undo());check(not (w.project.tracks[2].automation or {}).cutoff,'Undo removes the whole mapping take')
assert(w:redo());check(w.project.tracks[2].automation.cutoff,'Redo restores mapping take')
w:stop(true);assert(map:edit(7,nil,true));check(#map.bindings==6,'remove binding compacts the local list')
for n=7,Map.maxBindings do assert(map:edit(n,{})) end
check(not map:edit(17,{}) and #map.bindings==16,'binding count remains bounded')
print('PASS general CC assignments, all native ranges, pickup/rearm, fixed/current/manual targets, conflicts/Learn, sustain/channel handling, persistence, and atomic compensated automation; '..checks..' assertions')
