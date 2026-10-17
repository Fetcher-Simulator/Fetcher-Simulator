-- Run the real player/editor scripts in a deterministic UI/transport harness.
local root='files/server/server-lua-packages/fetcher.procedural-synth/'
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=dofile(root..'shared/schema.lua')
local now,connected,sent,audios,mode=100,true,{},{},nil
local checks=0
local function check(value,message) checks=checks+1; assert(value,message) end
local function sounding(note) for _,audio in ipairs(audios) do if not audio.closed and audio.notes[note] then return true end end;return false end
local function vector(x,y) return {x=x,y=y} end
local source={recordId='fetcher_procedural_synth_v1',valid=true}
function source:isValid() return self.valid end
local self={cell={id='ToddTest'}}
package.loaded[prefix..'shared.schema']=schema
package.loaded[prefix..'shared.project']=dofile(root..'shared/project.lua')
package.loaded['openmw.util']={vector2=vector,color={rgb=function(...) return {...} end}}
package.loaded['openmw.util'].vector3=function(x,y,z) return {x=x,y=y,z=z} end
package.loaded['openmw.animation']={}
package.loaded['openmw.types']={}
package.loaded['openmw.vfs']={pathsWithPrefix=function() return function() end end}
package.loaded[prefix..'client.midi']=dofile(root..'client/midi.lua')
package.loaded[prefix..'client.performance']=dofile(root..'client/performance.lua')
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
local input={KEY={Escape=1,F8=100},isMouseButtonPressed=function() return false end}
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
package.loaded[prefix..'client.workstation']=dofile(root..'client/workstation.lua')
local Studio=dofile(root..'client/studio.lua')
local lastStudio
local newStudio=Studio.new
Studio.new=function(...) lastStudio=newStudio(...);return lastStudio end
package.loaded[prefix..'client.studio']=Studio
local Editor=dofile(root..'client/editor.lua')
package.loaded[prefix..'client.editor']=Editor
local lastEditor
local newEditor=Editor.new
Editor.new=function(...) lastEditor=newEditor(...);return lastEditor end
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
-- All 28 parameters have actual editable controls.
local mouseVelocity,released
local e=Editor.new(schema.state({}),{noteOff=function() released=true end,noteOn=function(_,velocity) mouseVelocity=velocity end,change=function() end,
    command=function() end,close=function() end,meter=function() return 0,0 end})
local controls=0; for name in pairs(e.controls) do controls=controls+1;check(schema.specs[name]~=nil) end
check(controls==28,'all parameters editable')
local firstPianoKey=e.root.layout.content[#e.root.layout.content-12]
firstPianoKey.events.mousePress{button=1,offset=vector(10,56)}
check(mouseVelocity==1,'piano press position controls velocity')
e:frame(200)
check(released and not e.mouseNote,'mouse release outside the piano stops its note')
e:destroy()
-- Existing characters may resume without onInit. The first frame discovers MIDI.
local function be(n,size) local b={};for i=size,1,-1 do b[i]=string.char(n%256);n=math.floor(n/256) end;return table.concat(b) end
local track=string.char(0,144,60,100,96,128,60,0,0,255,47,0)
local fileBytes='MThd'..be(6,4)..be(0,2)..be(1,2)..be(96,2)..'MTrk'..be(#track,4)..track
local virtualPath='midi/bardcraft/preset/test.mid'
package.loaded['openmw.vfs'].pathsWithPrefix=function()
    local done=false
    return function() if not done then done=true;return virtualPath end end
end
package.loaded['openmw.vfs'].open=function()
    return {read=function() return fileBytes end,close=function() end}
end
player=dofile(root..'client/player.lua');api=player.interface;handler=player.eventHandlers.Synth_Message
connected=true;now=200
player.engineHandlers.onFrame(0)
check(api.midiSongs()[1]==virtualPath,'resumed character discovers installed MIDI')
handler{version=1,op='granted',id=7,epoch=4,seq=1,params=schema.state({})}
check(not api.playMidi('arbitrary/path.mid') and api.playMidi(virtualPath),'MIDI catalog paths only')
now=200.01;player.engineHandlers.onFrame(0.01)
check(api.status()[1].midi and audios[#audios].notes[60],'MIDI uses native note-on and relay')
check(sent[#sent].op=='on' and sent[#sent].note==60,'MIDI shares live-note transport')
handler{version=1,op='preset',id=7,epoch=4,seq=2,params=schema.state({}),presetId='b:pad'}
check(not api.status()[1].midi and audios[#audios].voices==0,'preset load stops MIDI')
check(api.playMidi(virtualPath));player.engineHandlers.onFrame(0.01);api.noteOn(64)
check(not api.status()[1].midi and sounding(64),'manual key takes over playback')
check(api.playMidi(virtualPath));player.engineHandlers.onFrame(0.01);api.close()
check(not api.status()[1].midi and audios[#audios].voices==0,'editor close stops MIDI')
-- Hiding keeps the owned session alive, independently of UI and MIDI parsing.
handler{version=1,op='close',id=7,epoch=4,seq=3}
handler{version=1,op='granted',id=7,epoch=5,seq=1,params=schema.state({})}
handler{version=1,op='catalog',entries={{id='p:42',name='My pad'}}}
lastEditor.nameElement.layout.events.textChanged('Unfinished name')
lastEditor:saved('p:42')
api.noteOn(64)
api.setParameter('cutoff',2345)
local before=#sent
player.engineHandlers.onKeyPress{code=input.KEY.Escape}
check(mode==nil and api.status()[1].editing and not api.status()[1].editorVisible,'Escape hides without releasing ownership')
check(not audios[#audios].notes[64] and sent[#sent].op=='off','hide releases manually held notes')
for n=before+1,#sent do check(sent[n].op~='close','hide does not release lock') end
now=201;player.engineHandlers.onFrame(0)
check(sent[#sent].op=='edit' and sent[#sent].params.cutoff==2345,'hidden pending edit flushes')
before=#sent
player.engineHandlers.onKeyPress{code=input.KEY.Z}
check(#sent==before and audios[#audios].voices==0,'hidden movement keys do not play notes')
player.engineHandlers.onKeyPress{code=input.KEY.F8}
check(api.status()[1].editorVisible and mode=='Interface','F8 reopens owned editor')
check(lastEditor.name=='Unfinished name' and lastEditor.selected=='p:42' and #lastEditor.entries==1,'draft name and preset selection survive hiding')
check(#sent==before,'reopening does not reacquire or restart session')
player.engineHandlers.onKeyPress{code=input.KEY.F8}
handler{version=1,op='saved',presetId='p:43'}
handler{version=1,op='catalog',entries={{id='p:43',name='Saved while hidden'}}}
handler{version=1,op='preset',id=7,epoch=5,seq=2,params=schema.state({cutoff=3456}),presetId='p:43'}
player.eventHandlers.Synth_Activate{id=7}
check(lastEditor.name=='Saved while hidden' and lastEditor.selected=='p:43' and lastEditor.params.cutoff==3456,'hidden server updates appear on activation')
-- A different UI mode hides the editor without removing that mode.
mode='Inventory';player.engineHandlers.onFrame(0)
check(mode=='Inventory' and not api.status()[1].editorVisible and api.status()[1].editing,'mode change preserves session and other UI')
mode=nil
local longTrack=string.char(0,144,60,100,146,0,128,60,0,0,255,47,0) -- 12-second note
local longBytes='MThd'..be(6,4)..be(0,2)..be(1,2)..be(96,2)..'MTrk'..be(#longTrack,4)..longTrack
package.loaded['openmw.vfs'].open=function() return {read=function() return longBytes end,close=function() end} end
check(api.playMidi(virtualPath),'MIDI may load while editor is hidden')
player.engineHandlers.onFrame(0.01)
check(api.status()[1].midi and audios[#audios].notes[60] and mode==nil,'hidden parsing starts native MIDI without opening UI')
local heartbeats=0
for step=1,100 do
    now=201+step*0.1
    handler{version=1,op='alive',id=7,epoch=5,seq=2+step}
    before=#sent;player.engineHandlers.onFrame(0.1)
    for n=before+1,#sent do if sent[n].op=='heartbeat' then heartbeats=heartbeats+1 end end
    check(api.status()[1].midi and not api.status()[1].editorVisible,'hidden playback remains active past lease duration')
end
check(heartbeats==5,'owner heartbeat continues while hidden')
before=#sent;player.eventHandlers.Synth_Activate{id=7}
check(api.status()[1].editorVisible and api.status()[1].midi and #sent==before,'activation reopens without interrupting MIDI')
api.hide()
for step=1,21 do
    now=211+step*0.1
    handler{version=1,op='alive',id=7,epoch=5,seq=102+step}
    player.engineHandlers.onFrame(0.1)
end
check(not api.status()[1].midi and audios[#audios].voices==0,'hidden MIDI reaches end and releases notes')
check(api.show(),'explicit show reopens session')
check(api.playMidi(virtualPath));player.engineHandlers.onFrame(0.01)
local finish
for _,element in ipairs(lastEditor.root.layout.content) do
    if element.content and element.content[1] and element.content[1].props.text=='Finish' then finish=element end
end
check(finish~=nil,'Finish control is available')
finish.events.mouseClick()
check(not api.status()[1].editing and not api.status()[1].midi and sent[#sent].op=='close','Finish ends playback and releases lock')
handler{version=1,op='close',id=7,epoch=5,seq=124}
handler{version=1,op='granted',id=7,epoch=6,seq=1,params=schema.state({})}
api.hide();api.playMidi(virtualPath);player.engineHandlers.onFrame(0.01)
connected=false;player.engineHandlers.onFrame(0)
check(#api.status()==0 and audios[#audios].closed and not api.show(),'hidden disconnect releases audio and ownership')
-- Exercise the actual Studio window and navigation through real callbacks.
connected=true;now=400
handler{version=1,op='granted',id=7,epoch=7,seq=1,params=schema.state({})}
handler{version=1,op='catalog',entries=schema.builtins}
api.studio();local w=api.workstation()
check(lastStudio and lastStudio.kind=='studio' and mode=='Interface','studio opens from synth without releasing session')
local function button(text)
    for _,element in ipairs(lastStudio.root.layout.content) do
        if element.content and element.content[1] and element.content[1].props.text==text then return element end
    end
    error('Missing button '..text)
end
button('+ Piano clip').events.mouseClick();now=400.01;player.engineHandlers.onFrame(0.01)
check(w.page=='piano' and #w.project.tracks==1,'piano clip button switches to editable piano roll')
local inserted=false
for _,element in ipairs(lastStudio.root.layout.content) do
    if element.props and element.props.position and element.props.position.x==76 and element.props.position.y==219 and element.events then
        element.events.mouseClick();inserted=true;break
    end
end
check(inserted and #w.project.tracks[1].notes==1,'piano grid click creates an actual note')
now=400.02;player.engineHandlers.onFrame(0.01)
button('Drums').events.mouseClick();now=400.03;player.engineHandlers.onFrame(0.01)
button('+ Snare').events.mouseClick();now=400.04;player.engineHandlers.onFrame(0.01)
check(w.project.tracks[2].presetId=='b:snare' and w.project.tracks[2].params.wave1==4,'drum button uses native noise sound')
local toggled=false
for _,element in ipairs(lastStudio.root.layout.content) do
    if element.props and element.props.position and element.props.position.x==304 and element.props.position.y==225 and element.events then
        element.events.mouseClick();toggled=true;break
    end
end
check(toggled and w.project.tracks[2].steps:sub(1,1)~='0','drum grid changes pattern data')
now=400.05;player.engineHandlers.onFrame(0.01)
button('Play clips').events.mouseClick();now=400.06;player.engineHandlers.onFrame(0.01)
check(api.status()[1].transport and sent[#sent].op=='on' and sent[#sent].channel==2,'studio transport plays both native tracks through channel relay')
player.engineHandlers.onKeyPress{code=input.KEY.F8};now=400.16;player.engineHandlers.onFrame(0.001)
check(api.status()[1].transportTime>=0.109,'transport uses real elapsed time instead of filtered engine frame duration')
check(mode==nil and api.status()[1].transport,'hidden studio retains transport')
player.engineHandlers.onKeyPress{code=input.KEY.F8}
check(mode=='Interface' and w.page=='drums' and api.status()[1].transport,'F8 restores the same studio page')
now=400.17;player.engineHandlers.onFrame(0.01)
button('Mixer').events.mouseClick();now=400.18;player.engineHandlers.onFrame(0.01)
button('Sound').events.mouseClick()
check(lastEditor.params==w.project.tracks[1].params,'mixer Sound opens the selected track synth')
api.setParameter('cutoff',2222);now=400.3;player.engineHandlers.onFrame(0.12)
check(w.project.tracks[1].params.cutoff==2222 and sent[#sent].op=='channel' and sent[#sent].channel==1,'sound edits update one track instead of main/manual sound')
api.studio();now=400.31;player.engineHandlers.onFrame(0.01)
button('Save').events.mouseClick()
check(sent[#sent].op=='dawSave' and sent[#sent].project.tracks[2].presetId=='b:snare','Save sends complete project to character storage')
local saveRevision=sent[#sent].saveRevision
w:setTrack(1,'level',0.1)
handler{version=1,op='dawSaved',id=7,epoch=7,saveRevision=saveRevision}
check(w.dirty,'late save acknowledgement cannot clear newer edits')
api.saveProject();handler{version=1,op='dawSaved',id=7,epoch=7,saveRevision=sent[#sent].saveRevision}
check(not w.dirty,'matching saved generation clears unsaved indicator')
api.close();check(sent[#sent].op=='close' and not api.status()[1].transport and mode==nil,'Finish closes Studio and all sequenced voices')
print(string.format('PASS client/editor lifecycle, chords, edits, presets, sequence, listener cleanup; %d assertions',checks))
