-- Run the real player/editor scripts in a deterministic UI/transport harness.
local root='files/server/server-lua-packages/fetcher.procedural-synth/'
local prefix='scripts.multiplayer.fetcher.procedural-synth.'
local schema=dofile(root..'shared/schema.lua')
local now,connected,sent,audios,mode=100,true,{},{},nil
local autoLibraryReply
local checks=0
local function check(value,message) checks=checks+1; assert(value,message) end
local function sounding(note) for _,audio in ipairs(audios) do if not audio.closed and audio.notes[note] then return true end end;return false end
local function vector(x,y) return {x=x,y=y} end
local source={recordId='fetcher_procedural_synth_v1',valid=true}
function source:isValid() return self.valid end
local self={cell={id='ToddTest'}}
package.loaded[prefix..'shared.schema']=schema
package.loaded[prefix..'client.speech-catalog']=dofile(root..'client/speech-catalog.lua')
package.loaded[prefix..'client.voice-text']=dofile(root..'client/voice-text.lua')
package.loaded[prefix..'shared.voice-transcripts']=dofile(root..'shared/voice-transcripts.lua')
package.loaded[prefix..'client.samples']=dofile(root..'client/samples.lua')
package.loaded[prefix..'client.soundsets']=dofile(root..'client/soundsets.lua')
package.loaded[prefix..'shared.sequence']=dofile(root..'shared/sequence.lua')
package.loaded[prefix..'client.transport']=dofile(root..'client/transport.lua')
package.loaded[prefix..'shared.project']=dofile(root..'shared/project.lua')
package.loaded['openmw.util']={vector2=vector,color={rgb=function(...) return {...} end,rgba=function(...) return {...} end}}
do
    local V={}
    function V.__add(a,b) return setmetatable({x=a.x+b.x,y=a.y+b.y,z=a.z+b.z},V) end
    function V.__sub(a,b) return setmetatable({x=a.x-b.x,y=a.y-b.y,z=a.z-b.z},V) end
    package.loaded['openmw.util'].vector3=function(x,y,z) return setmetatable({x=x,y=y,z=z},V) end
end
local dummyT=setmetatable({getAnglesXZ=function() return 0,0 end},{__mul=function(a,b) return b.x and b or a end});package.loaded['openmw.util'].transform={identity=dummyT,rotateX=function() return dummyT end,rotateY=function() return dummyT end,rotateZ=function() return dummyT end,scale=function() return dummyT end}
package.loaded['openmw.animation']={}
package.loaded['openmw.camera']={MODE={FirstPerson=1},getMode=function() return 2 end}
package.loaded['openmw.types']={}
package.loaded['openmw.vfs']={pathsWithPrefix=function() return function() end end}
package.loaded[prefix..'client.midi']=dofile(root..'client/midi.lua')
package.loaded[prefix..'client.hosted-midi']=dofile(root..'client/hosted-midi.lua')
package.loaded[prefix..'client.midi-export']=dofile(root..'client/midi-export.lua')
package.loaded[prefix..'client.pose-defaults']=dofile(root..'client/pose-defaults.lua')
package.loaded[prefix..'client.instrument-profiles']=dofile(root..'client/instrument-profiles.lua')
package.loaded[prefix..'client.seated-pose']=dofile(root..'client/seated-pose.lua')
package.loaded[prefix..'client.performance']=dofile(root..'client/performance.lua')
local viewport=vector(1024,768)
local mouseDown=false
local ui={TYPE={Text='Text',TextEdit='TextEdit',Widget='Widget',Image='Image',Window='Window'},
    texture=function() return {} end,content=function(t) return t end,screenSize=function() return viewport end,
    showMessage=function() end}
function ui.create(layout)
    local element={layout=layout}
    ui.latest=element
    function element:update() check(self.layout~=nil,'updating destroyed UI') end
    function element:destroy() check(self.layout~=nil,'double UI destruction'); self.layout=nil end
    return element
end
package.loaded['openmw.ui']=ui
package.loaded['openmw.async']={callback=function(_,fn) return fn end}
local input={KEY={Escape=1,F8=100,F9=103,Enter=101,NP_Enter=102},isMouseButtonPressed=function() return mouseDown end}
for i,name in ipairs({'Z','S','X','D','C','V','G','B','H','N','J','M','Comma'}) do input.KEY[name]=i+1 end
local ctrl,shift=false,false
input.isCtrlPressed=function() return ctrl end;input.isShiftPressed=function() return shift end
for n,name in ipairs{'A','Delete','LeftArrow','RightArrow','UpArrow','DownArrow','Y'} do input.KEY[name]=200+n end
package.loaded['openmw.input']=input
local uiOverride=false
package.loaded['openmw.interfaces']={Controls={overrideUiControls=function(value) uiOverride=value end},UI={getMode=function() return mode end,
    addMode=function(m) mode=m end,removeMode=function() mode=nil end}}
package.loaded['openmw.core']={getRealTime=function() return now end}
package.loaded['openmw.mp']={isConnected=function() return connected end,
    getObjectMpNum=function() return 7 end,sendToServer=function(event,data) sent[#sent+1]=data;if autoLibraryReply and data.op:sub(1,3)=='lib' then autoLibraryReply(data) end end}
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
package.loaded[prefix..'client.recorder']=dofile(root..'client/recorder.lua')
package.loaded[prefix..'client.workstation']=dofile(root..'client/workstation.lua')
package.loaded[prefix..'client.controls']=dofile(root..'client/controls.lua')
package.loaded[prefix..'client.automation-roll']=dofile(root..'client/automation-roll.lua')
package.loaded[prefix..'client.piano-roll']=dofile(root..'client/piano-roll.lua')
package.loaded[prefix..'client.dropdown']=dofile(root..'client/dropdown.lua')
package.loaded[prefix..'client.arrangement']=dofile(root..'client/arrangement.lua')
package.loaded[prefix..'client.pattern-tools']=dofile(root..'client/pattern-tools.lua')
package.loaded[prefix..'client.arranger']=dofile(root..'client/arranger.lua')
package.loaded[prefix..'client.library']=dofile(root..'client/library.lua')
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
package.loaded[prefix..'client.actors']=dofile(root..'client/actors.lua')
package.loaded[prefix..'client.performer-panel']=dofile(root..'client/performer-panel.lua')
package.loaded[prefix..'client.pose-calibration']=dofile(root..'client/pose-calibration.lua')
package.loaded[prefix..'client.pose-editor']=dofile(root..'client/pose-editor.lua')
package.loaded[prefix..'client.listening']=dofile(root..'client/listening.lua')
package.loaded[prefix..'client.listening-panel']=dofile(root..'client/listening-panel.lua')
package.loaded[prefix..'shared.band-layout']=dofile(root..'shared/band-layout.lua')
package.loaded[prefix..'client.stage-preflight']=dofile(root..'client/stage-preflight.lua')
local player=dofile(root..'client/player.lua')
local api=player.interface
do
    -- A first-person player has no rendered arm pose yet, but must still be a
    -- camera target so focusing can make the body/held instrument visible.
    local actor={cell=self.cell,isValid=function() return true end}
    local camera=package.loaded['openmw.camera'];local old=camera.getMode;camera.getMode=function() return camera.MODE.FirstPerson end
    package.loaded['openmw.types'].Actor={}
    local a=package.loaded[prefix..'client.actors'].new({getPlayerObject=function() return actor end,getObjectMpNum=function() end},{actors={}},actor)
    a:frame({[7]={owner='test',performers={[1]={kind='player',guid='test',instrument='lute'}},availability={[1]='Ready'}}},.01,now)
    check(next(a.poses)==nil and #a:targets('lute')==1,'first-person held player is focusable before arm rendering starts')
    a:frame({[7]={owner='test',performers={},availability={}}},.01,now)
    check(#a:targets('keyboard')==1 and #a:targets('lute')==0,'placed keyboard owner is focusable without stale held targets')
    a:clearAll();check(#a:targets('keyboard')==0,'disconnect clears pose camera targets')
    camera.getMode=old
end
do
    local before=#sent
    check(api.poseTuner('lute') and uiOverride and mode=='Interface','pose tuner opens independently of a workstation')
    local tuner=api.poseTunerState();local slider=tuner.children[1].layout
    check(slider.content[1].props.text=='Position X','compact prop controls expose position and rotation')
    local field=slider.content[3].layout
    field.events.focusGain();field.events.textChanged('12.5')
    check(api.poseCalibration():profile('lute').position.x==12.5 and #sent==before,'numeric tuning changes the live profile without network/project mutation')
    field.events.keyPress{code=input.KEY.Enter}
    slider.content[5].events.mouseClick()
    check(api.poseCalibration():profile('lute').position.x==12.75,'arrow steps update the same live profile')
    local value=api.poseCalibration():profile('lute').position.x
    field.events.focusGain();field.events.textChanged('nan');field.events.keyPress{code=input.KEY.Enter}
    check(api.poseCalibration():profile('lute').position.x==value,'invalid GUI text cannot corrupt a pose')
    tuner.page='Left';tuner:render();check(tuner.children[1].layout.content[1].props.text=='Wrist target X','hand page exposes IK targets')
    api.poseCalibration():reset('lute');player.engineHandlers.onKeyPress{code=input.KEY.Escape}
    check(not api.poseTunerState() and not uiOverride and mode==nil,'Escape releases UI control without ending a score')
end
local libraryStorage={}
local libraryServer=dofile('apps/openmw-server/scripts/procedural_synth_library.lua')({
    getCharacterStorageForCharacter=function(_,_,key) return libraryStorage[key] end,
    setCharacterStorageForCharacter=function(_,_,key,value) libraryStorage[key]=value;return true end,
},dofile(root..'shared/project.lua')(schema))
autoLibraryReply=function(data)
    local ok,response=pcall(libraryServer.handle,11,data)
    if not ok then response={op='libResult',requestId=data.requestId,message=tostring(response)} end
    response.version=1;response.id=data.id;response.epoch=data.epoch;player.eventHandlers.Synth_Message(response)
end
local function clientHandler(data) player.eventHandlers.Synth_Message(data) end
local handler=clientHandler
local function acceptRecovery()
    local l=api.projectLibrary();if l and l.action then l:snapshot(true) end
end
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
local firstPianoKey=e.root.layout.content[#e.root.layout.content-24]
firstPianoKey.events.mousePress{button=1,offset=vector(10,56)}
check(mouseVelocity==1,'piano press position controls velocity')
e:frame(200)
check(released and not e.mouseNote,'mouse release outside the piano stops its note')
local lastPianoKey=e.root.layout.content[#e.root.layout.content]
local upperNote;e.cb.noteOn=function(note) upperNote=note end
lastPianoKey.events.mousePress{button=1,offset=vector(10,56)};lastPianoKey.events.mouseRelease()
check(upperNote==84,'two-octave keyboard reaches closing C6')
local Dropdown=package.loaded[prefix..'client.dropdown'];local choices={}
for n=1,512 do choices[n]={id=n,label='Song '..n,search='folder/song '..n} end
local picked;local d=Dropdown.open(e,choices,nil,function(id) picked=id end,30,140,850)
check(e.dropdown==d and e.typing,'dropdown suppresses musical typing')
d:scrollTo(9999);check(d.offset==504,'scrollbar reaches final eight entries without paging')
d:wheel(1);check(d.offset==501,'wheel scrolls three rows')
d:scrollTo(-100);check(d.offset==0,'scroll position clamps at start')
local captured=d.thumb
d.thumb.layout.events.mousePress{button=1,offset=vector(3,8)}
d.thumb.layout.events.mouseMove{button=1,offset=vector(3,230*e.window.scale)}
check(d.offset>450 and d.thumb==captured,'thumb drag advances while preserving the captured widget')
d.field.layout.events.textChanged('song 512')
check(#Dropdown.filtered(choices,d.query)==1,'512-entry catalog filters directly')
d.rows[1].layout.events.mouseClick()
check(picked==512 and not e.dropdown and not e.typing,'selection closes popup and chooses exact song')
d=Dropdown.open(e,choices,nil,function() end,30,140,850);d.field.layout.events.textChanged('[')
check(#Dropdown.filtered(choices,d.query)==0,'search treats pattern characters literally')
e:key(input.KEY.Escape,true);check(not e.dropdown and mode=='Interface','Escape dismisses dropdown before hiding window')
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
        if item.content and item.content[1] and item.content[1].props and item.content[1].props.text==title then return item end
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
player=dofile(root..'client/player.lua');api=player.interface;handler=clientHandler
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
check(lastEditor.cb.midiChoices()[1].label=='test' and lastEditor.cb.midiChoices()[1].id==virtualPath,'MIDI list shows only the song name while preserving its loading path')
check(api.playMidi(virtualPath));player.engineHandlers.onFrame(0.01)
local finish
for _,element in ipairs(lastEditor.root.layout.content) do
    if element.content and element.content[1] and element.content[1].props and element.content[1].props.text=='Finish' then finish=element end
end
check(finish~=nil,'Finish control is available')
finish.events.mouseClick();acceptRecovery()
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
        if element.content and element.content[1] and element.content[1].props and element.content[1].props.text==text then return element end
    end
    error('Missing button '..text)
end
button('+ Piano clip').events.mouseClick();now=400.01;player.engineHandlers.onFrame(0.01)
check(w.page=='piano' and #w.project.tracks==1,'piano clip button switches to editable piano roll')
local inserted=false
for _,element in ipairs(lastStudio.root.layout.content) do
    if element.props and element.props.position and element.props.position.x==76 and element.props.position.y==260 and element.events then
        element.events.mousePress{button=1,offset=vector(1,1)};inserted=true;break
    end
end
check(inserted and #w.project.tracks[1].notes==1,'piano grid click creates an actual note')
now=400.02;player.engineHandlers.onFrame(0.01)
local function dragNote(resize,steps)
    local roll=lastStudio.roll;local note=roll.pool[1];local props=note.layout.props;local scale=lastStudio.window.scale
    local offset=resize and props.size.x-1 or 1;local rootPosition=lastStudio.root.layout.props.position
    local point=vector(rootPosition.x+props.position.x+offset,rootPosition.y+38*scale+props.position.y+5)
    local history=#w.undoStack;local old=w.project.tracks[1].notes[1].s
    mouseDown=true;note.layout.events.mousePress{button=1,position=point,offset=vector(offset,5)}
    note.layout.events.mouseMove{button=1,position=vector(point.x+steps*roll.cw*scale,point.y),offset=vector(offset,5)}
    check(#w.undoStack==history and w.project.tracks[1].notes[1].s==old,'drag previews do not mutate the song or rebuild history')
    mouseDown=false;now=now+0.001;lastStudio:frame(now)
    check(#w.undoStack==history+1,'release commits exactly one history action')
end
dragNote(false,2)
check(w.project.tracks[1].notes[1].s==2,'mouse drag moves a note on the snapped time grid')
check(w:undo() and w.project.tracks[1].notes[1].s==0,'drag undo restores its original start')
now=now+0.001;lastStudio:frame(now)
viewport=vector(1920,1440);lastStudio.window:zoom(0.5);now=now+0.001;lastStudio:frame(now)
dragNote(true,2)
check(w.project.tracks[1].notes[1].l==3,'resize uses physical coordinates correctly at enlarged GUI scale')
check(w:undo() and w.project.tracks[1].notes[1].l==1,'resize undo restores gate')
-- Automation uses the expanded workspace and the same native header hit testing.
w.page='auto';w.selected=1;w.automationName='cutoff';w.automationDraft=nil;w.revision=w.revision+1;now=now+.01;lastStudio:frame(now)
check(lastStudio.automationRoll and button('Import MIDI controls') and button('Follow programs'),'automation page exposes lane import and program policy')
local ar=lastStudio.automationRoll;local count=#(w.project.tracks[1].automation and w.project.tracks[1].automation.cutoff or {})
local pos=lastStudio.root.layout.props.position;local scale=lastStudio.window.scale
ar.element.layout.events.mousePress{button=1,position=vector(pos.x+(ar.x+ar.width*.23)*scale,pos.y+(38+ar.y+ar.height*.4)*scale)}
now=now+.01;lastStudio:frame(now);ar=lastStudio.automationRoll
local points=w.project.tracks[1].automation.cutoff;check(#points==count+1,'scaled fullscreen automation graph paints point')
local point=points[#points];local px=(point.s-ar.start)/ar.steps*ar.width;local py=(1-(point.v-ar.spec.min)/(ar.spec.max-ar.spec.min))*ar.height
local handle;for _,child in ipairs(ar.element.layout.content) do if child.events and child.events.mousePress then handle=child end end
assert(handle);local before=#w.undoStack;mouseDown=true
handle.events.mousePress{button=1,position=vector(pos.x+(ar.x+px)*scale,pos.y+(38+ar.y+py)*scale),offset=vector(5*scale,5*scale)}
handle.events.mouseMove{button=1,position=vector(pos.x+(ar.x+px+ar.width/16)*scale,pos.y+(38+ar.y+py-20)*scale),offset=vector(5*scale,5*scale)}
mouseDown=false;now=now+.01;lastStudio:frame(now)
check(#w.undoStack==before+1 and w.project.tracks[1].automation.cutoff[#points].s~=point.s,'automation drag commits once on release')
check(w:undo() and w.project.tracks[1].automation.cutoff[#points].s==point.s,'automation drag Undo restores authored point')
now=now+.01;lastStudio:frame(now);ar=lastStudio.automationRoll
for _,child in ipairs(ar.element.layout.content) do if child.events and child.events.mousePress then handle=child end end
handle.events.mousePress{button=3,offset=vector(5,5)};now=now+.01;lastStudio:frame(now)
check(#(w.project.tracks[1].automation.cutoff or {})==count and mode=='Interface','automation right-click deletes point and retains editor')
check(w:undo(),'automation deletion Undo')
w.page='piano';w.revision=w.revision+1
viewport=vector(1024,768);lastStudio.window:zoom(-0.5);now=now+0.001;lastStudio:frame(now)
w:selectNote(1,1);ctrl=true;lastStudio:key(input.KEY.D,true);ctrl=false
check(#w.project.tracks[1].notes==2 and w.project.tracks[1].notes[2].s==1,'Ctrl+D duplicates selected notes without changing their sound')
check(w:undo() and #w.project.tracks[1].notes==1,'duplicate is one undo action')
now=now+0.001;lastStudio:frame(now)
w:selectNote(1,1);lastStudio:key(input.KEY.Delete,true)
check(#w.project.tracks[1].notes==0 and w:undo() and #w.project.tracks[1].notes==1,'Delete and undo work through the actual Studio key handler')
now=now+0.001;lastStudio:frame(now)
local generation=w.generation;button('Zoom -').events.mouseClick();now=now+0.001;lastStudio:frame(now)
check(lastStudio.roll.steps==32,'piano roll zoom exposes two bars')
lastStudio.roll:pan(16);check(lastStudio.roll.start==16 and w.generation==generation,'timeline navigation is local view state, not a musical edit')
lastStudio.roll:pan(-16);button('Zoom +').events.mouseClick();now=now+0.001;lastStudio:frame(now)
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
button('Band').events.mouseClick();now=400.171;player.engineHandlers.onFrame(0.001)
check(w.page=='band' and button('Refresh instruments') and button('This keyboard v'),'Band page exposes member discovery and per-track destinations')
local fields=0
for _,element in ipairs(lastStudio.root.layout.content) do
    if element.layout and element.layout.type=='TextEdit' and element.layout.props.position.y>=205 then fields=fields+1 end
    local props=element.props or element.layout and element.layout.props
    if props and props.position and props.size then check(props.position.x+props.size.x<=940 and props.position.y+props.size.y<=705,'Band controls fit the logical window') end
end
check(fields==8,'all eight stable member names are editable')
button('Refresh instruments').events.mouseClick()
button('Mixer').events.mouseClick();now=400.18;player.engineHandlers.onFrame(0.01)
local originalSet=w.project.soundSet
button('>').events.mouseClick();now=400.181;player.engineHandlers.onFrame(0.001)
check(w.project.soundSet=='rock','Mixer set arrow changes the full arrangement')
w:undo();now=400.182;player.engineHandlers.onFrame(0.001)
check(w.project.soundSet==originalSet,'one Undo restores the arrangement before the set click')
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
check(sent[#sent].op=='libSave' and sent[#sent].project.tracks[2].presetId=='b:snare','Save sends complete project to character storage')
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
-- A dropped miscellaneous keyboard is a valid sound source, and both windows
-- use the shared chrome action rather than a second pickup implementation.
source.recordId='fetcher_procedural_keyboard_v1'
package.loaded['openmw.nearby'].activators={};package.loaded['openmw.nearby'].items={source}
handler{version=1,op='granted',id=7,epoch=9,seq=1,params=schema.state({})}
api.studio();w=api.workstation();w.project.name='Portable song';w:touch()
local pickButton
for _,layout in ipairs(lastStudio.root.layout.template.content[2].content) do
    if layout.content and layout.content[1].props.text=='Pick up' then pickButton=layout end
end
check(pickButton~=nil,'Studio chrome exposes keyboard pickup')
pickButton.events.mouseClick();acceptRecovery()
check(sent[#sent].op=='pickup' and sent[#sent].project.name=='Portable song' and not api.status()[1].editorVisible,
    'pickup sends a dirty project snapshot and hides the UI until the transaction completes')
check(not api.show() and not api.pickupKeyboard(),'pending pickup cannot reopen or double-submit')
handler{version=1,op='error',message='Test transfer failure',pickupFailed=true,id=7,epoch=9}
check(api.status()[1].editorVisible and api.status()[1].editing,'native failure restores the editor and ownership')
check(api.pickupKeyboard(),'retry queues pickup after failure')
handler{version=1,op='pickedUp',id=7,epoch=9}
check(not api.status()[1].editing and audios[#audios].voices==0,'successful pickup closes the performance')
handler{version=1,op='close',id=7,epoch=9,seq=2}
handler{version=1,op='granted',id=7,epoch=10,seq=1,params=schema.state({})}
check(api.pickupKeyboard() and sent[#sent].project==nil,'untouched fresh editor does not overwrite an existing saved project during pickup')
handler{version=1,op='pickedUp',id=7,epoch=10}
handler{version=1,op='close',id=7,epoch=10,seq=2}
-- Misc items normally run ActionTake. The shared activation interface must
-- consume keyboard activation before the standard handler takes the item.
local activation,activations=nil,{}
local actor={sendEvent=function(_,name,data) activations[#activations+1]={name=name,data=data} end}
package.loaded['openmw.world']={players={actor}}
package.loaded['openmw.interfaces'].Activation={addGlobalHandler=function(fn) activation=fn end}
local global=dofile(root..'client/global.lua')
check(type(activation)=='function' and global.engineHandlers.onActivate==nil,'keyboard uses the default activation handler chain')
local custom={recordId='fargoth',attached=false,hasScript=function(o) return o.attached end,addScript=function(o) o.attached=true end}
global.engineHandlers.onActorActive(custom);check(not custom.attached,'ordinary actors never receive the Studio stand controller')
custom.recordId='fetcher_synth_bard_1_1';global.engineHandlers.onActorActive(custom)
check(custom.attached,'owned Studio performers receive their custom stand controller on activation')
check(activation(source,actor)==false and #activations==1 and activations[1].name=='Synth_Activate',
    'placed miscellaneous keyboard opens Studio and suppresses ActionTake')
source.recordId='fetcher_procedural_synth_v1'
check(activation(source,actor)==false and #activations==2,'legacy stage keyboard still opens Studio')
check(activation(source,{})==false and #activations==2,'NPC activation cannot open a player editor')
check(activation({recordId='ordinary_lute'},actor)==nil and #activations==2,'other items keep normal activation')
connected=false
check(activation(source,actor)==nil and #activations==2,'offline activation keeps normal behavior')
connected=true
package.loaded['openmw.synth'].version=5
local oldClient=dofile(root..'client/player.lua')
check(oldClient.interface==nil and oldClient.eventHandlers.Synth_Activate~=nil,'older engines receive an upgrade message before loading the new window controls')
-- Performer panel never opens from a remote invitation; F9 is explicit.
handler{version=1,op='performer',held='',available={lute=true},invitation={token='qa',leader='Leader',instrument='lute',slot=1}}
check(mode==nil,'invitation state does not force a GUI or lock the camera')
player.engineHandlers.onKeyPress{code=input.KEY.F9};check(mode=='Interface','F9 opens the performer panel')
for _,name in ipairs{'violin','cello','trumpet','flute','clarinet','timpani'} do
    local found=false;for _,element in ipairs(ui.latest.layout.content) do
        if element.content and element.content[1] and element.content[1].props.text=='Need item: '..name then found=true end
    end
    check(found,'F9 exposes the '..name..' inventory/ready choice')
end
api.respondToInvite(false);check(sent[#sent].op=='respond' and sent[#sent].token=='qa' and sent[#sent].accept==false,'decline sends only the pending consent token')
api.readyInstrument('lute');check(sent[#sent].op=='hold' and sent[#sent].instrument=='lute','ready requests a server inventory check')
api.leaveBand();check(sent[#sent].op=='leaveBand','member can leave without owning Studio')
player.engineHandlers.onKeyPress{code=input.KEY.F9};check(mode==nil,'F9 closes the performer panel and releases the UI mode')
-- Reproduce loaded-song reuse from the actual Studio callbacks, including Band.
package.loaded['openmw.synth'].version=17
local paths={'midi/a.mid','midi/b.mid'};local files={}
local function midiBytes(pitch,count)
    local events={};for n=1,count or 1 do events[#events+1]=string.char(0,144,pitch,100,96,128,pitch,0) end
    local bytes=table.concat(events)..string.char(0,255,47,0)
    return 'MThd'..be(6,4)..be(0,2)..be(1,2)..be(96,2)..'MTrk'..be(#bytes,4)..bytes
end
files[paths[1]]=midiBytes(60);files[paths[2]]=midiBytes(72)
package.loaded['openmw.vfs'].pathsWithPrefix=function() local at=0;return function() at=at+1;return paths[at] end end
package.loaded['openmw.vfs'].open=function(path) return {read=function() return assert(files[path]) end,close=function() end} end
now=600;player=dofile(root..'client/player.lua');api=player.interface;handler=clientHandler
player.engineHandlers.onFrame(0)
handler{version=1,op='granted',id=7,epoch=20,seq=1,params=schema.state({})};api.studio();w=api.workstation()
lastStudio.cb.playSong();acceptRecovery();now=now+0.01;player.engineHandlers.onFrame(0.01)
check(w.project.midi==paths[1] and w.song.events[1].note==60,'initial Studio Play uses selected song')
lastStudio.cb.midiSelect(paths[2]);lastStudio.cb.playSong();acceptRecovery();now=now+0.01;player.engineHandlers.onFrame(0.01)
check(w.project.midi==paths[2] and w.song.events[1].note==72,'new selection replaces loaded song without Finish')
w.page='band';w.revision=w.revision+1;now=now+0.01;player.engineHandlers.onFrame(0.01)
lastStudio.cb.cycleMidi(-1);lastStudio.cb.playSong();acceptRecovery();now=now+0.01;player.engineHandlers.onFrame(0.01)
check(w.project.midi==paths[1] and w.song.events[1].note==60,'Band-page song cycling cannot replay previous selection')
files[paths[2]]=midiBytes(72,140)
lastStudio.cb.midiSelect(paths[2]);lastStudio.cb.playSong();acceptRecovery();now=now+0.01;player.engineHandlers.onFrame(0.01)
check(lastStudio.cb.midiLabel():find('Loading'),'large parse yields instead of blocking')
lastStudio.cb.midiSelect(paths[1]);lastStudio.cb.playSong();acceptRecovery()
for n=1,10 do now=now+0.01;player.engineHandlers.onFrame(0.01) end
check(w.project.midi==paths[1] and w.song.events[1].note==60,'cancelled asynchronous parse cannot overwrite newer selection')
local Format=package.loaded[prefix..'shared.project'](schema)
w:load(Format.new());w:add('piano');w.page='piano';w.revision=w.revision+1;now=now+0.01;player.engineHandlers.onFrame(0.01)
button('Record keyboard >').events.mouseClick();now=now+0.01;player.engineHandlers.onFrame(0.01)
check(w.page=='record','piano roll opens the dedicated recording keyboard')
button('Start take').events.mouseClick();now=now+2.13;player.engineHandlers.onFrame(0.01)
lastStudio:key(input.KEY.Z,true);lastStudio:key(input.KEY.Z,true)
now=now+0.38;lastStudio:key(input.KEY.Z,false);button('Keep take').events.mouseClick()
check(#w:noteData(1)==1 and w:noteData(1)[1].n==60 and w:noteData(1)[1].s==1 and w:noteData(1)[1].l==3,'actual keyboard callback records a quantized gate without key-repeat duplication')
-- Recording setup uses local preferences, with real picker/field callbacks.
button('Recording setup >').events.mouseClick();now=now+.01;lastStudio:frame(now)
check(w.page=='recordsetup' and button('Count-in: 1 bars v'),'Recording exposes setup without replacing the project')
local gen=w.generation
button('Count-in: 1 bars v').events.mouseClick();lastStudio.dropdown.rows[3].layout.events.mouseClick();now=now+.01;lastStudio:frame(now)
check(w.recorder.settings.countIn==2 and w.generation==gen,'count-in picker is a local two-bar preference')
button('Metronome: Off').events.mouseClick();now=now+.01;lastStudio:frame(now)
check(w.recorder.settings.metronome and button('Metronome: On'),'metronome toggle updates local state')
w.recorder:set('countIn',1);w.recorder:set('metronome',false);now=now+.01;lastStudio:frame(now)
button('< Recording').events.mouseClick();now=now+.01;lastStudio:frame(now)
check(w.page=='record','setup returns to recording keyboard')
-- Physical MIDI polling follows the real visible editor, not just Recording.
do
    local native=package.loaded['openmw.synth'];local previous=native.openMidiInput
    local batch,closed={},false
    native.openMidiInput=function() return {close=function() closed=true end,poll=function()
        local events=batch;batch={};return {connected=true,dropped=0,events=events}
    end} end
    assert(w.recorder:connect(0));local port=w.recorder.input
    local function midi(status,n,v)
        batch={{status=status,channel=1,a=n,b=v,age=0}};now=now+.01;player.engineHandlers.onFrame(.01)
    end
    w.selected=1;w.page='piano';w.revision=w.revision+1;local generation=w.generation
    midi(144,60,100)
    check(sounding(60) and w.recorder.input==port and not closed and sent[#sent].channel==1,'Piano page polls MIDI and plays selected part without disconnecting')
    midi(144,60,0);check(not sounding(60) and w.generation==generation,'unarmed Piano MIDI releases without modifying the song')
    assert(w:add('drum'));w.page='drums';w.revision=w.revision+1;midi(144,38,90)
    check(sounding(38) and sent[#sent].channel==w.selected and w.recorder.input==port,'Drums page plays selected lane through the MIDI port')
    api.sound(1)
    check(not sounding(38) and w.recorder.input==port,'opening another sound releases old MIDI gate while retaining device')
    midi(144,64,110)
    check(sounding(64) and sent[#sent].channel==1 and w.recorder.input==port,'track Synth editor monitors the sound being edited')
    api.sound(0);check(not sounding(64),'switching to manual Synth releases track gate')
    midi(144,65,80)
    check(sounding(65) and sent[#sent].channel==0 and w.recorder.inputTrack==0,'main manual Synth uses channel zero instead of selected drum lane')
    api.studio();check(not sounding(65) and w.recorder.input==port,'returning to Studio releases manual voice and keeps device')
    w.selected=1;w.page='piano';w.revision=w.revision+1;midi(144,67,100)
    api.hide();check(closed and not w.recorder.input and not sounding(67),'Hide immediately closes MIDI port and releases physical held note')
    api.show();native.openMidiInput=previous
end
-- New Song uses the real window callbacks and cancels a pending file parser.
lastStudio.cb.midiSelect(paths[2]);lastStudio.cb.playSong();acceptRecovery();now=now+0.01;player.engineHandlers.onFrame(0.01)
check(lastStudio.cb.midiLabel():find('Loading'),'file parser is pending before New Song')
button('New Song').events.mouseClick();now=now+0.01;player.engineHandlers.onFrame(0.01)
check(w.page=='newsong' and button('Create song'),'New Song opens settings before replacing the document')
button('Create song').events.mouseClick();acceptRecovery()
for n=1,12 do now=now+0.01;player.engineHandlers.onFrame(0.01) end
check(w.project.composition and w.project.midi=='' and #w:noteData(1)==0 and not lastStudio.cb.midiLabel():find('Loading'),'Create cancels old file parse and leaves a blank source-free song')
local score
w.cb.schedule=function(plan) score=plan;return true end
w:addNote(1,65,0);lastStudio.cb.playSong();acceptRecovery()
check(score and score.bytes:byte(10)==65 and w.project.midi=='','Studio Play uses the authored arrangement despite an installed song selection')
w:stop(true);w.page='piano';w.revision=w.revision+1;now=now+0.01;player.engineHandlers.onFrame(0.01)
button('+ Piano').events.mouseClick();now=now+0.01;player.engineHandlers.onFrame(0.01)
check(w.project.tracks[2].edited and w.project.tracks[2].kind=='midi','Piano plus button adds a full-song part inside authored songs')
button('Song').events.mouseClick();now=now+0.01;player.engineHandlers.onFrame(0.01)
check(w.page=='song' and button('+ Drum part v') and button('Song loop'),'Song page exposes parts and independent song loop settings')
for _,element in ipairs(lastStudio.root.layout.content) do
    local props=element.props or element.layout and element.layout.props
    if props and props.position and props.size then check(props.position.x+props.size.x<=940 and props.position.y+props.size.y<=705,'Song controls fit the logical window') end
end
button('Save').events.mouseClick();local composed=sent[#sent].project
check(composed.composition and composed.midi=='','window Save sends source-free composition metadata')
api.selectMidi(paths[1]);handler{version=1,op='dawProject',id=7,epoch=20,project=composed}
lastStudio.cb.playSong();acceptRecovery()
check(w.project.composition and w.project.midi=='' and score.bytes:byte(10)==65,'load clears explicit file selection before authored-song Play')
lastStudio.cb.midiSelect(paths[1]);lastStudio.cb.playSong();acceptRecovery();now=now+0.01;player.engineHandlers.onFrame(0.01)
check(w.project.midi==paths[1] and not w.project.composition,'explicit file selection still replaces an authored song')
-- Exercise Timing map controls and actual song ruler widget geometry.
w:newSong(120,4,4,8);w.page='song';now=now+.01;lastStudio:frame(now)
button('Timing map >').events.mouseClick();now=now+.01;lastStudio:frame(now)
check(w.page=='timing' and button('Set tempo') and button('Set meter'),'Song opens dedicated timing editor')
w.timingBeat=8;w.timingBpm=60;button('Set tempo').events.mouseClick();now=now+.01;lastStudio:frame(now)
w.timingNumerator=3;w.timingDenominator=4;button('Set meter').events.mouseClick();now=now+.01;lastStudio:frame(now)
check(w.project.timeline.tempos[2].tempo==1000000 and Format.barStart(w.project,4)==11,'Timing callbacks commit tempo/meter map')
w.timingBeat=14;w.timingNumerator=7;w.timingDenominator=8;button('Set meter').events.mouseClick();now=now+.01;lastStudio:frame(now)
w:addNote(1,60,32);w.rollStart=32;w.rollSteps=32;w.page='piano';w.revision=w.revision+1;now=now+.01;lastStudio:frame(now)
local roll=lastStudio.roll;check(roll.songGrid and #roll.barGrid.layout.content>=3,'Piano uses mapped song bar grid')
check(math.abs(roll.barGrid.layout.content[2].props.position.x-12*roll.cw)<.001,'3/4 passage has twelve sixteenth steps per bar')
local count=#w:noteData(1);local scale=lastStudio.window.scale
roll.barGrid.layout.events.mousePress{button=1,offset=vector(roll.cw*2*scale,roll.rh*.5*scale)}
check(#w:noteData(1)==count+1,'bar overlay forwards empty-grid painting')
w.page='song';w.revision=w.revision+1;now=now+.01;lastStudio:frame(now)
button('Timing map >').events.mouseClick();now=now+.01;lastStudio:frame(now)
button('Remove meter').events.mouseClick();now=now+.01;lastStudio:frame(now)
check(#w.project.timeline.meters==1 and w:undo(),'Timing removal has one undo action')
-- Exercise real arranger widgets, mouse capture and shared-source navigation.
w:newSong(120,4,4,16);w:addNote(1,60,0);w:addNote(1,64,4);w:selectAllNotes(1)
button('Piano').events.mouseClick();now=now+0.01;player.engineHandlers.onFrame(0.01)
button('Make clip').events.mouseClick();now=now+0.01;player.engineHandlers.onFrame(0.01)
check(w.page=='arrange' and lastStudio.arranger and #w.project.tracks[1].arrangement.regions==1,'Make clip opens the timeline with a linked placement')
local function dragRegion(resize,bars)
    local arranger=lastStudio.arranger;local entry=arranger.pool[1];local props=entry.element.layout.props;local scale=lastStudio.window.scale
    local offset=resize and props.size.x-1 or 2;local rootPosition=lastStudio.root.layout.props.position
    local point=vector(rootPosition.x+props.position.x+offset,rootPosition.y+38*scale+props.position.y+5)
    local history=#w.undoStack;local source=w.project.tracks[1].arrangement.regions[1];local old=source.s
    mouseDown=true;entry.element.layout.events.mousePress{button=1,position=point,offset=vector(offset,5)}
    entry.element.layout.events.mouseMove{button=1,position=vector(point.x+bars*arranger.bar*arranger.cw*scale,point.y),offset=vector(offset,5)}
    check(#w.undoStack==history and source.s==old,'arrangement drag previews leave document/history untouched')
    mouseDown=false;now=now+0.01;lastStudio:frame(now)
    check(#w.undoStack==history+1,'placement drag commits once on release')
end
local ar=lastStudio.arranger;local entry=ar.pool[1];local props=entry.element.layout.props;local scale=lastStudio.window.scale;local rootPosition=lastStudio.root.layout.props.position
local point=vector(rootPosition.x+props.position.x+props.size.x-1,rootPosition.y+38*scale+props.position.y+5)
local gen=w.generation
ar:press(1,{button=1,position=point,offset=vector(props.size.x-1,5)});ar:move{position=point};ar:finish(false)
check(w.generation==gen,'stationary placement drag does not change history or snap an existing endpoint')
dragRegion(false,2)
check(w.project.tracks[1].arrangement.regions[1].s==32,'timeline drag moves a placement by whole bars')
viewport=vector(1920,1440);lastStudio.window:zoom(0.5);now=now+0.01;lastStudio:frame(now)
dragRegion(true,2)
check(w.project.tracks[1].arrangement.regions[1].l==48,'right-edge drag repeats the source at enlarged GUI scale')
button('Duplicate placement').events.mouseClick();now=now+0.01;player.engineHandlers.onFrame(0.01)
check(#w.project.tracks[1].arrangement.regions==2 and #w.project.tracks[1].arrangement.clips==1,'timeline duplicate links a single source')
local gen=w.generation;local ar=lastStudio.arranger;local rootPosition=lastStudio.root.layout.props.position;local scale=lastStudio.window.scale
local scrollPoint=vector(rootPosition.x+(ar.x+ar.width-2)*scale,rootPosition.y+(38+565)*scale)
mouseDown=true;ar.thumb.layout.events.mousePress{button=1,position=scrollPoint,offset=vector(1,1)}
check(w.arrangeBar==ar.maximum and w.generation==gen,'timeline scrollbar moves view without a project edit')
mouseDown=false;now=now+0.01;lastStudio:frame(now)
w.arrangeBar=0;w.revision=w.revision+1;now=now+0.01;lastStudio:frame(now)
w:changeRegion(1,2,{l=160});now=now+0.01;lastStudio:frame(now)
local clipped=lastStudio.arranger.pool[2];local props=clipped.element.layout.props;local scale=lastStudio.window.scale;local rootPosition=lastStudio.root.layout.props.position
local point=vector(rootPosition.x+props.position.x+props.size.x-1,rootPosition.y+38*scale+props.position.y+5)
mouseDown=true;clipped.element.layout.events.mousePress{button=1,position=point,offset=vector(props.size.x-1,5)}
check(not lastStudio.arranger.drag.resize,'a placement clipped by the viewport does not expose a false resize handle')
lastStudio.arranger:finish(true);mouseDown=false;w:undo();now=now+0.01;lastStudio:frame(now)
button('Edit source').events.mouseClick();now=now+0.01;player.engineHandlers.onFrame(0.01)
check(w:clipSource() and lastStudio.roll and button('Play source'),'source opens in the existing piano roll with a dedicated audition button')
w:selectAllNotes(1);lastStudio.roll:draw()
local roll=lastStudio.roll;local widget=roll.pool[1];local props=widget.layout.props;local scale=lastStudio.window.scale;local rootPosition=lastStudio.root.layout.props.position
local point=vector(rootPosition.x+props.position.x+props.size.x-1,rootPosition.y+38*scale+props.position.y+5)
mouseDown=true;widget.layout.events.mousePress{button=1,position=point,offset=vector(props.size.x-1,5)}
widget.layout.events.mouseMove{button=1,position=vector(point.x+200*roll.cw*scale,point.y),offset=vector(1,5)}
mouseDown=false;now=now+0.01;lastStudio:frame(now)
local maxEnd=0;for _,note in ipairs(w:noteData(1)) do maxEnd=math.max(maxEnd,note.s+note.l) end
check(maxEnd==w:clipSource().length,'source drag clamps group gates to the source boundary')
w:undo();w:editClip(1,1);now=now+0.01;lastStudio:frame(now)
w:selectAllNotes(1);w:editSelected(1,{pitch=2});button('Arrange clips').events.mouseClick();now=now+0.01;player.engineHandlers.onFrame(0.01)
check(not w.clipEdit and w.project.tracks[1].arrangement.clips[1].notes.bytes~=composed.tracks[1].edited.bytes,'returning to arranger exits source editing')
button('Save').events.mouseClick();check(sent[#sent].project.tracks[1].arrangement.regions[2].clip==1,'actual Save sends compact source references')
-- Real multi-selection, preview/release, unique/copy/offset controls and shortcuts.
w:newSong(120,4,4,16);w:addNote(1,60,0);w:selectAllNotes(1);w:captureClip(1);w:duplicateRegion(1,1)
w:addPart('bass');w:addNote(2,36,32);w:selectAllNotes(2);w:captureClip(2)
w.page='arrange';w.revision=w.revision+1;now=now+.01;lastStudio:frame(now)
local function pressPlacement(slot,modifier)
    local a=lastStudio.arranger;local entry=a.pool[slot];local p=entry.element.layout.props;local scale=lastStudio.window.scale;local root=lastStudio.root.layout.props.position
    local point=vector(root.x+p.position.x+3,root.y+38*scale+p.position.y+5)
    input.isShiftPressed=function() return modifier end
    entry.element.layout.events.mousePress{button=1,position=point,offset=vector(3,5)}
    return point
end
pressPlacement(1,false);lastStudio.arranger:finish(false);pressPlacement(3,true)
check(#w:selectedRegions()==2 and not lastStudio.arranger.drag,'Shift-click selects placements across tracks without moving')
now=now+.01;lastStudio:frame(now);local gen=w.generation;local history=#w.undoStack
local point=pressPlacement(1,false);local a=lastStudio.arranger;local scale=lastStudio.window.scale;mouseDown=true
a:move{position=vector(point.x+a.bar*a.cw*scale,point.y)}
check(w.generation==gen and w.project.tracks[1].arrangement.regions[1].s==0,'group drag remains a nonmutating preview')
local x1=a.pool[1].element.layout.props.position.x;local x2=a.pool[3].element.layout.props.position.x
check(math.abs((x2-x1)/scale-32*a.cw)<.01,'group preview preserves cross-track spacing')
mouseDown=false;now=now+.01;lastStudio:frame(now)
check(w.project.tracks[1].arrangement.regions[1].s==16 and w.project.tracks[2].arrangement.regions[1].s==48 and #w.undoStack==history+1,'group release commits both tracks once')
button('Make unique').events.mouseClick();now=now+.01;lastStudio:frame(now)
check(#w.project.tracks[1].arrangement.clips==2 and #w.project.tracks[2].arrangement.clips==2,'Make unique button detaches each selected source')
button('Copy source to v').events.mouseClick();now=now+.01;lastStudio:frame(now)
check(lastStudio.dropdown,'source transfer opens destination picker')
check(lastStudio.dropdown.visibleEntries[1].id==2,'destination list targets the other part');lastStudio.dropdown.rows[1].layout.events.mouseClick();now=now+.01;lastStudio:frame(now)
check(w.selected==2 and #w.project.tracks[2].arrangement.clips==3,'destination picker copies source without changing destination instrument')
w:selectRegion(2,1,false);w.revision=w.revision+1;now=now+.01;lastStudio:frame(now)
local offsetField
for _,e in ipairs(lastStudio.children) do if e.layout.type==ui.TYPE.TextEdit then
    local p=e.layout.props;if p.position and math.abs(p.position.x-215*lastStudio.window.scale)<.1 and p.text=='0' then offsetField=e end
end end
assert(offsetField);offsetField.layout.events.focusGain();offsetField.layout.events.textChanged('0.5');offsetField.layout.events.keyPress{code=input.KEY.Enter}
check(w.project.tracks[2].arrangement.regions[1].offset==2,'offset text field stores quarter units in placement phase')
now=now+.01;lastStudio:frame(now);ctrl=true;lastStudio:key(input.KEY.A,true);ctrl=false
check(#w:selectedRegions()==3,'Ctrl+A selects all arrangement placements')
ctrl=true;lastStudio:key(input.KEY.D,true);ctrl=false;check(#w.project.tracks[1].arrangement.regions==4,'Ctrl+D duplicates selected group')
lastStudio:key(input.KEY.Delete,true);check(#w.project.tracks[1].arrangement.regions==2 and #w:selectedRegions()==0,'Delete removes only selected duplicates')
input.isShiftPressed=function() return false end
-- Fullscreen grows the musical viewport without stretching fonts or editing notes.
assert(w:newSong(120,4,4,8));assert(w:addNote(1,60,0));w.page='piano';w.rollSteps=16;w.rollStart=0
viewport=vector(2560,1440);lastStudio.window.preferred=1.3;lastStudio.window.full=false
lastStudio.window.width=940*1.3;lastStudio.window.height=733*1.3
w.revision=w.revision+1;now=now+.01;lastStudio:frame(now)
local win=lastStudio.window;local oldWidth,oldHeight=lastStudio.roll.width,lastStudio.roll.height
local oldRows,oldSteps=lastStudio.roll.rows,lastStudio.roll.steps
local generation,history=w.generation,#w.undoStack
win:fullscreen();now=now+.01;lastStudio:frame(now)
local roll=lastStudio.roll
check(roll.width>oldWidth and roll.height>oldHeight and roll.rows>oldRows and roll.steps>oldSteps,'1440p fullscreen grows time and pitch capacity')
check(math.abs(win.scale-1.3)<1e-6 and roll.width*win.scale>2400,'fullscreen uses available width while keeping chosen text scale')
check(w.generation==generation and #w.undoStack==history,'viewport expansion is not a song edit')
local position=lastStudio.root.layout.props.position;local grid
for _,item in ipairs(lastStudio.root.layout.content) do if item.events and item.props and item.props.position and math.abs(item.props.position.x-roll.x*win.scale)<.01 and math.abs(item.props.position.y-roll.y*win.scale)<.01 then grid=item end end
assert(grid);local row=math.min(15,roll.rows-1);local expected=roll.base+roll.rows-1-row
local point=vector(position.x+(roll.x+roll.cw*3)*win.scale,position.y+(38+roll.y+(row+.5)*roll.rh)*win.scale)
grid.events.mousePress{button=1,position=point,offset=vector(3*roll.cw*win.scale,(row+.5)*roll.rh*win.scale)}
check(w:noteData(1)[2].n==expected and w:noteData(1)[2].s==3,'expanded grid hit testing places the intended pitch and time')
now=now+.01;lastStudio:frame(now);roll=lastStudio.roll
w:selectNote(1,2);roll:draw();local note=roll.pool[2];local props=note.layout.props
point=vector(position.x+props.position.x+5,position.y+38*win.scale+props.position.y+5)
mouseDown=true;note.layout.events.mousePress{button=1,position=point,offset=vector(5,5)}
note.layout.events.mouseMove{button=1,position=vector(point.x+roll.cw*win.scale,point.y-roll.rh*win.scale),offset=vector(5,5)}
mouseDown=false;now=now+.01;lastStudio:frame(now)
check(w:noteData(1)[2].n==expected+1,'dragging an expanded note moves one pitch row')
generation=w.generation;history=#w.undoStack
viewport=vector(3440,1440);now=now+.01;lastStudio:frame(now)
check(lastStudio.roll.width>roll.width and w.generation==generation,'ultrawide viewport expands without a musical edit')
w.base=116;w.revision=w.revision+1;now=now+.01;lastStudio:frame(now)
check(lastStudio.roll.base+lastStudio.roll.rows==128,'expanded pitch range stays within MIDI 0-127')
viewport=vector(800,600);now=now+.01;lastStudio:frame(now)
check(lastStudio.root.layout.props.size.x==800 and lastStudio.roll.rows>=12 and lastStudio.roll.width*win.scale<=800,'small viewport refits controls and retains usable rows')
viewport=vector(2560,1440);now=now+.01;lastStudio:frame(now);win:fullscreen();now=now+.01;lastStudio:frame(now)
check(math.abs(lastStudio.roll.width-oldWidth)<.01 and math.abs(lastStudio.roll.height-oldHeight)<.01,'Restore recovers previous editor dimensions')
w.page='drums';assert(w:add('drum'));assert(w:toggleStep(2,0));w.revision=w.revision+1;now=now+.01;lastStudio:frame(now)
button('Duplicate to next bar').events.mouseClick();now=now+.01;lastStudio:frame(now)
check(w.bar==1 and w.project.tracks[2].steps:sub(17,17)~='0','Drums button duplicates the displayed bar')
button('Mixer').events.mouseClick();now=now+.01;lastStudio:frame(now)
button('Rename track').events.mouseClick();now=now+.01;lastStudio:frame(now)
local field
for _,element in ipairs(lastStudio.children) do if element.layout.type==ui.TYPE.TextEdit and math.abs(element.layout.props.size.x-660*lastStudio.window.scale)<.1 then field=element end end
assert(field);field.layout.events.focusGain();field.layout.events.textChanged('My renamed drum')
button('Apply name').events.mouseClick();now=now+.01;lastStudio:frame(now)
check(w.project.tracks[w.selected].name=='My renamed drum' and w.page=='mixer','Rename track commits and returns to its originating page')
local exportData;local native=package.loaded['openmw.synth']
native.exportMidi=function(name,data) exportData=data;return {name='roundtrip.mid',path='test-profile/studio-midi/roundtrip.mid'} end
native.listMidiExports=function() return exportData and {'roundtrip.mid'} or {} end
native.readMidiExport=function(name) check(name=='roundtrip.mid','only listed native exports are read');return exportData end
button('Export MIDI').events.mouseClick();check(exportData and exportData:sub(1,4)=='MThd','Export MIDI button invokes native writer')
local present=false;for _,path in ipairs(api.midiSongs()) do if path=='midi/__studio_exports__/roundtrip.mid' then present=true end end
check(present and api.importMidi('midi/__studio_exports__/roundtrip.mid'),'new export is immediately eligible for the MIDI selector')
now=now+.01;player.engineHandlers.onFrame(.01)
check(w.project.midi=='midi/__studio_exports__/roundtrip.mid' and w.song,'export imports through the native bounded reader')
assert(w:newSong(120,4,4,8));assert(w:addNote(1,60,0))
-- Native hit testing chooses the last overlapping sibling, including empty Widgets.
local function topHit(content,x,y)
    for n=#content,1,-1 do
        local e=content[n].layout or content[n];local p=e.props or {};local at,size=p.position,p.size
        if p.visible~=false and at and size and x>=at.x and x<at.x+size.x and y>=at.y and y<at.y+size.y then
            return e
        end
    end
end
viewport=vector(2560,1440);local win=lastStudio.window;win.preferred=1.3;win.full=true;win:bounds();win.changed=true
w.page='mixer';w.revision=w.revision+1;now=now+.01;lastStudio:frame(now)
check(uiOverride,'Studio owns inventory/UI toggle while visible')
for _,caption in ipairs{'Song','Piano','Drums','Band','Samples','Synth'} do
    local tab=button(caption);local at,size=tab.props.position,tab.props.size
    check(topHit(lastStudio.root.layout.content,at.x+size.x/2,at.y+size.y/2)==tab,'fullscreen Mixer leaves '..caption..' tab clickable')
end
w.page='arrange';w.revision=w.revision+1;now=now+.01;lastStudio:frame(now)
check(lastStudio.bodyOffset==0 and lastStudio.arranger.width==lastStudio.width-240,'fullscreen arrangement uses all available horizontal workspace')
check(lastStudio.arranger.height>264 and lastStudio.arranger.y+lastStudio.arranger.height<lastStudio.height-103,'fullscreen arrangement grows vertically without covering tools')
local arrangeZoom=w.arrangeBars or 8
button('Zoom +').events.mouseClick();now=now+.01;lastStudio:frame(now)
check(w.arrangeBars==arrangeZoom/2,'arrangement zoom changes time density')
button('Zoom -').events.mouseClick();now=now+.01;lastStudio:frame(now)
check(w.arrangeBars==arrangeZoom,'arrangement zoom can be restored')
for _,caption in ipairs{'Song','Mixer','Piano','Drums','Band','Samples'} do
    local tab=button(caption);local at,size=tab.props.position,tab.props.size
    check(topHit(lastStudio.root.layout.content,at.x+size.x/2,at.y+size.y/2)==tab,'fullscreen arrangement leaves '..caption..' clickable')
end
button('Piano').events.mouseClick();now=now+.01;lastStudio:frame(now)
w.base=60;w.revision=w.revision+1;now=now+.01;lastStudio:frame(now)
local normalY,normalHeight=lastStudio.roll.y,lastStudio.roll.height
local geometry={win.x,win.y,win.width,win.height,win.full,win.preferred,preferences.fullscreen};local gen=w.generation
button('Expand editor').events.mouseClick();now=now+.01;lastStudio:frame(now)
check(lastStudio.pianoFocus and win.expanded and lastStudio.roll.y<normalY and lastStudio.roll.height>normalHeight,'dedicated editor gives piano the header space')
check(w.generation==gen and win.full==geometry[5] and preferences.fullscreen==geometry[7],'expanded editor is a transient view, not saved geometry or a song edit')
-- Paint in the newly available upper rows and right-click only one selected note.
local roll=lastStudio.roll;local grid
for _,item in ipairs(lastStudio.root.layout.content) do if item.events and item.props and item.props.position and math.abs(item.props.position.x-roll.x*win.scale)<.01 and math.abs(item.props.position.y-roll.y*win.scale)<.01 then grid=item end end
local pos=lastStudio.root.layout.props.position
local pt=vector(pos.x+(roll.x+roll.cw*3)*win.scale,pos.y+(38+roll.y+roll.rh*2.5)*win.scale)
local count=#w:noteData(w.selected)
grid.events.mousePress{button=1,position=pt,offset=vector(roll.cw*3*win.scale,roll.rh*2.5*win.scale)}
now=now+.01;lastStudio:frame(now);roll=lastStudio.roll
check(#w:noteData(w.selected)==count+1,'expanded editor paints notes at its own grid origin')
w:selectAllNotes(w.selected);roll:draw();local selectedCount=#w:selectedNotes(w.selected);local slot
for n in pairs(roll.visible) do slot=n;break end
assert(slot and selectedCount>1);local history=#w.undoStack
roll.pool[slot].layout.events.mousePress{button=3,offset=vector(5,5)}
check(#w:noteData(w.selected)==count and #w.undoStack==history+1 and mode=='Interface' and uiOverride,'right click deletes only the clicked note and keeps Studio open')
check(w:undo() and #w:noteData(w.selected)==count+1,'right-click delete supports Undo')
-- Native Escape may already have removed Interface when the Lua key arrives.
mode=nil;lastStudio.root.layout.events.keyPress{code=input.KEY.Escape};player.engineHandlers.onKeyPress{code=input.KEY.Escape};player.engineHandlers.onKeyRelease{code=input.KEY.Escape};now=now+.01;lastStudio:frame(now)
now=now+.01;lastStudio:frame(now)
check(not lastStudio.pianoFocus and not win.expanded and mode=='Interface' and lastStudio.roll.y==normalY,'Escape returns to normal Piano after native mode pop')
check(win.x==geometry[1] and win.y==geometry[2] and win.width==geometry[3] and win.height==geometry[4] and win.full==geometry[5],'Escape preserves original fullscreen/window geometry')
win.full=false;win:bounds();win.changed=true;now=now+.01;lastStudio:frame(now)
local oldSize=lastStudio.root.layout.props.size;button('Expand editor').events.mouseClick();now=now+.01;lastStudio:frame(now)
check(lastStudio.root.layout.props.size.x==viewport.x and lastStudio.root.layout.props.size.y==viewport.y and not win.full,'piano expansion fills viewport from a windowed Studio')
button('Back to Piano [Esc]').events.mouseClick();now=now+.01;lastStudio:frame(now)
check(lastStudio.root.layout.props.size.x==oldSize.x and lastStudio.root.layout.props.size.y==oldSize.y,'Back to Piano restores the windowed canvas')
api.hide();check(not uiOverride,'hiding Studio restores game inventory controls')
api.show();check(uiOverride,'reopening Studio reacquires UI controls')
-- Real Projects controls exercise Save As naming, explicit deletion, and unsaved Finish.
button('Projects').events.mouseClick();now=now+.01;lastStudio:frame(now)
check(w.page=='projects' and button('Save As new'),'header opens named project library')
button('Save As new').events.mouseClick();now=now+.01;lastStudio:frame(now)
local field
for _,e in ipairs(lastStudio.children) do if e.layout.type==ui.TYPE.TextEdit and e.layout.props.size.x>600*lastStudio.window.scale then field=e end end
assert(field);field.layout.events.focusGain();field.layout.events.textChanged('UI project copy')
button('Create project copy').events.mouseClick();now=now+.01;lastStudio:frame(now)
check(w.project.name=='UI project copy' and not w.dirty and api.projectLibrary().active>0,'Save As dialog names and saves an independent copy')
local l=api.projectLibrary();local total=#l.entries
button('Delete project').events.mouseClick();now=now+.01;lastStudio:frame(now)
check(#l.entries==total and button('Confirm delete'),'Delete requires explicit confirmation')
button('Cancel delete').events.mouseClick();now=now+.01;lastStudio:frame(now)
check(#l.entries==total,'cancel deletion keeps named project')
w:addNote(1,67,8);button('Finish').events.mouseClick();now=now+.01;lastStudio:frame(now)
check(l.action and button('Keep recovery and continue') and mode=='Interface','Finish with edits asks how to preserve the draft')
button('Cancel').events.mouseClick();now=now+.01;lastStudio:frame(now)
check(not l.action and w.dirty and mode=='Interface','Cancel Finish keeps editor and edits')
-- Every main track selector can reach the fourth page of a 32-part document.
do
    check(w:newSong(120,4,4,8),'create paging document')
    for n=2,32 do assert(w:addPart('keys')) end
    for _,spec in ipairs{{'song','songPage','Parts'},{'mixer','mixerPage','Tracks'},{'arrange','arrangePage','Tracks'},{'band','bandPage','Tracks'}} do
        w.page=spec[1];w.revision=w.revision+1;now=now+.01;lastStudio:frame(now)
        button(spec[3]..' 1-8 v').events.mouseClick()
        check(#lastStudio.dropdown.entries==4,'four track ranges on '..spec[1])
        lastStudio.dropdown.rows[4].layout.events.mouseClick();now=now+.01;lastStudio:frame(now)
        check(w[spec[2]]==3 and button(spec[3]..' 25-32 v'),'last track page selectable on '..spec[1])
    end
    for n=1,32 do w.project.tracks[n]=Format.newTrack('drum','Lane '..n,16) end
    w.page='band';w.revision=w.revision+1;now=now+.01;lastStudio:frame(now)
    button('Members 1-8 v').events.mouseClick();lastStudio.dropdown.rows[4].layout.events.mouseClick();now=now+.01;lastStudio:frame(now)
    check(w.memberPage==3 and button('Members 25-32 v'),'member roster pages independently through all 32 performers')
    w.selected=1;w.revision=w.revision+1;now=now+.01;lastStudio:frame(now)
    button('Part 1: drum v').events.mouseClick()
    check(#lastStudio.dropdown.entries==9,'Band exposes all nine physical instruments and the correct drum fallback')
    local popup=lastStudio.dropdown;popup.field.layout.events.textChanged('flute')
    w.selected=2;popup.rows[1].layout.events.mouseClick()
    check(w.project.tracks[1].instrument=='flute' and w.project.tracks[2].instrument==nil,'instrument picker retains its original part if selection changes while open')
    now=now+.01;lastStudio:frame(now)
    self.rotation=dummyT;self.position=package.loaded['openmw.util'].vector3(0,0,0)
    package.loaded['openmw.types'].Activator={record=function() return {model='meshes/keyboard.nif'} end}
    local nearby=package.loaded['openmw.nearby'];nearby.COLLISION_TYPE={World=1,Door=2,HeightMap=4}
    package.loaded['openmw.util'].bitOr=function() return 7 end
    nearby.castRay=function(a,b) return {hit=a.z>b.z,hitPos=package.loaded['openmw.util'].vector3(a.x,a.y,0),hitNormal={z=1}} end
    button('Auto NPC band (32 parts)').events.mouseClick()
    for n=1,32 do now=now+.02;player.engineHandlers.onFrame(.02) end
    local setup
    for n=#sent,1,-1 do if sent[n].op=='autoBand' then setup=sent[n];break end end
    check(setup.op=='autoBand' and #setup.project.tracks==32 and w.bandSetup,'Band automatic setup sends a validated document with a pending request')
    local targets,routes={},{};for n=1,32 do targets[n]={kind='npc',mpNum=1000+n,refId='fetcher_synth_bard_1_'..n,instrument='drum'};routes[n]=n end
    handler{version=1,op='bandSetup',id=setup.id,epoch=setup.epoch,requestId=setup.requestId,restage=false,targets=targets,routes=routes}
    check(not w.bandSetup and w.project.tracks[32].output.slot==32,'matching setup response routes the last track as well as the first')
    w.page='drums';w.revision=w.revision+1;now=now+.01;lastStudio:frame(now)
    button('Lanes 1-8 v').events.mouseClick();lastStudio.dropdown.rows[4].layout.events.mouseClick();now=now+.01;lastStudio:frame(now)
    check(w.drumPage==3 and button('Lanes 25-32 v'),'last drum-lane page selectable')
    check(w:newSong(120,4,4,8) and w.mixerPage==0 and w.drumPage==0,'new song resets pages from a larger document')
end
viewport=vector(1024,768);lastStudio.window:zoom(-0.5)
api.close();check(not uiOverride,'Finish restores game UI controls')
-- The normal installation can have a non-default global GUI scale. Native
-- screenSize remains physical pixels; Window layouts must use layer coordinates.
viewport=vector(2560,1440)
local guiViewport=vector(2133,1200)
ui.layers={indexOf=function(name) if name=='Windows' then return 1 end end,
    [1]={size=guiViewport}}
local scaledWindow=package.loaded[prefix..'client.window'].new()
scaledWindow.full=true;scaledWindow:bounds()
check(scaledWindow.actualWidth==2133 and scaledWindow.actualHeight==1200,'fullscreen fits the scaled GUI layer instead of physical resolution')
check(scaledWindow.canvasWidth*scaledWindow.scale==2133,'fullscreen canvas preserves the scaled layer width')
local layout=scaledWindow:root({})
check(layout.layout.props.size.x==2133 and layout.layout.props.size.y==1200,'root hit area stays within scaled viewport')
guiViewport=vector(1706,960);ui.layers[1].size=guiViewport;scaledWindow:bounds()
check(scaledWindow.actualWidth==1706 and scaledWindow.actualHeight==960,'GUI scale changes update fullscreen bounds')
layout:destroy();ui.layers=nil
-- Large installed libraries retain songs beyond the previous 512-item cutoff.
package.loaded['openmw.synth'].listMidiExports=nil
package.loaded['openmw.vfs'].pathsWithPrefix=function()
    local at=0;return function() at=at+1;if at<=1030 then return string.format('midi/large/%04d.mid',at) end end
end
player=dofile(root..'client/player.lua');api=player.interface;now=now+.01;player.engineHandlers.onFrame(.01)
local catalog=api.midiSongs()
check(#catalog==1024 and catalog[600]=='midi/large/0600.mid' and catalog[1024]=='midi/large/1024.mid','discovery includes 600-song test library and retains bounded 1024-song ceiling')
-- A fresh client has no local MIDI installation: catalogue, download, import,
-- local coexistence, and stale selection handling all run through the real player.
do
local function testHostedMidi()
api.close();connected=true;now=now+1
local localPath='midi/personal/local.mid'
package.loaded['openmw.vfs'].pathsWithPrefix=function()
    local done=false;return function() if not done then done=true;return localPath end end
end
local localReads=0
package.loaded['openmw.vfs'].open=function(path)
    check(path==localPath,'server MIDI never requires local VFS files');localReads=localReads+1
    return {read=function() return fileBytes end,close=function() end}
end
player=dofile(root..'client/player.lua');api=player.interface;handler=player.eventHandlers.Synth_Message
player.engineHandlers.onFrame(.01)
handler{version=1,op='granted',id=7,epoch=500,seq=1,params=schema.state({})}
local listId
for n=#sent,1,-1 do if sent[n].op=='midiCatalog' then listId=sent[n].requestId;break end end
check(listId~=nil,'Studio requests server catalogue on acquisition')
local remotePath='midi/server/remote.mid';local remoteHash=string.rep('a',64)
local function hostedMessage(op,data)
    data=data or {};data.version=1;data.op=op;data.id=7;data.epoch=data.epoch or 500
    handler(data)
end
local pf=package.loaded[prefix..'shared.project'](schema)
local saved=pf.new();saved.midi=remotePath;saved.tracks[1]=pf.newTrack('midi','Saved source',16)
hostedMessage('dawProject',{project=saved})
player.engineHandlers.onFrame(.01)
check(api.workstation().project.midi==remotePath and not api.workstation():hasSong(),'saved project waits for server catalogue instead of losing its source')
hostedMessage('midiCatalogBegin',{requestId=listId,count=1})
hostedMessage('midiCatalogPage',{requestId=listId,offset=0,entries={{path=remotePath,size=#fileBytes,hash=remoteHash}}})
hostedMessage('midiCatalogEnd',{requestId=listId})
check(#api.midiSongs()==2 and api.midiSongs()[1]==localPath,'server library merges with personal files')
player.engineHandlers.onFrame(.01)
local fileId
for n=#sent,1,-1 do if sent[n].op=='midiFile' then fileId=sent[n].requestId;break end end
hostedMessage('midiFileBegin',{requestId=fileId,path=remotePath,size=#fileBytes,hash=remoteHash})
hostedMessage('midiFileChunk',{requestId=fileId,offset=0,bytes=fileBytes})
hostedMessage('midiFileEnd',{requestId=fileId})
for _=1,10 do player.engineHandlers.onFrame(.01) end
check(api.workstation().project.midi==remotePath and api.workstation():hasSong() and localReads==0,'downloaded MIDI imports with no local file')
check(api.importMidi(localPath),'personal MIDI remains selectable');player.engineHandlers.onFrame(.01)
check(api.workstation().project.midi==localPath and localReads==1,'local MIDI uses its original reader')
api.close();hostedMessage('midiFileEnd',{requestId=fileId})
connected=false;player.engineHandlers.onFrame(.01)
check(#api.midiSongs()==1 and api.midiSongs()[1]==localPath,'disconnect removes prior server library but preserves personal songs')
end
testHostedMidi()
end
print(string.format('PASS client/editor lifecycle, chords, edits, presets, selection/recording, listener cleanup; %d assertions',checks))
