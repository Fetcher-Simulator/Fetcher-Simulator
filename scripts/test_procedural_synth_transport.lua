local root='files/server/server-lua-packages/fetcher.procedural-synth/';local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=dofile(root..'shared/schema.lua');local format=dofile(root..'shared/project.lua')(schema)
local Sequence=dofile(root..'shared/sequence.lua');package.loaded[prefix..'shared.sequence']=Sequence
local T=dofile(root..'client/transport.lua');local now=100;local requests,messages,starts,replacements={},{},{},{}
local function check(ok,msg) assert(ok,msg) end
local engine={createStudio=function()
    return {closed=false,playing=false,position=0,fault=0,strikes=0,configure=function() return true end,mixer=function() return true end,effects=function() return true end,
        loadScore=function(_,bytes,length,loop) return Sequence.validate(bytes,length,loop) end,
        start=function(self,delay,offset) starts[#starts+1]={delay,offset};self.playing=true;return true end,
        replace=function(_,delay) replacements[#replacements+1]=delay;return true end,
        stop=function(self) self.playing=false end,keepAlive=function() end,set=function() return true end,
        noteOn=function() return true end,noteOff=function() return true end,releaseTrack=function() end,
        trackPeak=function() return 0.1 end,trackVoices=function() return 1 end}
end}
local transport=T.new(engine,{sendToServer=function(_,data) requests[#requests+1]=data end},
    {getRealTime=function() return now end},function(message) messages[#messages+1]=message end)
local p=format.new();p.tracks[1]=format.newTrack('piano','Test',16);p.tracks[1].notes={{n=60,s=0,l=1,v=1}}
local plan=Sequence.compile(p,nil,nil,'clips');local i={id=7,epoch=1,seen=100,source={},channels={[1]={params=schema.state({}),level=1}},workstation={awaiting=true}}
function i.workstation:stop() self.stopped=true;transport:stop(i,function(op,args) requests[#requests+1]={op=op,playId=args.playId} end) end
local instances={[7]=i};local function send(op,args) args.op=op;requests[#requests+1]=args end
check(transport:queue(i,plan,false,send),'queue starts upload and reserves native tracks')
i.pendingBand={};transport:frame(instances);check(not i.upload.began,'score Begin waits for structural band update so server cannot cancel its upload');i.pendingBand=nil
i.pendingChannels={[1]={}};transport:frame(instances);check(i.upload.offset==1,'score waits for pending channel settings');i.pendingChannels={}
now=100.02;transport:receive({op='clock',sent=100,serverTime=50.01},instances)
check(math.abs(transport.offset+50)<0.000001,'clock estimate subtracts half RTT')
transport:frame(instances);check(requests[#requests].op=='scoreCommit','bounded chunks commit only after complete upload')
local function download(id,started,activate,replace,epoch)
    transport:receive({op='scoreBegin',id=7,epoch=epoch or 1,playId=id,count=#plan.bytes/11,length=plan.length,loop=true,started=started,activate=activate,replace=replace},instances)
    transport:receive({op='scoreChunk',id=7,epoch=epoch or 1,playId=id,index=1,bytes=plan.bytes},instances)
    transport:receive({op='scoreReady',id=7,epoch=epoch or 1,playId=id},instances)
end
download(1,51,51,false);transport:frame(instances)
check(#starts==1 and math.abs(starts[1][1]-0.98)<0.000001 and starts[1][2]==0 and not i.workstation.awaiting,'future start maps server time to native delay')
now=101.2;transport:queue(i,plan,true,send);transport:frame(instances);download(2,51,52,true);transport:frame(instances)
check(#replacements==1 and #starts==1,'live replacement does not restart the existing native transport')
transport:stop(i,send);download(2,51,52,true);transport:frame(instances)
check(not i.studio.playing and #starts==1,'Stop tombstone rejects late ready packets')
i.nextPlayId=3;i.stoppedPlayId=2;i.workstation=nil;now=106.25;i.seen=now
download(3,51,51,false);transport:frame(instances)
check(#starts==2 and starts[2][1]==0 and math.abs(starts[2][2]-1.25)<0.000001,'late listener seeks to current loop phase')
transport:receive({op='scoreBegin',id=8,epoch=3,playId=1,count=1,length=96000,loop=true},instances)
transport:receive({op='transportStop',id=8,epoch=2,playId=99},instances)
check(transport.downloads[8] and transport.downloads[8].epoch==3,'older session cannot cancel a pending new-session download')
check(#messages==0,'valid playback produces no error messages')
print('PASS clock estimation, chunk upload, future start, live replacement, late seek and cancellation')
