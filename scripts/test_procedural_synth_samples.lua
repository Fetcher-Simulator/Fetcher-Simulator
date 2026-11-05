local root='files/server/server-lua-packages/fetcher.procedural-synth/'
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=dofile(root..'shared/schema.lua');package.loaded[prefix..'shared.schema']=schema
local catalog=dofile(root..'client/speech-catalog.lua');package.loaded[prefix..'client.speech-catalog']=catalog
package.loaded[prefix..'client.voice-text']=dofile(root..'client/voice-text.lua')
package.loaded[prefix..'shared.voice-transcripts']=dofile(root..'shared/voice-transcripts.lua')
package.loaded[prefix..'client.midi']=dofile(root..'client/midi.lua')
package.loaded[prefix..'shared.project']=dofile(root..'shared/project.lua')
package.loaded[prefix..'client.soundsets']=dofile(root..'client/soundsets.lua')
local Samples=dofile(root..'client/samples.lua')
local W=dofile(root..'client/workstation.lua')
local checks=0;local function check(b,m) checks=checks+1;assert(b,m) end
local ref={path='sound/vo/d/m/hlo_dm001.mp3',offset=0.125,duration=2,root=60,oneShot=true,hash=string.rep('a',32)}
check(schema.state{sample=ref}.sample.offset==.125,'sample snapshot accepted')
for _,changes in ipairs{{path='sound/../secret.wav'},{path='C:/test.wav'},{path='sound/UPPER.wav'},
    {offset=0/0},{duration=31},{root=60.5},{hash='bad'},{oneShot=1},{extra=true}} do
    local invalid={};for k,v in pairs(ref) do invalid[k]=v end;for k,v in pairs(changes) do invalid[k]=v end
    check(not schema.state{sample=invalid},'invalid sample rejected')
end
local state='Loading';local previews=0;local stopped=0
local synth={version=38,sampleInfo=function() return {status=state,hash=string.rep('a',32),duration=2} end,
    create=function() previews=previews+1;return {set=function() end,sample=function() end,noteOn=function() end,
        keepAlive=function() end,noteOff=function() end,close=function() stopped=stopped+1 end} end}
local paths={'sound/vo/d/m/hlo_dm001.mp3','sound/fx/test.wav','music/test.ogg'}
local browser=Samples.new(synth,{pathsWithPrefix=function(prefix)
    local n=0;return function() repeat n=n+1 until not paths[n] or paths[n]:sub(1,#prefix)==prefix;return paths[n] end
end},{},{voice={records={{infos={{sound='Vo/D/M/hlo_dm001.mp3',text='Quiet voices hide secrets.'}}}}}})
browser:start();browser:frame();check(#browser:entries()==1,'voice category excludes effects and music')
check(browser:entries()[1].search:find('Quiet voices',1,true),'sample search includes installed subtitle text')
check(browser:entries()[1].label:find('hlo_dm001.mp3',1,true),'voice rows retain the filename beside the spoken line')
check(catalog[ref.path] and catalog[ref.path]:find('hello'),'reuses /s race/gender/type metadata')
browser.category='All';check(#browser:entries()==3,'loose/archive VFS catalog categories')
browser:select(ref.path);check(not browser:audition() and previews==0,'loading cannot audition or assign')
state='Ready';check(browser:audition() and previews==1,'ready sample audition')
browser:stop();check(stopped==1,'preview close disposes native source')
local mismatch={};for k,v in pairs(ref) do mismatch[k]=v end;mismatch.hash=string.rep('b',32)
check(Samples.status(synth,mismatch).status=='Mismatch','per-listener file identity mismatch')
local configured,message=0,''
local w=W.new{configure=function() configured=configured+1 end,message=function(s) message=s end,
    sampleStatus=function(s) return Samples.status(synth,s) end,panic=function() end}
w:add('piano');local notes=w.project.tracks[1].notes;local output=w.project.tracks[1].output
check(w:setSample(1,ref) and w.project.tracks[1].params.sample.path==ref.path,'assign sample to part')
check(w.project.tracks[1].notes==notes and w.project.tracks[1].output==output,'assignment retains notes and physical route')
local project=dofile(root..'shared/project.lua')(schema)
check(project.project(w.project).tracks[1].params.sample.hash==ref.hash,'project round trip preserves identity/trim')
check(w:undo() and not w.project.tracks[1].params.sample and w:redo() and w.project.tracks[1].params.sample,'sample undo/redo')
local edited={path=ref.path,offset=ref.offset,duration=2,root=60,oneShot=false,hash=ref.hash,reverse=true,loop={start=.2,finish=1.8,crossfade=.02}}
check(w:setSample(1,edited) and project.project(w.project).tracks[1].params.sample.loop.finish==1.8,'reverse/loop edit survives complete project validation')
check(w:undo() and not w.project.tracks[1].params.sample.reverse and w:redo() and w.project.tracks[1].params.sample.reverse,'reverse/loop edits support Undo/Redo together')
check(schema.sampleKey(edited)~=schema.sampleKey(ref),'playback edits reconfigure the sound without changing its file hash')
for _,loop in ipairs{{start=.2,finish=.205,crossfade=0},{start=.2,finish=3,crossfade=0},{start=.2,finish=1,crossfade=.5},
    {start=0/0,finish=1,crossfade=0},{start=.2,finish=1,crossfade=.02,extra=1}} do
 local bad={};for k,v in pairs(edited) do bad[k]=v end;bad.loop=loop;check(not schema.sample(bad),'malformed loop rejected before save or relay')
end
local bad={};for k,v in pairs(edited) do bad[k]=v end;bad.oneShot=true;check(not schema.sample(bad),'one-shot cannot carry an endless sustain loop')
bad.oneShot=false;bad.reverse=1;check(not schema.sample(bad),'direction is boolean only')
check(browser:loadReference(edited) and browser.loop.crossfade==.02 and browser.reverse,'editing a part loads its complete playback view')
check(browser:audition() and browser:release(),'loop preview can be explicitly released')
check(browser:trim(.1,1.5) and not browser.loop and browser.reverse,'changing trim clears stale loop bounds while keeping reverse')
browser:select(ref.path);check(not browser.loop and not browser.reverse and not browser.gain,'choosing a new source clears previous playback edits')
check(browser:toggleLoop() and not browser.oneShot and browser.loop.finish==2,'loop default uses actual decoded length rather than requested 30-second cap')
check(not browser:setLoop(0,3,.02),'loop editing cannot extend beyond the decoded file')
check(browser:toggleLoop() and not browser.loop,'loop can be disabled without changing the source')
local old={version=37,sampleInfo=synth.sampleInfo};check(Samples.status(old,edited).status=='Unavailable','older client reports unsupported playback edits')
local short={version=38,sampleInfo=function() return {status='Ready',hash=ref.hash,duration=.5} end}
check(Samples.status(short,edited).status=='Unavailable','matching file identity still validates decoded loop bounds')
local waveCalls=0;synth.sampleWaveform=function() waveCalls=waveCalls+1;return {hash=ref.hash,duration=2,minimum={-.1},maximum={.2}} end
check(browser:wave() and browser:wave() and waveCalls==1,'waveform is cached across frames instead of recreating native peak tables')
browser:trim(.2,1.2);check(browser:wave() and waveCalls==2,'changing the trim invalidates waveform cache')
browser:select(ref.path);browser.duration=2;local cuts=assert(browser:chops(4))
check(#cuts==4 and cuts[1].offset==0 and cuts[4].offset==1.5 and cuts[4].duration==.5,'equal chops cover the actual excerpt without gaps or extra files')
browser.reverse=true;cuts=assert(browser:chops(4));check(cuts[1].offset==1.5 and cuts[4].offset==0 and cuts[1].reverse,'reversed chops follow displayed playback order')
browser.offset=600;check(not browser:chops(4),'slices outside the bounded source start range fail cleanly')
browser:select(ref.path);browser.duration=.02;check(not browser:chops(16),'slices shorter than 10ms are rejected')
state='Loading';check(not w:prepareSamples(),'owner waits for pending loading before transport')
state='Unavailable';check(w:prepareSamples(),'missing sample does not block unrelated parts')
check(not w:setSample(1,mismatch),'mismatch cannot replace current sound')
local bank
for _,p in ipairs(schema.builtins) do if p.id=='b:sampled-electric-clean' then bank=p end end
check(bank and schema.isBank(bank.state.sample),'recorded bank is a normal validated sound preset')
for _,changes in ipairs{{path='sound/foreign/bank.fsb'},{offset=.1},{duration=2},{root=61},{oneShot=true}} do
 local ref={};for k,v in pairs(bank.state.sample) do ref[k]=v end;for k,v in pairs(changes) do ref[k]=v end
 check(not schema.state{sample=ref},'bank cannot become an arbitrary excerpt or path')
end
w.project.tracks[1].followPrograms=true
check(w:preset(1,bank.id) and not w.project.tracks[1].followPrograms,'recorded preset disables incompatible program following')
check(project.project(w.project).tracks[1].params.sample.hash==bank.state.sample.hash,'bank reference survives project validation')
check(w:undo() and w:redo() and schema.isBank(w.project.tracks[1].params.sample),'bank assignment supports undo and redo')
state='Ready';check(Samples.status(synth,bank.state.sample).status=='Mismatch','different installed bank is explicitly mismatched')
print('PASS sample validation, /s metadata, VFS categories, preview, identity, part assignment, persistence and readiness; '..checks..' assertions')
local kitIndex
for n,kit in ipairs(schema.kits) do if kit.name=='Recorded Acoustic' then kitIndex=n end end
local kitW=W.new{configure=function() end,message=function() end,panic=function() end}
check(kitIndex and kitW:addKit(kitIndex) and #kitW.project.tracks==10,'recorded kit adds ten independently mixed lanes')
for _,track in ipairs(kitW.project.tracks) do check(schema.isBank(track.params.sample) and track.note~=60,'recorded kit preserves GM trigger pitches') end
check(kitW:preset(4,'b:sampled-drum-kit') and kitW.project.tracks[4].note==46,'choosing a bank preset keeps open-hat pitch instead of reference root 60')
check(project.project(kitW.project).tracks[4].params.sample.hash=='57fde34599e1a0562d255490162fecc2','recorded kit persists exact bank identity')
check(kitW:undo() and kitW:undo() and #kitW.project.tracks==0,'kit creation and preset edit remain undoable')
print('PASS recorded kit lane pitches, independent tracks, source identity and history')
local articulations=0
for _,p in ipairs(schema.builtins) do if p.state.sample and p.role~='Drums' then
    check(schema.sampledStrings[p.state.sample.path],'all built-in sampled strings expose articulation controls')
    if p.state.stringMute>0 then
        articulations=articulations+1
        check((p.midiProgram==28 or p.id=='b:sampled-thrash-rhythm' and p.midiProgram==30) and p.state.release<=.025,'muted recorded presets retain technique identity and short gate release')
    end
end end
check(articulations==4,'recorded metal, rock, funk and thrash have distinct damping starting points')
check(w:preset(1,'b:sampled-electric-muted') and project.project(w.project).tracks[1].params.stringMute==.7,'muted bank sound persists through project validation')
check(w:undo() and w:redo() and w.project.tracks[1].params.stringDecay==.8,'modeled mute controls survive Undo/Redo')
for _,id in ipairs{'b:sampled-bass-short','b:sampled-bass-ghost'} do
 check(w:preset(1,id) and schema.isBank(w.project.tracks[1].params.sample),'captured bass attack can be selected as a normal sound')
 local ref=w.project.tracks[1].params.sample
 check(project.project(w.project).tracks[1].params.sample.hash==ref.hash,'captured attack identity persists')
 check(w:undo() and w:redo() and w.project.tracks[1].params.sample.hash==ref.hash,'captured attack selection supports Undo/Redo')
 check(w.project.tracks[1].params.stringMute==0 and w.project.tracks[1].params.pickAttack==0,'captured attacks do not acquire synthetic damping or pick transients')
end
