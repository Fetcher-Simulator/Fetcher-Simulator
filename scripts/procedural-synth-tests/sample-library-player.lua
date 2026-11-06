-- Native acceptance uses a copied database/profile on the regular private port.
local core=require('openmw.core');local self=require('openmw.self');local types=require('openmw.types');local nearby=require('openmw.nearby')
local I=require('openmw.interfaces');local synth=require('openmw.synth');local vfs=require('openmw.vfs')
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local F=require(prefix..'shared.project')(require(prefix..'shared.schema'));local Samples=require(prefix..'client.samples');local Studio=require(prefix..'client.studio')
local role,started,phase,at,view,browser,entryId,baseline= nil,nil,0,0,nil,nil,nil,nil
local flags={};local path='sound/fetcher/synth/sample-slicing-qa.wav';local name='Private native saved reverse kit'
local function log(s) print('[StudioSampleLibraryQA] '..tostring(role)..' '..s) end
local function activate()
 for _,o in ipairs(nearby.activators) do if o.recordId=='fetcher_procedural_synth_v1' and (role=='Synth A' and o.position.x<2400 or role=='Synth B' and o.position.x>2400) then o:activateBy(self);return end end
end
local function run()
 local now=core.getRealTime();started=started or now;role=role or types.NPC.record(self).name
 assert(now-started<420,'Sample library acceptance timed out');synth.setMonitorVolume(0)
 local api=I.ProceduralSynth;if not api then return end
 local w=api.workstation();local l=api.sampleLibrary();local states=api.status()
 if role=='Synth B' then
  for _,state in ipairs(states) do if state.nativePlaying then assert(state.nativeFault==0,'Listener native fault');for _,c in ipairs(state.channels) do if c.voices>0 and c.rms>.0001 then flags.audio=true end end end end
  if not w then if now-started>3 then activate() end;return end
  if not l.ready and not l.pending then l:list();return end
  if l.pending or not l.ready then return end
  for _,entry in ipairs(l.entries) do assert(entry.name~=name,'Owner sample library leaked to listener') end
  if flags.audio and flags.slices then phase=99;log('PASS complete: private character library isolated; late listener receives legitimate phrase references and native audio');core.quit() end
  return
 end
 if phase==0 then
  if now-started<4 then return end
  if not w then activate();return end
  if not api.projectLibrary().ready or api.projectLibrary().pending then return end
  local info=synth.sampleInfo(path,0,2);if info.status~='Ready' then assert(info.status=='Loading',info.error);return end
  browser=Samples.new(synth,vfs,self);assert(browser:loadReference{path=path,offset=0,duration=2,root=48,oneShot=true,hash=info.hash,reverse=true,gain=.75})
  local p=F.new();p.name='Private sample library acceptance';p.autoBand=false;p.leaderPlays=false;p.composition={bars=4,numerator=4,denominator=4,loopStart=1,loopEnd=4,loop=true};p.timeline={beats=16,tempos={{beat=0,tempo=500000}}}
  p.tracks[1]=F.newTrack('piano','Original sample',16);p.tracks[1].params.sample=assert(browser:reference());p.tracks[1].mute=true;assert(w:load(p));w.cursorBeat=4;w.selected=1;api.hide()
  browser.editView={key=browser:viewKey(),first=0,span=2,caret=0,trimFirst=0,trimLast=2,points={.55,1.1,1.6},sensitivity=.68,spacing=.12,snap=false}
  view=Studio.new(w,{midiLabel=function() return 'Private saved-sample acceptance' end,hide=function() end,stop=function() w:stop(true) end,playSong=function() w:play('song') end,playClips=function() w:play('clips') end,transport=function(a) w:transportAction(a) end})
  view.samples=browser;view:setSampleFocus(true);view:openSampleLibrary('kit');l.name=name;view:render();phase=1;at=now;log('READY visual: Save new kit in personal sample library')
 elseif phase==1 then
  view:frame(now)
  if l.selected and l.selected.name==name and not l.pending then
   local r=l.selected.recipe;assert(r.source.root==48 and r.source.reverse and r.source.gain==.75 and #r.markers==3 and not r.settings.snap)
   entryId=l.selected.id;baseline=#l.entries;browser.root=72;assert(l:open(entryId,browser));phase=2;at=now
  end
 elseif phase==2 then
  view:frame(now);if l.pending or browser.pendingRecipe then return end
  assert(browser.root==48 and browser.editView.points[2]==1.1 and not browser.editView.snap)
  flags.refs=assert(browser:slices(browser.editView.points));assert(#flags.refs==4 and flags.refs[1].offset==1.45 and flags.refs[4].offset==0)
  assert(w:addSampleSlices(flags.refs,'Saved kit',false));assert(#w.project.tracks==5);assert(w:undo() and #w.project.tracks==1)
  assert(w:addSampleSlices(flags.refs,'Saved phrase',true));assert(#w.project.tracks==5);view:destroy();view=nil
  phase=3;at=now
 elseif phase==3 then
  if not w:prepareSamples() then return end
  assert(w:play('song'));phase=4;at=now;log('READY own samples: saved reverse kit reopened, created drum lanes with Undo, and arranged original-timing phrase')
 elseif phase==4 then
  if now-at<20 then return end
  for _,state in ipairs(states) do assert(state.nativeFault==0,'Owner native fault') end
  api.close();phase=5;at=now
 elseif phase==5 then
  if now-at<3 then return end
  if not w then activate();return end
  if not l.ready and not l.pending then l:list();return end
  if not l.ready or l.pending then return end
  assert(#l.entries==baseline,'saved entries must survive closing and reacquiring keyboard')
  local b=Samples.new(synth,vfs,self);l.browser=b;browser=b;assert(l:open(entryId,b));phase=6;at=now
 elseif phase==6 then
  browser:frame();if l.pending or browser.pendingRecipe then return end
  assert(browser.root==48 and browser.reverse and browser.editView.points[2]==1.1 and not browser.editView.snap,'per-character recipe persistence')
  l.name='Private native renamed kit';assert(l:rename());phase=7;at=now
 elseif phase==7 then
  if l.pending then return end
  assert(l.selected.name=='Private native renamed kit' and l.selected.revision==2,'native rename revision')
  assert(not l:delete() and l:delete());phase=8;at=now
 elseif phase==8 then
  if l.pending then return end
  assert(not l.selected and #l.entries==baseline-1,'native delete updates only private library')
  assert(w:newSong(120,4,4,4));assert(api.sound(1));flags.cutoff=w.project.tracks[1].params.cutoff;flags.generation=w.generation
  phase=9;at=now;log('READY Synth Projects: click Projects on the Synth page')
 elseif phase==9 then
  if w.page=='projects' then
   assert(w.generation==flags.generation and w.project.tracks[1].params.cutoff==flags.cutoff,'Projects changed the sound')
   api.close();phase=99;log('PASS complete: native UI save, private SQLite recipe round trip/reacquire, markers/reverse/root, lanes/phrase/history, rename/delete and Synth Projects navigation');core.quit()
  end
 end
end
return {engineHandlers={onFrame=function() if phase==99 then return end;local ok,err=pcall(run);if not ok then phase=99;log('FAIL '..tostring(err));core.quit() end end},eventHandlers={Synth_Message=function(d)
 local count=0;for _,c in pairs(d.channels or {}) do if c.params and c.params.sample and c.params.sample.path==path then count=count+1 end end
 if count>=4 then flags.slices=true end
end}}
