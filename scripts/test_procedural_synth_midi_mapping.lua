local root='files/server/server-lua-packages/fetcher.procedural-synth/';local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=dofile(root..'shared/schema.lua');local factory=dofile(root..'shared/project.lua');local F=factory(schema)
local MIDI=dofile(root..'client/midi.lua');local Sequence=dofile(root..'shared/sequence.lua')
package.loaded[prefix..'shared.schema']=schema;package.loaded[prefix..'shared.project']=factory
package.loaded[prefix..'client.midi']=MIDI;package.loaded[prefix..'client.soundsets']=dofile(root..'client/soundsets.lua')
local W=dofile(root..'client/workstation.lua');package.loaded[prefix..'client.controller-map']=dofile(root..'client/controller-map.lua')
local R=dofile(root..'client/recorder.lua')
local checks=0;local function check(ok,msg) checks=checks+1;assert(ok,msg) end
local now,position=100,1;local ons,offs,messages,store={},{},{},{}
local w=W.new{configure=function() end,panic=function() end,message=function(s) messages[#messages+1]=s end,
    on=function(track,n,v,e) ons[#ons+1]={track=track,n=n,v=v,expression=e};return true end,off=function(track,n) offs[#offs+1]={track,n} end,
    now=function() return now end,progress=function() return true,position,0 end,stopSequence=function() end,schedule=function() return true end}
assert(w:newSong(120,4,4,8));local section={get=function(_,k) return store[k] end,set=function(_,k,v) store[k]=v end}
local r=R.new(w,{}, {getRealTime=function() return now end},{},section,function(s) messages[#messages+1]=s end)
w.recorder=r;local function event(status,n,v,ch,age) return r:event{status=status,a=n,b=v,channel=ch or 1,age=age or 0} end
local function on(n) assert(event(144,n,100));return ons[#ons] end
local function off(n) assert(event(128,n,0)) end
check(not r.mapping.enabled and not r.mapping.switches and r.mapping.articulationCC==-1,'old preferences start with mappings disabled')
check(not R.mappingOptions{articulationCC=64} and not R.mappingOptions{articulationCC=12,stringCC=12}
    and not R.mappingOptions{switchBase=125} and not R.mappingOptions{enabled=1} and not R.mappingOptions{bad=true},'malformed, duplicate and reserved bindings reject')
local before=w.generation;on(24);off(24);check(ons[#ons].n==24 and not ons[#ons].expression,'disabled keyswitch notes remain playable')
assert(r:setMapping('articulationCC',20));assert(r:setMapping('stringCC',21));assert(r:setMapping('pickCC',22));assert(r:setMapping('enabled',true))
event(176,20,64);event(176,21,32);event(176,22,127)
local first=on(45);check(first.expression.stringArticulation==2 and first.expression.stringSelect==2 and first.expression.pickDirection==2,'CC bins latch palm/string two/alternate at note attack')
event(176,20,127);event(176,21,16);event(176,22,0)
local second=on(47);check(second.expression.stringArticulation==3 and second.expression.stringSelect==1 and second.expression.pickDirection==0,'a later queued attack receives its own snapshot')
check(first.expression.stringArticulation==2 and first.expression.stringSelect==2,'later CC changes never mutate a previous note snapshot')
off(45);off(47);check(w.generation==before and w.project.tracks[1].params.stringArticulation==0,'unarmed controls do not alter musical document or sound preset')
assert(r:setMapping('switches',true));assert(event(144,25,127));local count=#ons;event(128,25,0)
check(#ons==count and not r.counts[25] and on(50).expression.stringArticulation==1,'keyswitch press/release is consumed and latches Open')
off(50);event(144,24,127);event(128,24,0);check(on(50).expression.stringArticulation==0,'Patch keyswitch explicitly returns to the patch articulation');off(50)
assert(r:set('channel',2));count=#ons;event(176,20,127,1);event(144,27,127,1)
check(#ons==count and not next(r.expression),'channel filtering applies before CC and keyswitch mapping')
event(176,20,32,2);count=#ons;on(50);check(#ons==count,'other-channel notes are filtered without creating attacks') -- on() uses channel one
event(144,50,100,2);check(ons[#ons].expression.stringArticulation==1,'accepted MIDI channel uses its mapped expression');event(128,50,0,2)
assert(r:set('channel',0));assert(r:learnMapping('articulationCC'));event(176,64,127)
check(r.learn=='articulationCC' and r.mapping.articulationCC==20,'sustain cannot hijack a pending Learn binding')
on(54);off(54);check(r.counts[54]==1,'pedal still retains notes while Learn is pending')
event(176,64,0);check(not r.counts[54] and r.learn=='articulationCC','Learn does not swallow sustain release')
event(176,23,100);check(not r.learn and r.mapping.articulationCC==23 and store.midiMapping.articulationCC==23,'Learn saves the next eligible CC locally')
assert(r:learnMapping('articulationCC'));event(176,21,100);check(r.learn and r.mapping.articulationCC==23,'conflicting learned CC remains pending instead of replacing another binding')
r:learnMapping('articulationCC');event(176,23,127);on(52);event(176,64,127);off(52)
check(r.counts[52]==1,'mapped playing retains normal sustain gates');event(176,64,0);check(not r.counts[52],'sustain release remains authoritative')
event(176,121,0);check(not next(r.expression),'reset controllers clears live overrides')
event(176,23,127);r:frame('synth',0);event(144,55,100)
check(ons[#ons].track==0 and not ons[#ons].expression,'switching to manual Synth resets the previous track expression');off(55)
event(176,23,64);r:frame(false);check(not next(r.expression),'Hide clears a keyswitch/CC latch even without held notes')
local reload=R.new(w,{}, {},{},section,function() end)
check(reload.mapping.enabled and reload.mapping.articulationCC==23 and reload.mapping.switches and not next(reload.expression),'bindings persist while transient expression does not')
check(not event(176,23.5,20) and not event(144,60,99.5) and not event(176,23,100,1,3),'fractional/stale MIDI data rejects')
-- Auto writing is explicit, one transaction, timestamp compensated and compiled.
r.monitorTrack=nil;r:releaseInput();w.playing=true;w.native=true;w.awaiting=false;w.mode='song';w.beat=2
assert(r:set('latency',50));local history=#w.undoStack;assert(r:writeMappings());check(w.automationWriting,'explicit Auto write begins on a playing Studio track')
position=1.25
event(176,23,64,1,.075);event(176,21,32,1,.075)
check(not r:setMapping('pickCC',24),'binding edits cannot disturb an active write transaction')
assert(r:writeMappings());check(not w.automationWriting and #w.undoStack==history+1,'End write commits all controller curves as one undo step')
local t=w.project.tracks[1];local curve=t.automation.stringArticulation;local point
for _,p in ipairs(curve) do if p.v==2 then point=p end end
check(point and point.s==9 and point.linear==false,'CC capture removes event age plus input compensation and snaps to the existing 1/64 grid')
local saved=assert(F.project(w.project));check(saved.tracks[1].automation.stringArticulation[2].v==2,'mapping curves survive strict project persistence')
local plan=Sequence.compile(w.project,nil,MIDI,'song',F);check(Sequence.validate(plan.bytes,plan.length,plan.loop),'authored mapping curves compile to the native score clock')
assert(r:writeMappings());r:disconnect();check(not w.automationWriting and not next(r.expression),'disconnect finishes mapping write and clears transient overrides')
w:stop(true);check(not r:writeMappings(),'stopped transport cannot silently arm an Auto write')
w.cb.recordOptions=function() return {countIn=0,latency=0,untilStop=false} end
w.cb.recordStart=function(t) return r:start(t) end;w.cb.recordEnd=function() r:ended() end
assert(w:startTake(1));event(176,23,64);on(60)
check(ons[#ons].expression.stringArticulation==2 and w.recording.held[60],'note-take monitoring forwards the same expressive attack snapshot')
check(not r:setMapping('enabled',false),'binding configuration stays frozen during note capture')
now=now+.2;off(60);w:endTake(true)
check(messages[#messages]:find('take')~=nil,'note capture/configuration limitations are explicit to the player')
local snapshot=schema.strike{stringArticulation=2,stringSelect=7,pickDirection=1}
check(snapshot and not schema.strike{cutoff=100} and not schema.strike{stringSelect=8} and not schema.strike{pickDirection=.5},'live expression wire schema is bounded to three discrete fields')
print('PASS optional CC/keyswitch mapping, Learn/reserved channels, immutable attack snapshots, local persistence, sustain/target cleanup and explicit Auto capture; '..checks..' assertions')
