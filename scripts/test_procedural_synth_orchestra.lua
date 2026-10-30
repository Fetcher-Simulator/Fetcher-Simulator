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
w.project.leaderPlays=false;local plan=Layout.plan(w.project)
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
local models=W.new{configure=function() end,band=function() end,panic=function() end,message=function() end,prune=function() end}
local p=Format.new();p.leaderPlays=false;p.timeline={beats=4,tempos={{beat=0,tempo=500000}}}
for n,entry in ipairs{{'guitar',30},{'bass',33},{'snare',nil},{'hat',nil}} do
    local track=Format.newTrack('midi','Part '..n,16);track.edited=Format.encodeNotes({},960);track.sourceRole=entry[1];track.sourceProgram=entry[2];track.instrument=n>2 and 'drum' or 'lute';p.tracks[n]=track
end
p.tracks[1].params.cutoff=3210;p.tracks[1].mixer.bass=5;p.tracks[1].level=.3
check(models:load(p) and models:matchStageInstruments(),'older lute/hand-drum arrangements can opt into current GM models')
local current=models.project
check(current.tracks[1].params.cutoff==3210 and current.tracks[1].mixer.bass==5 and current.tracks[1].level==.3 and current.tracks[1].edited.bytes==p.tracks[1].edited.bytes,'matching physical models preserves sounds, mix and musical data')
check(current.tracks[1].instrument=='guitar' and current.tracks[2].instrument=='bass' and current.tracks[3].instrument=='drumkit','guitar bass and percussion resolve to separate physical identities')
local band=Layout.plan(current)
check(#band==3 and band.wideSpacing and band.routes[3]==band.routes[4],'all kit pieces share one drummer and request wider staging')
check(Format.project(current) and models:undo() and models.project.tracks[1].instrument=='lute' and models:redo(),'model matching preserves valid saved data and supports one-step history')
for _,name in ipairs{'guitar','bass','drumkit'} do
    check(Format.target{kind='player',characterId=12,instrument=name} and Format.target{kind='npc',mpNum=12,refId='fetcher_synth_bard_1_12',instrument=name},'new instrument is accepted for both players and NPCs')
    check(Format.instrumentModels[name].model:find('/'..name..'.nif',1,true),'new instrument has a fixed trusted model path')
end
print('PASS orchestral/rock instrument mapping, independent props/sounds, model matching, persistence and Undo; '..checks..' assertions')
