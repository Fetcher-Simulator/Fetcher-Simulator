-- Real transport, independent native-output doubles, and exact score partition checks.
local root='files/server/server-lua-packages/fetcher.procedural-synth/';local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=dofile(root..'shared/schema.lua');local format=dofile(root..'shared/project.lua')(schema)
local Sequence=dofile(root..'shared/sequence.lua');package.loaded[prefix..'shared.sequence']=Sequence
local T=dofile(root..'client/transport.lua');local now=100;local outputs={};local checks=0
local function check(ok,msg) checks=checks+1;assert(ok,msg) end
local function copy(t) if type(t)~='table' then return t end;local out={};for k,v in pairs(t) do out[k]=copy(v) end;return out end
local source={id=7,isValid=function() return true end};local physical={id=8,isValid=function() return true end}
local native={createStudio=function(object)
    local a={source=object,closed=false,playing=false,tracks={},starts={},replaces=0,strikes=0,mixPeak=0.1,masterRms=0.05}
    function a:configure(n,p,l) self.tracks[n]={params=p,level=l};return true end
    function a:mixer() return true end
    function a:effects(_,master) self.master=master;return true end
    function a:loadScore(bytes,length,loop) self.bytes=bytes;self.length=length;self.loop=loop;return #bytes%11==0 end
    function a:start(delay,offset) self.starts[#self.starts+1]={delay=delay,offset=offset};self.playing=true;return true end
    function a:replace() self.replaces=self.replaces+1;return true end
    function a:close() self.closed=true;self.playing=false end
    function a:stop() self.playing=false end
    function a:keepAlive() end
    function a:set() return true end
    function a:trackPeak() return self.closed and 0 or 0.1 end
    function a:trackRms() return 0.05 end
    function a:trackReduction() return 0 end
    function a:trackVoices() return self.closed and 0 or 1 end
    function a:releaseTrack() end
    outputs[#outputs+1]=a;return a
end}
local errors={};local transport=T.new(native,{sendToServer=function() end},{getRealTime=function() return now end},
    function(m) errors[#errors+1]=m end,function(id) return id==8 and physical end)
transport.offset=0;transport.samples=3;transport.lastClock=100
local p=format.new();for n=1,2 do p.tracks[n]=format.newTrack('piano','Part '..n,16);p.tracks[n].notes={{n=60+n,s=0,l=2,v=1}} end
local plan=Sequence.compile(p,nil,nil,'clips')
local band=format.band();band[1].target={mpNum=8,cell='INT:A',refId='fetcher_procedural_keyboard_v1'}
local i={id=7,epoch=1,source=source,seen=now,channels={[1]={params=schema.state({}),level=1},[2]={params=schema.state({}),level=0.8}}}
transport:band(i,band,{0,1},{'Ready'})
local d={bytes=plan.bytes,chunks={plan.bytes},length=plan.length,loop=true,started=101,activate=101,playId=1}
transport:finish(i,d)
local home,remote=i.studio,i.outputs[1].audio
check(home.source==source and remote.source==physical,'each native stream uses the assigned physical object')
check(#home.bytes==11 and home.bytes:byte(9)==1 and #remote.bytes==11 and remote.bytes:byte(9)==2,'each note appears in exactly one physical score')
check(home.starts[1].delay==1 and remote.starts[1].delay==1,'all outputs share one future server start')
check(home.tracks[2]==nil and remote.tracks[1]==nil,'routed lanes do not allocate duplicate synthesizers')
local updated=copy(band);updated[1].level=0.3;updated[1].mute=true
transport:band(i,updated,{0,1},{'Ready'})
check(remote.master==0 and home.master==0.7 and #remote.starts==1,'member mute/gain affects only that output without restarting score')
transport:band(i,band,{0,1},{'Missing'})
check(remote.closed and home.playing and transport:metrics(i,2)==0,'missing member stops its source while home continues')
now=102.25;transport:band(i,band,{0,1},{'Ready'});remote=i.outputs[1].audio
check(remote~=outputs[2] and math.abs(remote.starts[1].offset-1.25)<1e-9 and #home.starts==1,'same physical member rejoins current phase without restarting other outputs')
local replacement=copy(d);replacement.playId=2;replacement.replace=true;replacement.activate=103
transport:finish(i,replacement)
check(home.replaces==1 and remote.replaces==1,'bar-boundary edits replace every output on the same timeline')
transport:stop(i)
check(not home.playing and not remote.playing and not i.lastScore,'Stop cancels all outputs and prevents stale rejoins')
transport:band(i,band,{1,1},{'Ready'});transport:finish(i,d)
check(i.studio.bytes=='' and #i.outputs[1].audio.bytes==22,'all-remote song uses an empty home clock and one copy of each note')
for n=1,24 do
    transport:band(i,band,{n%2,1},{'Ready'});transport:finish(i,d)
    local alive=0;for _,a in ipairs(outputs) do if not a.closed then alive=alive+1 end end
    check(alive==2,'repeated routing does not leak native outputs')
end
transport:close(i);local alive=0;for _,a in ipairs(outputs) do if not a.closed then alive=alive+1 end end
check(alive==0 and #errors==0,'Finish closes every routed stream')
print('PASS physical band routing, exact partition, shared starts/replacement, missing/rejoin, gain/mute and bounded cleanup; '..checks..' assertions')
