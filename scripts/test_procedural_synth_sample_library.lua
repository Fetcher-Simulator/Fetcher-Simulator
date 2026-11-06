local root='files/server/server-lua-packages/fetcher.procedural-synth/'
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=dofile(root..'shared/schema.lua');local factory=dofile(root..'shared/sample-library.lua');local format=factory(schema)
package.loaded[prefix..'shared.schema']=schema;package.loaded[prefix..'shared.sample-library']=factory
for _,name in ipairs{'speech-catalog','voice-text'} do package.loaded[prefix..'client.'..name]=dofile(root..'client/'..name..'.lua') end
package.loaded[prefix..'shared.voice-transcripts']=dofile(root..'shared/voice-transcripts.lua')
local Samples=dofile(root..'client/samples.lua');local Library=dofile(root..'client/sample-library.lua')
local n=0;local function check(b,m) n=n+1;assert(b,m) end
local function copy(t) if type(t)~='table' then return t end;local out={};for k,v in pairs(t) do out[k]=copy(v) end;return out end
local ref={path='sound/test.wav',offset=.2,duration=2,root=48,oneShot=false,hash=string.rep('a',32),gain=.7,reverse=true,loop={start=.1,finish=1.8,crossfade=.02}}
local recipe={version=1,kind='kit',source=ref,span=2,markers={.25,.6,1.5},settings={sensitivity=.75,spacing=.12,snap=false}}
local canonical=assert(format.recipe(recipe));check(canonical.source~=ref and canonical.markers~=recipe.markers and not canonical.settings.snap,'validated recipes own independent references and settings')
for _,change in ipairs{{version=2},{kind='url'},{span=0/0},{span=31},{markers={.001}},{markers={1,.5}},{markers={.2,.205}},{markers={1.999}},{settings={sensitivity=2,spacing=.1,snap=true}},{settings={sensitivity=.5,spacing=.001,snap=true}},{url='https://bad.test'}} do
 local bad=copy(recipe);for k,v in pairs(change) do bad[k]=v end;check(not format.recipe(bad),'malformed recipes rejected before storage')
end
for _,change in ipairs{{hash=false},{path='sound/../secret.wav'},{duration=31}} do
 local bad=copy(recipe);for k,v in pairs(change) do bad.source[k]=v end;check(not format.recipe(bad),'unsafe or unidentified references rejected')
end
for _,preset in ipairs(schema.builtins) do if schema.isBank(preset.state.sample) then local bad=copy(recipe);bad.source=preset.state.sample;check(not format.recipe(bad),'multisample banks are not raw excerpts');break end end
local storage,fail={},false
local server=dofile('apps/openmw-server/scripts/procedural_synth_samples.lua')({
 getCharacterStorageForCharacter=function(id,namespace,key) check(namespace=='ProceduralSynth' and key=='samples','separate character namespace');return storage[id] end,
 setCharacterStorageForCharacter=function(id,_,_,value) if fail then return false end;storage[id]=copy(value);return true end,
},format)
local serial=0;local function request(op,data,id) serial=serial+1;data=copy(data or {});data.op=op;data.requestId=serial;return server.handle(id or 11,data) end
check(#request('sampleList').entries==0 and storage[11]==nil,'listing empty library does not mutate storage')
local saved=request('sampleSave',{entryId=0,name='Reverse kit',recipe=recipe})
check(saved.entry.id==1 and saved.entry.revision==1 and saved.entries[1].slices==4 and not saved.entries[1].recipe,'listing exposes metadata without entire recipes')
check(#request('sampleList',nil,22).entries==0,'other characters cannot see entries')
local open=request('sampleOpen',{entryId=1}).entry
check(open.recipe.source.root==48 and open.recipe.source.loop.finish==1.8 and not open.recipe.settings.snap,'trim/root/gain/reverse/loop/markers/settings survive storage')
local renamed=request('sampleRename',{entryId=1,entryRevision=1,name='Named kit'})
check(renamed.entry.revision==2 and storage[11].entries[1].name=='Named kit','rename increments revision')
check(not pcall(request,'sampleSave',{entryId=1,entryRevision=1,name='Old',recipe=recipe}) and storage[11].entries[1].revision==2,'stale saves cannot overwrite newer entries')
fail=true;check(not pcall(request,'sampleDelete',{entryId=1,entryRevision=2}) and #storage[11].entries==1,'failed deletion leaves original storage intact')
check(not pcall(request,'sampleRename',{entryId=1,entryRevision=2,name='Failed'}) and storage[11].entries[1].name=='Named kit','failed rename cannot mutate aliased original tables')
check(not pcall(request,'sampleSave',{entryId=0,name='Failed',recipe=recipe}) and storage[11].nextId==2,'failed new save does not consume an ID');fail=false
request('sampleDelete',{entryId=1,entryRevision=2});check(#storage[11].entries==0 and storage[11].nextId==2,'delete retains monotonic IDs')
for _,bad in ipairs{false,{version=99},{version=1,nextId=1,entries={{id=1,revision=1,name='Invalid',recipe=recipe}}}} do storage[33]=bad;check(not pcall(request,'sampleList',nil,33) and storage[33]==bad,'unsupported storage remains intact') end
for i=1,64 do request('sampleSave',{entryId=0,name='Kit '..i,recipe=recipe},44) end
check(not pcall(request,'sampleSave',{entryId=0,name='Overflow',recipe=recipe},44) and #storage[44].entries==64,'bounded library rejects a 65th entry atomically')
-- Real browser restores recipes only after its matching decode is Ready.
local state,hash,duration='Loading',ref.hash,2
local synth={version=40,sampleInfo=function() return {status=state,hash=hash,duration=duration} end}
local b=Samples.new(synth,{},{},{});assert(b:loadRecipe(recipe));check(b.pendingRecipe and not b.editView and not b:recipe('kit'),'asynchronous load cannot be saved prematurely')
state='Ready';b:frame();check(not b.pendingRecipe and b.editView.points[3]==1.5 and not b.editView.snap and b.root==48,'Ready restores original slice order, controls and playback view')
local round=assert(b:recipe('kit'));check(round.source.offset==.2 and round.source.reverse and round.source.gain==.7 and round.settings.sensitivity==.75,'browser recipe round trip')
local slices=assert(b:slices(b.editView.points));check(#slices==4 and slices[1].offset==1.95 and slices[4].offset==.2,'reversed kit creates slices in its displayed playback order')
b.editView.trimFirst=.1;check(not b:recipe('sample'),'unapplied trim preview cannot silently save older bounds');b.editView.trimFirst=0
hash=string.rep('b',32);check(not b:reference() and not b:recipe('sample'),'hash mismatch cannot preview, save or assign');hash=ref.hash
state='Unavailable';assert(b:loadRecipe(recipe));check(not b:reference() and b.pendingRecipe,'missing sound remains unavailable');state='Ready';duration=1.9;b:frame();check(b:info().status=='Unavailable' and not b:reference(),'decoded length mismatch cannot misplace kit boundaries')
duration=2;b:select(ref.path);assert(b:loadRecipe(recipe));assert(b:recipe('sample'))
-- Client request queue exercises stale replies, draft protection, revisions and timeouts.
local w={revision=0,generation=9,dirty=true};local now=0;local queue={};local l=Library.new(w,{now=function() return now end,send=function(op,data) data=copy(data);data.op=op;queue[#queue+1]=data end});l.browser=b
local function reply() local data=table.remove(queue,1);local ok,out=pcall(server.handle,55,data);if not ok then out={requestId=data.requestId,message=tostring(out)} end;l:receive(out);return out end
assert(l:list());reply();l.name='Client kit';check(l:save(b,'kit',false),'client saves current ready recipe');b.root=50;reply()
check(l.selected.id==1 and b.root==50 and w.generation==9 and w.dirty,'save acknowledgement leaves project and newer draft edits untouched')
check(l:open(1,b),'client starts loading selected entry');b.root=51;reply();check(b.root==51 and l.status:find('Newer sample edits',1,true),'late open does not replace a newer draft')
l:open(1,b);reply();check(b.root==48 and b.editView.points[2]==.6,'explicit open restores saved draft')
l:open(1,b);l.browser=nil;reply();check(l.status:find('Newer sample edits',1,true),'leaving the sample page cancels an in-flight load');l.browser=b
l.name='Renamed client';l:rename();reply();check(l.selected.revision==2 and l.selected.name=='Renamed client','client tracks server revisions after rename')
check(not l:delete() and #storage[55].entries==1,'first delete press only arms confirmation');check(l:delete(),'second press deletes');reply();check(not l.selected and #l.entries==0 and b.path==ref.path,'delete keeps working draft and projects')
l.name='Delayed';assert(l:save(b,'sample',false));local timed=table.remove(queue,1);now=11;l:frame(now);check(l.uncertain and not l:save(b,'sample',false),'uncertain writes cannot be blindly retried')
local out=server.handle(55,timed);l:receive(out);check(#l.entries==0,'late timed-out reply cannot replace current state')
l:list();reply();check(not l.uncertain and #l.entries==1,'refresh reconciles a write which completed after timeout')
l:open(l.entries[1].id,b);local current=table.remove(queue,1);l:receive{requestId=timed.requestId,entry=out.entry};check(l.pending.id==current.requestId,'older reply cannot consume newer request');l:receive(server.handle(55,current))
local a=assert(io.open(root..'shared/sample-library.lua','rb'));local text=a:read('*a');a:close();local c=assert(io.open('apps/openmw-server/scripts/procedural_synth_sample_format.lua','rb'));check(text==c:read('*a'),'client/server sample recipe format stays identical');c:close()
print('PASS sample/kit validation, private storage, atomic revisions/limits, matching asynchronous restore, slice order, stale edits and timeout reconciliation; '..n..' assertions')
