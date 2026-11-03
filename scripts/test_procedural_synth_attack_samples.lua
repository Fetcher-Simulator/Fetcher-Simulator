local root='files/server/server-lua-packages/fetcher.procedural-synth/';local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=dofile(root..'shared/schema.lua');local F=dofile(root..'shared/project.lua')(schema)
package.loaded[prefix..'shared.schema']=schema
package.loaded[prefix..'client.speech-catalog']={}
local Samples=dofile(root..'client/samples.lua');local Sets=dofile(root..'client/soundsets.lua')
local count=0;local function check(ok,message) count=count+1;assert(ok,message) end
local function preset(id) for _,p in ipairs(schema.builtins) do if p.id==id then return p end end end
local state=assert(preset('b:sampled-bass-expressive')).state;local ref=assert(schema.sample(state.sample))
check(ref.attacks.short.path:find('bass%-short') and ref.attacks.dead.path:find('bass%-ghost'),'expressive preset uses actual captured sources')
local key=schema.sampleKey(ref);ref.attacks.short.hash=string.rep('b',32)
check(schema.sampleKey(ref)~=key,'sample identity includes every attack fingerprint');ref=assert(schema.sample(state.sample))
local p=F.new();p.tracks[1]=F.newTrack('piano','Expression',16);p.tracks[1].params=state
check(F.project(p).tracks[1].params.sample.attacks.dead.hash==ref.attacks.dead.hash,'private/shared project validation retains attack maps')
for _,bad in ipairs{
 {attacks={unknown=ref.attacks.short}}, {attacks={}}, {attacks={short={}}},
 {attacks={short={path='sound/test.wav',offset=0,duration=1,root=60,oneShot=false,hash=string.rep('a',32)}}},
 {attacks={short=ref}}, {path='sound/test.wav'},
} do
 local copy=schema.sample(ref);for k,v in pairs(bad) do copy[k]=v end
 check(not schema.sample(copy),'malformed, nested, empty and non-bank attack maps reject')
end
local called={};local status='Loading';local synth={sampleInfo=function(path)
 called[path]=true;return {status=path==ref.attacks.short.path and status or 'Ready',hash=path==ref.path and ref.hash or path==ref.attacks.short.path and ref.attacks.short.hash or ref.attacks.dead.hash}
end}
check(Samples.status(synth,ref).status=='Loading' and called[ref.attacks.short.path] and called[ref.attacks.dead.path],'all attack banks preload and loading is exposed')
status='Ready';check(Samples.status(synth,ref).status=='Ready','all fingerprints must be ready')
ref.attacks.dead.hash=string.rep('f',32)
-- Keep the simulated installed identity unchanged after corrupting the save.
synth.sampleInfo=function(path) local original=state.sample;return {status='Ready',hash=path==original.path and original.hash or path==original.attacks.short.path and original.attacks.short.hash or original.attacks.dead.hash} end
check(Samples.status(synth,ref).status=='Mismatch','an altered auxiliary bank cannot silently change the instrument')
for _,set in ipairs{'captured-rock','captured-thrash','captured-funk','captured-clean'} do
 local guitar,id=Sets.assignment(set,'guitar',true,40,'electricguitar',30,'Rhythm')
 check(id:find('captured%-guitar') and guitar.sample.attacks.short,'captured sound set selects actual guitar palms')
 local bass=Sets.assignment(set,'bass',true,28,'bassguitar',33,'Bass')
 check(bass.sample.attacks.dead,'captured finger-bass identity remains expressive')
 local picked,pickedId=Sets.assignment(set,'bass',true,28,'bassguitar',34,'Bass')
 check(pickedId=='b:sampled-bass-picked' and not picked.sample.attacks,'picked identity is not relabeled fingerstyle')
 local nylon=Sets.assignment(set,'guitar',true,60,'lute',24,'Classical')
 check(nylon.sample.path:find('guitar%-nylon'),'acoustic identity remains acoustic')
 local timpani,timpaniId=Sets.assignment(set,'strings',true,60,'timpani',47,'Timpani')
 check(timpaniId=='b:orchestra-timpani','captured sets preserve pitched percussion')
end
print('PASS captured sample maps, bounded validation, persistence, identity/readiness and source-correct sound sets; '..count..' assertions')
