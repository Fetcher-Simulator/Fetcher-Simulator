-- Disposable native acceptance; regular port, copied character database/profiles.
local core=require('openmw.core');local self=require('openmw.self');local types=require('openmw.types');local nearby=require('openmw.nearby')
local I=require('openmw.interfaces');local synth=require('openmw.synth');local vfs=require('openmw.vfs')
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local F=require(prefix..'shared.project')(require(prefix..'shared.schema'));local Samples=require(prefix..'client.samples');local Studio=require(prefix..'client.studio')
local role,started,phase,at,view,browser,entryId=nil,nil,0,0,nil,nil,nil;local flags={};local path='sound/fetcher/synth/sample-slicing-qa.wav'
local function log(s) print('[StudioSampleEdgesQA] '..tostring(role)..' '..s) end
local function run()
 local now=core.getRealTime();started=started or now;role=role or types.NPC.record(self).name;assert(now-started<420,'Sample edge acceptance timed out');synth.setMonitorVolume(0)
 local api=I.ProceduralSynth;if not api then return end;local w=api.workstation();local l=api.sampleLibrary();local states=api.status()
 if role=='Synth B' then
  for _,state in ipairs(states) do if state.nativePlaying then assert(state.nativeFault==0,'Listener native fault');for _,c in ipairs(state.channels) do if c.voices>0 and c.rms>.00001 then flags.audio=true end end end end
  if flags.audio and flags.edges then phase=99;log('PASS complete: late listener receives fade/gain references and sounding native sample phrase');core.quit() end;return
 end
 if phase==0 then
  if now-started<4 then return end
  if not w then for _,o in ipairs(nearby.activators) do if o.recordId=='fetcher_procedural_synth_v1' and o.position.x<2400 then o:activateBy(self);return end end;return end
  if not api.projectLibrary().ready or api.projectLibrary().pending then return end
  assert(synth.version>=41);local info=synth.sampleInfo(path,0,2);if info.status~='Ready' then assert(info.status=='Loading',info.error);return end
  assert(info.peak>0 and info.peak<1,'Native cached raw peak');flags.peak=info.peak;flags.hash=info.hash
  browser=Samples.new(synth,vfs,self);assert(browser:loadReference{path=path,offset=0,duration=2,root=48,oneShot=true,hash=info.hash,reverse=true,gain=.75})
  local p=F.new();p.name='Private sample edges acceptance';p.autoBand=false;p.leaderPlays=false;p.composition={bars=4,numerator=4,denominator=4,loopStart=1,loopEnd=4,loop=true};p.timeline={beats=16,tempos={{beat=0,tempo=500000}}}
  p.tracks[1]=F.newTrack('piano','Original sample',16);p.tracks[1].params.sample=assert(browser:reference());p.tracks[1].mute=true;assert(w:load(p));w.cursorBeat=4;w.selected=1;w.page='samples';api.hide()
  browser.editView={key=browser:viewKey(),first=0,span=2,caret=.7,trimFirst=0,trimLast=2,points={.55,1.1,1.6},sensitivity=.68,spacing=.12,snap=false}
  view=Studio.new(w,{midiLabel=function() return 'Private source edges' end,hide=function() end,stop=function() w:stop(true) end,playSong=function() w:play('song') end,playClips=function() w:play('clips') end,transport=function(a) w:transportAction(a) end})
  view.samples=browser;view:setSampleFocus(true);view:render();phase=1;at=now;log('READY visual: drag both lower green fade handles inward, then Normalize -1 dB')
 elseif phase==1 then
  view:frame(now)
  if (browser.fadeIn or 0)>.09 and (browser.fadeOut or 0)>.09 and (browser.gain or 1)~=.75 then
   assert(browser.gain*flags.peak<=10^(-1/20)+.000001,'Normalize must retain source headroom')
   flags.fadeIn,flags.fadeOut,flags.gain=browser.fadeIn,browser.fadeOut,browser.gain
   view:history(false);assert(browser.gain==.75 and browser.fadeIn==flags.fadeIn,'Source Undo is grouped independently');view:history(true);assert(browser.gain==flags.gain)
   phase=2;log('READY Edit slice: source fades/normalization/Undo verified; click Edit slice')
  end
 elseif phase==2 then
  view:frame(now)
  if browser.sliceParent and browser:info().status=='Ready' then
   assert(browser.duration==.55 and browser.offset==.9 and (browser.fadeIn or 0)+(browser.fadeOut or 0)<=.55+1e-7,'Extracted reversed slice bounds')
   phase=3;log('READY Back to source: bounded independent slice loaded; click Back to source')
  end
 elseif phase==3 then
  view:frame(now)
  if not browser.sliceParent and browser:info().status=='Ready' and browser.editView then
   assert(browser.duration==2 and browser.editView.points[2]==1.1 and browser.fadeIn==flags.fadeIn and browser.gain==flags.gain)
   l.name='Private source fades kit';assert(l:save(browser,'kit',false));phase=4
  end
 elseif phase==4 then
  view:frame(now);if l.pending then return end
  assert(l.selected and l.selected.recipe.source.fadeIn==flags.fadeIn and l.selected.recipe.source.fadeOut==flags.fadeOut)
  entryId=l.selected.id;view:openSampleLibrary();browser:select(path);assert(l:open(entryId,browser));phase=5
 elseif phase==5 then
  view:frame(now);if l.pending or browser.pendingRecipe then return end
  assert(browser.fadeIn==flags.fadeIn and browser.fadeOut==flags.fadeOut and browser.gain==flags.gain and browser:info().hash==flags.hash,'library restore: '..tostring(l.status)..' / '..tostring(browser.fadeIn)..' / '..tostring(browser.fadeOut)..' / '..tostring(browser.gain))
  local refs=assert(browser:slices(browser.editView.points));for _,ref in ipairs(refs) do assert(ref.fadeIn and ref.fadeOut and ref.fadeIn+ref.fadeOut<=ref.duration+1e-7) end
  assert(w:setSample(1,assert(browser:reference())));assert(w:addSampleSlices(refs,'Faded phrase',true));view:destroy();view=nil;phase=6
 elseif phase==6 then
  if not w:prepareSamples() then return end;assert(w:play('song'));phase=7;at=now;log('READY own edges: library restored fades/root/gain; arranged bounded faded slices are playing')
 elseif phase==7 then
  if now-at<20 or api.projectLibrary().pending then return end
  for _,state in ipairs(states) do assert(state.nativeFault==0,'Owner native fault') end
  assert(api.projectLibrary():save(true,'Private faded phrase project'));phase=8;at=now
 elseif phase==8 then
  if now-at<1 or api.projectLibrary().pending then return end
  assert(api.projectLibrary():open(api.projectLibrary().active,false,true));phase=9;at=now
 elseif phase==9 then
  if now-at<1 or api.projectLibrary().pending then return end
  assert(#w.project.tracks==5 and w.project.tracks[1].params.sample.fadeIn==flags.fadeIn)
  for n=2,5 do local ref=w.project.tracks[n].params.sample;assert(ref.fadeIn and ref.fadeOut and ref.hash==flags.hash and ref.gain==flags.gain) end
  api.close();phase=99;log('PASS complete: native fade drags, cached-peak normalization, local Undo/Redo, slice extraction/back, saved kit and project round trips, native phrase relay/audio');core.quit()
 end
end
return {engineHandlers={onFrame=function() if phase==99 then return end;local ok,err=pcall(run);if not ok then phase=99;log('FAIL '..tostring(err));core.quit() end end},eventHandlers={Synth_Message=function(d)
 local count=0;for _,c in pairs(d.channels or {}) do if c.params and c.params.sample and c.params.sample.path==path and c.params.sample.fadeIn and c.params.sample.fadeOut then count=count+1 end end;if count>=4 then flags.edges=true end
end}}
