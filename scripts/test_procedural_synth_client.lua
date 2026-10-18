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
package.loaded[prefix..'shared.sequence']=dofile(root..'shared/sequence.lua')
package.loaded[prefix..'client.transport']=dofile(root..'client/transport.lua')
package.loaded[prefix..'shared.project']=dofile(root..'shared/project.lua')
package.loaded['openmw.util']={vector2=vector,color={rgb=function(...) return {...} end}}
package.loaded['openmw.util'].vector3=function(x,y,z) return {x=x,y=y,z=z} end
package.loaded['openmw.animation']={}
package.loaded['openmw.types']={}
package.loaded['openmw.vfs']={pathsWithPrefix=function() return function() end end}
package.loaded[prefix..'client.midi']=dofile(root..'client/midi.lua')
package.loaded[prefix..'client.performance']=dofile(root..'client/performance.lua')
local viewport=vector(1024,768)
local mouseDown=false
local ui={TYPE={Text='Text',TextEdit='TextEdit',Widget='Widget',Image='Image',Window='Window'},
    texture=function() return {} end,content=function(t) return t end,screenSize=function() return viewport end,
    showMessage=function() end}
function ui.create(layout)
    local element={layout=layout}
    function element:update() check(self.layout~=nil,'updating destroyed UI') end
    function element:destroy() check(self.layout~=nil,'double UI destruction'); self.layout=nil end
    return element
end
package.loaded['openmw.ui']=ui
package.loaded['openmw.async']={callback=function(_,fn) return fn end}
local input={KEY={Escape=1,F8=100,Enter=101,NP_Enter=102},isMouseButtonPressed=function() return mouseDown end}
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
local preferences={}
package.loaded['openmw.storage']={playerSection=function() return {get=function(_,k) return preferences[k] end,set=function(_,k,v) preferences[k]=v end} end}
local monitor=1
package.loaded['openmw.synth'].getMonitorVolume=function() return monitor end
package.loaded['openmw.synth'].setMonitorVolume=function(value) monitor=value end
package.loaded[prefix..'client.window']=dofile(root..'client/window.lua')
package.loaded[prefix..'client.workstation']=dofile(root..'client/workstation.lua')
package.loaded[prefix..'client.controls']=dofile(root..'client/controls.lua')
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
-- Both instrument views expose every native parameter through editable controls.
local mouseVelocity,released
local e=Editor.new(schema.state({}),{noteOff=function() released=true end,noteOn=function(_,velocity) mouseVelocity=velocity end,change=function() end,
    command=function() end,close=function() end,meter=function() return 0,0 end})
local controls=0; for name in pairs(e.controls) do controls=controls+1;check(schema.specs[name]~=nil) end
check(controls==29,'all synth parameters including envelope curve editable')
local firstPianoKey=e.root.layout.content[#e.root.layout.content-12]
firstPianoKey.events.mousePress{button=1,offset=vector(10,56)}
check(mouseVelocity==1,'piano press position controls velocity')
e:frame(200)
check(released and not e.mouseNote,'mouse release outside the piano stops its note')
local Controls=package.loaded[prefix..'client.controls']
check(Controls.parse('150 ms',{min=0.002,max=5,unit='s'})==0.15,'numeric fields accept milliseconds')
check(Controls.parse('150',{min=0.002,max=5,unit='s'})==0.15,'bare time values use displayed milliseconds')
check(Controls.parse('75',{min=0,max=1,unit='%'})==0.75,'bare percentage values use displayed percent')
check(Controls.format(0.1201,{unit='s'})=='120.1 ms','fine envelope changes remain visible')
check(Controls.parse('2 kHz',{min=20,max=18000,unit='Hz'})==2000,'numeric fields accept kHz')
check(Controls.parse('75%',{min=0,max=1,unit='%'})==0.75,'numeric fields accept percent')
for _,bad in ipairs{'NaN','1e999','-1','hello','150 Hz'} do
    check(Controls.parse(bad,{min=0.002,max=5,unit='s'})==nil,'invalid numeric text rejected')
end
local function control(owner,title)
    for _,element in ipairs(owner.children) do
        local child=element.layout.content and element.layout.content[1]
        if child and child.props and child.props.text==title then return element end
    end
end
local attack=assert(control(e,'AMP A'))
local field=attack.layout.content[3]
field.layout.events.focusGain();field.layout.events.textChanged('150 ms')
check(e.params.attack==0.01,'draft text does not emit intermediate audio edits')
field.layout.events.keyPress{code=input.KEY.Enter}
check(e.params.attack==0.15 and not e.typing,'Enter commits a precise envelope time')
field.layout.events.focusGain();field.layout.events.textChanged('200 s');field.layout.events.focusLoss()
check(e.params.attack==0.15,'invalid commit preserves the previous value')
attack.layout.content[5].events.mouseClick()
check(math.abs(e.params.attack-0.151)<0.000001,'plus button adjusts envelope by one millisecond')
input.isShiftPressed=function() return true end
attack.layout.content[5].events.mouseClick()
check(math.abs(e.params.attack-0.1511)<0.000001,'Shift enables finer increments')
input.isShiftPressed=function() return false end
local seen={};for name in pairs(e.controls) do seen[name]=true end
e.params.drumType=2;e:frame(200.1)
check(e.drums and e.controls.noiseDecay and e.controls.bodyFrequency,'drum mode exposes separate body and noise controls')
for name in pairs(e.controls) do seen[name]=true end
e.params.drumType=0;e.params.instrumentType=1;e:frame(200.11)
check(e.controls.stringMute and e.controls.stringDecay,'guitar exposes physical string controls')
for name in pairs(e.controls) do seen[name]=true end
e.page='effects';e:render()
check(e.controls.driveType and e.controls.cabinet,'effects controls exposed per instrument')
for name in pairs(e.controls) do seen[name]=true end
for name in pairs(schema.specs) do check(seen[name],'parameter has an editable field: '..name) end
e.page='sound';e.params.instrumentType=0;e.params.drumType=0;e:render();e:frame(200.2)
-- Native Window events and scaled hit testing exercise the actual editor tree.
local originalVolume=e.params.volume
local networkEvents=#sent
local win=e.window
win:drag{position=vector(70,80),size=vector(846,659.7)}
mouseDown=true;e:frame(201)
check(e.root.layout.props.position.x~=70,'native drag is not rebuilt while mouse capture is active')
mouseDown=false;e:frame(202)
check(e.root.layout.props.position.x==70 and math.abs(win.scale-0.9)<0.0001,'drag/resize commits bounded geometry on release')
local cutoff
for _,element in ipairs(e.children) do
    if element.layout.content and element.layout.content[1] and element.layout.content[1].props.text:find('Low%-pass cutoff') then cutoff=element;break end
end
check(cutoff~=nil,'cutoff control remains attached after resize')
local cutoffWidth=cutoff.layout.props.size.x
cutoff.layout.content[2].events.mousePress{button=1,offset=vector(cutoffWidth,5)}
check(e.params.cutoff==schema.specs.cutoff.max,'resized slider maps physical mouse position to the correct value')
for _=1,5 do e:plots() end
check(cutoff.layout.props.size.x==cutoffWidth,'repeated live updates never multiply GUI scale again')
local bar=e.root.layout.template.content[2]
bar.content[7].events.mousePress{button=1,offset=vector(55*win.scale,5)}
e:frame(203)
check(monitor==0.5 and e.params.volume==originalVolume and #sent==networkEvents,'monitor changes listener volume without synth/project or network edits')
local x,y,ww,hh=win.x,win.y,win.width,win.height
win:fullscreen();e:frame(204)
check(e.root.layout.props.position.x==0 and e.root.layout.props.size.x==viewport.x and e.root.layout.props.size.y==viewport.y,'fullscreen fills the current viewport')
win:fullscreen();e:frame(205)
check(win.x==x and win.y==y and win.width==ww and win.height==hh,'restore returns to previous window geometry')
win:zoom(0.1);e:frame(206)
check(win.scale>0.9 and preferences.scale==win.preferred,'scale setting updates the visible editor and persistent preference')
viewport=vector(800,600);e:frame(207)
check(win.x+win.actualWidth<=800 and win.y+win.actualHeight<=600,'resolution changes keep window and controls on screen')
local restored=package.loaded[prefix..'client.window'].new()
check(restored.preferred==win.preferred,'window preferences survive hiding, reopening and page switches')
e:destroy()
local browser,previewOns,previewOffs=nil,0,0
browser=Editor.new(schema.state({}),{noteOff=function() previewOffs=previewOffs+1 end,
    noteOn=function() previewOns=previewOns+1 end,change=function() end,close=function() end,meter=function() return 0,0 end,
    command=function(op,args)
        if op=='load' then for _,p in ipairs(schema.builtins) do if p.id==args.presetId then browser:state(schema.state(p.state),p.id) end end end
    end})
browser.bank='Guitar';browser.page='presets';browser:catalog(schema.builtins)
local function browserButton(title)
    for _,item in ipairs(browser.root.layout.content) do
        if item.content and item.content[1] and item.content[1].props.text==title then return item end
    end
    error('Missing browser button: '..title)
end
browserButton('Clean Electric').events.mouseClick()
check(browser.selected=='b:guitar-clean' and browser.params.instrumentType==0,'browser selection does not replace the current sound')
browserButton('Load + Audition').events.mouseClick()
check(browser.params.instrumentType==1 and previewOns==1,'audition waits for the loaded sound before starting its note')
browser:frame(300);browser:frame(302)
check(previewOffs==1 and not browser.previewRemaining,'audition releases its own note')
browserButton('Page >').events.mouseClick()
check(browser.browserPage==1 and browserButton('Garage Fuzz')~=nil,'filtered browser paginates guitar bank')
browserButton('Bank >').events.mouseClick()
check(browser.bank=='Rock / Metal' and browser.browserPage==0 and browserButton('Crunch Rhythm')~=nil,'changing genre resets pagination')
browser:destroy()
viewport=vector(1024,768);preferences={};monitor=1
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
check(w.project.tracks[2].presetId=='b:snare' and w.project.tracks[2].params.drumType==2,'drum button uses native noise sound')
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
button('Mix').events.mouseClick();now=400.19;player.engineHandlers.onFrame(0.01)
check(w.page=='channel','per-track processor page opens')
button('Bypassed').events.mouseClick();now=400.2;player.engineHandlers.onFrame(0.01)
check(w.project.tracks[1].mixer.eqEnabled==1,'EQ bypass toggle changes project')
button('Back to mixer').events.mouseClick();now=400.21;player.engineHandlers.onFrame(0.01)
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
handler{version=1,op='state',id=7,epoch=8,seq=1,params=schema.state({}),channels={
    [1]={params=schema.state({drumType=4}),level=1},[2]={params=schema.state({drumType=3}),level=1}}}
handler{version=1,op='on',id=7,epoch=8,seq=2,channel=1,note=46,velocity=1}
local openAudio=audios[#audios]
check(openAudio.notes[46],'listener starts open hat')
handler{version=1,op='on',id=7,epoch=8,seq=3,channel=2,note=42,velocity=1}
check(openAudio.voices==0 and audios[#audios].notes[42],'closed hat chokes open hat on the listener')
handler{version=1,op='close',id=7,epoch=8,seq=4}
package.loaded['openmw.synth'].version=5
local oldClient=dofile(root..'client/player.lua')
check(oldClient.interface==nil and oldClient.eventHandlers.Synth_Activate~=nil,'older engines receive an upgrade message before loading the new window controls')
print(string.format('PASS client/editor lifecycle, chords, edits, presets, sequence, listener cleanup; %d assertions',checks))
