local root='files/server/server-lua-packages/fetcher.procedural-synth/'
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=dofile(root..'shared/schema.lua');package.loaded[prefix..'shared.schema']=schema
local catalog=dofile(root..'client/speech-catalog.lua');package.loaded[prefix..'client.speech-catalog']=catalog
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
local synth={sampleInfo=function() return {status=state,hash=string.rep('a',32),duration=2} end,
    create=function() previews=previews+1;return {set=function() end,sample=function() end,noteOn=function() end,
        keepAlive=function() end,close=function() stopped=stopped+1 end} end}
local paths={'sound/vo/d/m/hlo_dm001.mp3','sound/fx/test.wav','music/test.ogg'}
local browser=Samples.new(synth,{pathsWithPrefix=function(prefix)
    local n=0;return function() repeat n=n+1 until not paths[n] or paths[n]:sub(1,#prefix)==prefix;return paths[n] end
end},{})
browser:start();browser:frame();check(#browser:entries()==1,'voice category excludes effects and music')
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
state='Loading';check(not w:prepareSamples(),'owner waits for pending loading before transport')
state='Unavailable';check(w:prepareSamples(),'missing sample does not block unrelated parts')
check(not w:setSample(1,mismatch),'mismatch cannot replace current sound')
print('PASS sample validation, /s metadata, VFS categories, preview, identity, part assignment, persistence and readiness; '..checks..' assertions')
