-- Run the real player/editor scripts in a deterministic UI/transport harness.
local root='files/server/server-lua-packages/fetcher.procedural-synth/'
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=dofile(root..'shared/schema.lua')
local now,connected,sent,audios,mode=100,true,{},{},nil
local checks=0
local function check(value,message) checks=checks+1; assert(value,message) end
local function vector(x,y) return {x=x,y=y} end
local source={recordId='fetcher_procedural_synth_v1',valid=true}
function source:isValid() return self.valid end
local self={cell={id='ToddTest'}}
package.loaded[prefix..'shared.schema']=schema
package.loaded['openmw.util']={vector2=vector,color={rgb=function(...) return {...} end}}
local ui={TYPE={Text='Text',TextEdit='TextEdit',Widget='Widget',Image='Image'},
    texture=function() return {} end,content=function(t) return t end,screenSize=function() return vector(1024,768) end,
    showMessage=function() end}
function ui.create(layout)
    local element={layout=layout}
    function element:update() check(self.layout~=nil,'updating destroyed UI') end
    function element:destroy() check(self.layout~=nil,'double UI destruction'); self.layout=nil end
    return element
end
package.loaded['openmw.ui']=ui
package.loaded['openmw.async']={callback=function(_,fn) return fn end}
local input={KEY={Escape=1},isMouseButtonPressed=function() return false end}
for i,name in ipairs({'Z','S','X','D','C','V','G','B','H','N','J','M','Comma'}) do input.KEY[name]=i+1 end
package.loaded['openmw.input']=input
package.loaded['openmw.interfaces']={UI={getMode=function() return mode end,
    addMode=function(m) mode=m end,removeMode=function() mode=nil end}}
package.loaded['openmw.core']={getRealTime=function() return now end}
package.loaded['openmw.mp']={isConnected=function() return connected end,
    getObjectMpNum=function() return 7 end,sendToServer=function(event,data) sent[#sent+1]=data end}
package.loaded['openmw.self']=self
package.loaded['openmw.nearby']={activators={source}}
package.loaded['openmw.synth']={create=function(obj)
    check(obj==source)
    local audio={closed=false,peak=0,voices=0,notes={},params={},alive=0}
    function audio:set(name,value) self.params[name]=value end
    function audio:noteOn(note,velocity) self.notes[note]=true; self.voices=self.voices+1 end
    function audio:noteOff(note) self.notes[note]=nil; self.voices=math.max(0,self.voices-1) end
    function audio:allNotesOff() self.notes={}; self.voices=0 end
    function audio:close() self.closed=true end
    function audio:keepAlive() self.alive=self.alive+1 end
    audios[#audios+1]=audio; return audio
end}
local Editor=dofile(root..'client/editor.lua')
package.loaded[prefix..'client.editor']=Editor
local player=dofile(root..'client/player.lua')
local api=player.interface
local handler=player.eventHandlers.Synth_Message
handler{version=1,op='granted',id=7,epoch=1,seq=1,params=schema.state({})}
check(mode=='Interface' and api.status()[1].editing,'editor opens')
check(sent[#sent].op=='list','catalog requested')
api.noteOn(60); api.noteOn(64);api.noteOn(67)
check(audios[1].voices==3 and sent[#sent].op=='on','optimistic chord')
check(api.setParameter('cutoff',1000) and not api.setParameter('cutoff',0/0),'native parameter validation')
now=101;player.engineHandlers.onFrame()
check(sent[#sent].op=='edit' and sent[#sent].params.cutoff==1000,'coalesced edit')
handler{version=1,op='preset',id=7,epoch=1,seq=10,params=schema.state({wave1=3}),presetId='b:pad'}
check(audios[1].voices==0 and audios[1].params.wave1==3,'preset stops notes and applies state')
handler{version=1,op='on',id=7,epoch=1,seq=9,note=60,velocity=1}
check(audios[1].voices==0,'stale sequence ignored')
api.noteOn(72); api.close();check(mode==nil and audios[1].voices==0,'close releases held keys')
check(sent[#sent].op=='close','close releases server lock')
handler{version=1,op='close',id=7,epoch=1,seq=11}
check(audios[1].closed and #api.status()==0,'server cleanup closes native stream')
handler{version=1,op='state',id=7,epoch=2,seq=1,params=schema.state({})}
check(#api.status()==1 and not api.status()[1].editing,'listener has spatial audio without editor')
handler{version=1,op='on',id=7,epoch=2,seq=2,note=60,velocity=1}
check(audios[2].voices==1,'listener plays relayed note')
handler{version=1,op='off',id=7,epoch=2,seq=3,note=60}
check(audios[2].voices==0,'listener releases note')
self.cell={id='Other'};player.engineHandlers.onFrame()
check(audios[2].closed and #api.status()==0,'cell transition cleans listener')
handler{version=1,op='state',id=7,epoch=3,seq=1,params=schema.state({})}
now=110;player.engineHandlers.onFrame()
check(audios[3].closed,'missing server liveness expires audio')
connected=false;player.engineHandlers.onFrame()
-- All 26 parameters have actual editable controls.
local mouseVelocity,released
local e=Editor.new(schema.state({}),{noteOff=function() released=true end,noteOn=function(_,velocity) mouseVelocity=velocity end,change=function() end,
    command=function() end,close=function() end,meter=function() return 0,0 end})
local controls=0; for name in pairs(e.controls) do controls=controls+1;check(schema.specs[name]~=nil) end
check(controls==26,'all parameters editable')
local firstPianoKey=e.root.layout.content[#e.root.layout.content-12]
firstPianoKey.events.mousePress{button=1,offset=vector(10,56)}
check(mouseVelocity==1,'piano press position controls velocity')
e:frame(200)
check(released and not e.mouseNote,'mouse release outside the piano stops its note')
e:destroy()
print(string.format('PASS client/editor lifecycle, chords, edits, presets, sequence, listener cleanup; %d assertions',checks))
