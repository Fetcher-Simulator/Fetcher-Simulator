local root='files/server/server-lua-packages/fetcher.procedural-synth/'
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=dofile(root..'shared/schema.lua');local format=dofile(root..'shared/project.lua')(schema)
package.loaded[prefix..'shared.schema']=schema;package.loaded[prefix..'shared.project']=dofile(root..'shared/project.lua')
package.loaded[prefix..'client.midi']=dofile(root..'client/midi.lua');package.loaded[prefix..'client.soundsets']=dofile(root..'client/soundsets.lua')
local Workstation=dofile(root..'client/workstation.lua');local Library=dofile(root..'client/library.lua')
local checks=0;local function check(b,m) checks=checks+1;assert(b,m) end
local function copy(t) if type(t)~='table' then return t end;local out={};for k,v in pairs(t) do out[k]=copy(v) end;return out end
local storage,fail={},false
local server=dofile('apps/openmw-server/scripts/procedural_synth_library.lua')({
    getCharacterStorageForCharacter=function(id,_,key) return copy(storage[id..':'..key]) end,
    setCharacterStorageForCharacter=function(id,_,key,v) if fail then return false end;storage[id..':'..key]=copy(v);return true end,
},format)
local serial=0
local function request(op,data,character)
    serial=serial+1;data=copy(data or {});data.op=op;data.requestId=serial;return server.handle(character or 11,data)
end
local legacy=format.new();legacy.name='Legacy song';legacy.tracks[1]=format.newTrack('piano','Sample',16)
legacy.tracks[1].notes={{n=60,s=0,l=1,v=.7}}
legacy.tracks[1].params.sample={path='sound/test.wav',hash=string.rep('a',32),offset=.2,duration=1,root=60,oneShot=true}
storage['11:project']=copy(legacy)
local list=request('libList');check(#list.entries==1 and list.entries[1].name=='Legacy song' and storage['11:project'].name=='Legacy song','legacy save migrates as a copy with original row preserved')
check(#request('libList',nil,22).entries==0,'character isolation')
local opened=request('libOpen',{projectId=1});check(opened.project.tracks[1].params.sample.offset==.2,'full sample document survives migration')
legacy.name='Second song';local saved=request('libSave',{projectId=0,project=legacy,saveRevision=1})
check(saved.projectId==2 and #saved.entries==2,'Save As creates a distinct named entry')
local renamed=request('libRename',{projectId=2,projectRevision=1,name='Renamed'})
check(renamed.projectRevision==2 and request('libOpen',{projectId=2}).project.name=='Renamed','explicit project rename persists')
check(not pcall(request,'libSave',{projectId=2,projectRevision=1,project=legacy,saveRevision=2}),'stale writer cannot overwrite a newer revision')
fail=true;check(not pcall(request,'libDelete',{projectId=2,projectRevision=2}) and #storage['11:library'].entries==2,'failed write preserves prior library')
fail=false;request('libDelete',{projectId=2,projectRevision=2});check(#request('libList').entries==1,'confirmed deletion removes only chosen entry')
for n=1,4 do legacy.name='Draft '..n;request('libSnapshot',{project=legacy,projectId=1,token='draft'..n,saveRevision=n}) end
local rec=request('libList').recoveries
check(#rec==3 and rec[1].name=='Draft 4' and rec[3].name=='Draft 2','recovery retains newest three document drafts')
legacy.name='Updated draft';request('libSnapshot',{project=legacy,projectId=1,token='draft4',saveRevision=9})
legacy.name='Old delayed draft';request('libSnapshot',{project=legacy,projectId=1,token='draft4',saveRevision=3})
rec=request('libList').recoveries
check(#rec==3 and rec[1].name=='Updated draft' and request('libRecover',{recoveryId=rec[1].id}).projectId==0,'same draft updates without older overwrite and restores as new unsaved copy')
check(request('libOpen',{projectId=1}).project.name=='Legacy song','autosave leaves named save unchanged')
local bad=copy(legacy);bad.tracks[1].params.sample.duration=31
check(not pcall(request,'libSnapshot',{project=bad,projectId=1,token='bad',saveRevision=1}),'invalid recovery cannot replace stored data')
storage['33:library']={version=99};check(not pcall(request,'libList',nil,33) and storage['33:library'].version==99,'future library remains intact')
storage['44:recovery']=false;check(not pcall(request,'libList',nil,44) and storage['44:recovery']==false,'unsupported false recovery row is not treated as missing')
for n=2,16 do request('libSave',{projectId=0,project=legacy,saveRevision=1}) end
check(not pcall(request,'libSave',{projectId=0,project=legacy,saveRevision=1}) and #request('libList').entries==16,'bounded library rejects seventeenth project')
-- Run real client model against queued server replies to exercise ordering.
storage={};local now=0;local queue={};local panics=0
local w=Workstation.new{panic=function() panics=panics+1 end,configure=function() end,message=function() end,importMidi=function() end}
local l=Library.new(w,{now=function() return now end,send=function(op,data) data=copy(data);data.op=op;queue[#queue+1]=data end,show=function() end,message=function() end})
local function reply()
    local data=table.remove(queue,1);local ok,out=pcall(server.handle,55,data)
    if not ok then out={requestId=data.requestId,message=tostring(out)} end;l:receive(out);return out
end
l:list();reply();w:newSong(120,4,4,8);w:addNote(1,60,0)
check(l:save(true,'Named copy'),'Save As accepts a copy name');local save=table.remove(queue,1)
local out=server.handle(55,save);w:addNote(1,64,4);l:receive(out)
check(w.dirty and l.active==1 and w.project.name=='New song','late save ack keeps newer edits and their current title')
l:save(false);reply();check(not w.dirty,'matching save acknowledgement clears dirty')
w:addNote(1,67,8);local gen=w.generation;local before=panics
now=31;l:frame(now);check(queue[1].op=='libSnapshot' and w.generation==gen and panics==before,'autosave does not edit, stop or reschedule music');reply()
check(w.dirty and l.autosaved==gen and storage['55:library'].entries[1].project.tracks[1].edited.bytes~=w.project.tracks[1].edited.bytes,'recovery is separate from named save')
local ran=false;l:guard(function() ran=true end,'leave');check(l.action and not ran and w.page=='projects','unsaved action waits for a choice')
l:cancel();check(not ran and w.dirty,'Cancel preserves edits')
w:addNote(1,69,10);l:guard(function() ran=true end,'leave');fail=true;l:snapshot(true);reply();check(not ran and l.action,'storage failure cannot continue destructive action')
fail=false;l:snapshot(true);reply();check(ran and not l.action,'Keep recovery continues only after server acknowledgement')
l:save(false);reply();l:open(1,false);w:addNote(1,70,12);reply();check(w.dirty and #w:noteData(1)==5,'late Open response cannot replace newer edits')
l:cancel();w.dirty=false;l:open(1,false);reply();check(not w.dirty and l.active==1,'clean open loads named project and revision')
l:open(request('libList',nil,55).recoveries[1].id,true);reply();check(w.dirty and l.active==0 and #w:noteData(1)==4,'recovery restores musical data as unsaved copy')
l:save(true);local delayed=table.remove(queue,1);now=now+11;l:frame(now);l:receive(server.handle(55,delayed))
check(w.dirty and not l.pending and l.active==0,'timeout ignores late acknowledgements without clearing edits')
w.recording={};check(not l:save(false) and not l:snapshot(true),'unfinished recording cannot be silently saved as a project');w.recording=nil
print('PASS project migration/library isolation, version conflicts, recovery bounds, failures, autosave, unsaved actions and delayed replies; '..checks..' assertions')
