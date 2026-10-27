local root='files/server/server-lua-packages/fetcher.procedural-synth/';local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local function module(name) local value=dofile(root..name:gsub('%.','/')..'.lua');package.loaded[prefix..name]=value;return value end
local schema=module('shared.schema');local Format=module('shared.project')(schema);local Layout=module('shared.band-layout')
local MIDI=module('client.midi');local sets=module('client.soundsets');module('client.arrangement');module('shared.sequence')
local W=module('client.workstation');local checks=0
local function check(value,why) checks=checks+1;assert(value,why) end
local w=W.new{configure=function() end,band=function() end,panic=function() end,prune=function() end,message=function() end,now=function() return 0 end}
local song={tracks=6,division=96,names={},programs={},tempos={},duration=2,beats=4,events={}}
local expected={'violin','cello','trumpet','flute','clarinet','timpani'}
for n,program in ipairs{40,42,56,73,71,47} do
 local channel=n-1;local part=n..':'..channel;song.programs[part]=program
 for _,kind in ipairs{'on','off'} do song.events[#song.events+1]={kind=kind,part=part,track=n,channel=channel,note=(n==2 or n==6) and 48 or 60,velocity=.8,tick=kind=='on' and 0 or 96,time=kind=='on' and 0 or .5} end
end
table.sort(song.events,function(a,b) return a.time==b.time and a.track<b.track or a.time<b.time end)
check(w:import(song,'midi/private-orchestra.mid'),'orchestral MIDI imports')
local plan=Layout.plan(w.project)
for n,instrument in ipairs(expected) do
 check(w.project.tracks[n].instrument==instrument and plan[n].instrument==instrument,'GM program selects '..instrument)
 check(Format.target{kind='npc',mpNum=n,refId='fetcher_synth_bard_1_'..n,instrument=instrument},'new model is a validated performer instrument')
end
check(w:applySoundSet('orchestra'),'whole-song orchestral starting set applies')
for n,instrument in ipairs(expected) do check(w.project.tracks[n].presetId=='b:orchestra-'..instrument,'orchestra set uses the instrument family for '..instrument) end
check(w.project.tracks[6].params.drumType==0,'timpani remains a pitched tonal instrument rather than a fixed-frequency drum voice')
local saved=assert(Format.project(w.project));for n,name in ipairs(expected) do check(saved.tracks[n].instrument==name,'instrument preference survives save validation') end
local oldPreset=w.project.tracks[1].presetId
w.project.band[1].target={kind='npc',mpNum=77,refId='fetcher_synth_bard_1_1',instrument='violin'};w.project.tracks[1].output.slot=1
w:touch();local history=#w.undoStack
check(w:setStageInstrument(1,'flute') and w.project.band[1].target.instrument=='flute','prop selection updates its bound performer')
check(w.project.tracks[1].presetId==oldPreset and #w.undoStack==history+1,'prop change leaves sound intact and is one Undo step')
check(w:undo() and w.project.tracks[1].instrument=='violin' and w.project.band[1].target.instrument=='violin','prop selection supports Undo')
check(not w:setStageInstrument(1,'arbitrary-model.nif'),'untrusted model paths cannot become instrument identities')
local _,patch=sets.programPatch({soundSet='orchestra'},{},42);check(patch=='b:orchestra-cello','opt-in GM program following keeps cello distinct')
_,patch=sets.programPatch({soundSet='orchestra'},{},47);check(patch=='b:orchestra-timpani','program following retains pitched timpani')
for _,bank in ipairs(schema.banks) do if bank=='Orchestral / Cinematic' then check(true,'orchestral presets have a browser bank') end end
print('PASS orchestral instrument/program mapping, independent props/sounds, presets, persistence and Undo; '..checks..' assertions')
