local root=arg[1] or '.'
local base=root..'/files/server/server-lua-packages/fetcher.hotkeys/'
local checks=0
local function check(value,message) checks=checks+1;assert(value,message) end
local function load(path) return assert(loadfile(path))() end
local Model=load(base..'shared/model.lua')
local VoiceText=load(base..'client/voice-text.lua')
check(Model.state().pages==3,'three pages by default')
local saved=Model.state{pages=3,base=2,slots={['1']={kind='item',id='Potion'},['20']={kind='voice',id='sound/vo/x.mp3'},['160']={kind='item',id='last'}}}
check(saved.slots['1'].id=='potion' and saved.slots['20'].kind=='voice','bindings normalized')
saved.pages=1;saved.base=1;saved=Model.state(saved)
check(saved.slots['20'] and saved.slots['160'],'hiding pages never destroys their assignments')
check(not Model.binding{kind='command',id='/admin'},'arbitrary commands rejected')
check(not Model.binding{kind='voice',id='x',extra=true},'unexpected binding fields rejected')
check(not Model.binding{kind='item',id='x\0y'},'control bytes rejected')
for basePage=1,16 do for pages=basePage,16 do
    check(Model.page(basePage,pages,false,false)==basePage,'release restores base page')
    check(Model.page(basePage,pages,true,false)==Model.wrap(basePage+1,pages),'held +1 wraps')
    check(Model.page(basePage,pages,true,true)==Model.wrap(basePage+2,pages),'held +2 takes precedence')
end end
check(not Model.chord({1,1},function() return true end),'a chord requires two distinct keys')
check(not Model.chord({1,2},function(k) return k==1 end),'both chord keys must be held')
check(Model.chord({1,2},function() return true end),'two held keys enable the chord')

-- Use the real /s resolver with a small catalogue, including mod fallback and
-- colliding IDs. Browse access and playback access must agree.
local now,played=10,{}
local messages={}
local p={guid=11,characterId=100,race='elf',isMale=true,gender=1,charGenComplete=true,health=100,name='Player'}
p.sendMessage=function(_,text) messages[#messages+1]=text end
local q={guid=12,characterId=200,race='elf',isMale=false,gender=0,charGenComplete=true,health=100,name='Other'}
q.sendMessage=p.sendMessage
local players={[11]=p,[12]=q}
local sections={}
local function section(name)
    sections[name]=sections[name] or {}
    return {getCopy=function(_,key) local value=sections[name][key];if value then return Model.state(value) end end,
        set=function(_,key,value) sections[name][key]=Model.state(value) end}
end
local events={}
package.loaded.mp={getUptime=function() return now end,getPlayer=function(id) return players[id] end,
    send=function(guid,event,data) events[#events+1]={guid=guid,event=event,data=data} end,
    playSpeech=function(guid,path) played[#played+1]={guid=guid,path=path};return true end,log=function() end,
    storage={globalSection=section}}
package.loaded.config={SPEECH_COOLDOWN_SECONDS=1}
package.loaded.speech_data={races={elf={default={male={hello={{'001','Vo/m/hello.mp3'}}},female={hello={{'001','Vo/f/hello.mp3'}}}},
    tb={male={hello={{'002','Vo/m/tb.mp3'}}}}}},global={misc={{'irrelevant','Vo/misc/a.mp3'}}}}
package.loaded.starwind_speech_data={races={elf={male={hello={{'001','Vo/mod/collision.mp3'},{'003','Vo/mod/new.mp3'}}}}}}
package.loaded.custom_speech_data={races={}}
local speech=load(root..'/apps/openmw-server/scripts/speech_commands.lua');package.loaded.speech_commands=speech
local list=speech.catalogue(p)
check(#list==4,'base expansion global and custom fallback lines are listed')
local ids={};for _,e in ipairs(list) do ids[e.id]=true end
check(ids['sound/vo/mod/new.mp3'] and not ids['sound/vo/mod/collision.mp3'],'catalogue honors /s duplicate precedence')
check(not ids['sound/vo/f/hello.mp3'],'other sex voice is excluded')
local female=speech.catalogue(q);check(#female==2,'female list includes only current voice and global lines')
local data=package.loaded.speech_data
local originalMisc=data.global.misc;data.global.misc={}
for i=1,170 do data.global.misc[i]={tostring(i),'Vo/misc/line'..i..'.mp3'} end
local actual,expected={},{}
for _,entry in ipairs(speech.catalogue(p)) do if entry.category=='misc' then actual[#actual+1]=entry.label end end
for i=1,170 do expected[i]='misc '..i end
check(table.concat(actual,',')==table.concat(expected,','),'numeric order across misc 2/7/17/20/68/70/165/170 boundaries')
data.global.misc=originalMisc
data.races.elf.default.male.idle={{'100','Vo/m/idle100.mp3'},{'20','Vo/m/idle20.mp3'},
    {'002','Vo/m/idle2.mp3'},{'010','Vo/m/idle10.mp3'},{'extra','Vo/m/idle-extra.mp3'}}
actual={};local categories={}
for _,entry in ipairs(speech.catalogue(p)) do
    if entry.category=='idle' then actual[#actual+1]=entry.label end
    categories[#categories+1]=entry.category
end
check(table.concat(actual,',')=='idle 002,idle 010,idle 20,idle 100,idle extra','padded IDs retain their labels; nonnumeric IDs follow numbers')
local grouped=true;for i=2,#categories do if categories[i]<categories[i-1] then grouped=false end end
check(grouped,'numeric sorting keeps voice categories grouped')
data.races.elf.default.male.idle=nil
local server=load(root..'/apps/openmw-server/scripts/hotkeys.lua').eventHandlers
local function request(pid,op,extra)
    local d={pid=pid,version=1,op=op};for k,v in pairs(extra or {}) do d[k]=v end
    server.Hotkeys_Request(d)
end
local function latest() return events[#events].data end
request(11,'load');local token=latest().token
check(events[#events].guid==11 and latest().state.pages==3,'state goes only to authenticated character')
request(12,'bind',{token=token,index=1,binding={kind='item',id='potion'}})
check(not sections.FetcherHotkeys['200'],'another player cannot reuse token')
request(11,'bind',{token=token,index=20,binding={kind='voice',id='sound/vo/m/hello.mp3'}})
check(latest().state.slots['20'].kind=='voice','available voice can be bound')
request(11,'bind',{token=token,index=1,binding={kind='voice',id='sound/vo/f/hello.mp3'}})
check(not sections.FetcherHotkeys['100'].slots['1'],'wrong race/sex binding rejected')
request(11,'bind',{token=token,index=161,binding={kind='item',id='x'}})
check(not sections.FetcherHotkeys['100'].slots['161'],'out-of-range slot rejected')
request(11,'pages',{token=token,pages=1});request(11,'pages',{token=token,pages=3})
check(latest().state.slots['20'],'server preserves assignments when reducing and increasing pages')
request(11,'play',{token=token,id='sound/vo/m/hello.mp3'})
check(#played==1 and played[1].guid==11,'voice hotkey uses authoritative speech playback')
request(11,'play',{token=token,id='sound/vo/m/hello.mp3'})
check(#played==1,'voice hotkeys share /s cooldown')
now=12;p.isMale=false;p.gender=0
request(11,'play',{token=token,id='sound/vo/m/hello.mp3'})
check(#played==1,'previously bound voice loses playback access after appearance change')
request(11,'play',{token=token,id='https://host/evil.mp3'})
check(#played==1,'arbitrary paths and URLs are never forwarded')
p.characterId=300;request(11,'bind',{token=token,index=1,binding={kind='item',id='x'}})
check(not sections.FetcherHotkeys['300'],'stale token cannot write another character')
p.characterId=100;p.isMale=true;p.gender=1;now=14
server.OnPlayerDisconnect{guid=11};request(11,'load');token=latest().token
check(latest().state.slots['20'],'bindings persist across disconnect')
request(11,'catalogue',{token=token})
check(latest().op=='catalogueEnd','catalogue has an explicit completion marker')
local old=#events
request(11,'catalogue',{token=token});check(#events==old,'catalogue requests are rate limited')
request(11,'bind',{token=token,index=20});check(not latest().state.slots['20'],'slot can be cleared')
request(11,'bind',{token=token,index=4,binding={kind='command',id='/admin'}})
check(not sections.FetcherHotkeys['100'].slots['4'],'server rejects arbitrary command actions')
local commands=load(root..'/apps/openmw-server/scripts/hotkeys_commands.lua')
check(commands.handleChat(p,{message='/voicelines'},{commandPrefix='/'})==false and latest().op=='voices','chat command opens the voice browser')
check(commands.handleChat(p,{message='!voicelines'},{commandPrefix='!'})==false and latest().op=='voices','custom command prefixes work')
check(commands.handleChat(p,{message='/hotbar'},{commandPrefix='/'})==nil,'redundant item-window command removed')
check(commands.handleChat(p,{message='/other'},{commandPrefix='/'})==nil,'unknown commands remain with their own handler')

-- Stub renderer and input: exercise actual GUI construction, hit testing,
-- drag release, scrollbar, settings registration and virtualized list updates.
local vector={};vector.__index=vector
vector.__add=function(a,b) return setmetatable({x=a.x+b.x,y=a.y+b.y},vector) end
vector.__sub=function(a,b) return setmetatable({x=a.x-b.x,y=a.y-b.y},vector) end
vector.__eq=function(a,b) return a.x==b.x and a.y==b.y end
function vector:length() return math.sqrt(self.x*self.x+self.y*self.y) end
local function v(x,y) return setmetatable({x=x,y=y},vector) end
local created,registered,removed={},0,0
local keys={Escape=41,PageUp=75,PageDown=78,LeftCtrl=224,LeftShift=225,LeftAlt=226,UpArrow=82,DownArrow=81}
for i=0,9 do keys['_'..i]=i+30 end
local ui={TYPE={Widget=1,Image=2,TextEdit=3,Flex=4},ALIGNMENT={Center=1},texture=function(t) return t end,content=function(t) return t end,
    create=function(layout)
        local e={layout=layout};e.update=function() end;e.destroy=function() e.destroyed=true end
        created[#created+1]=e;return e
    end,layers=setmetatable({indexOf=function(name) return name=='Windows' and 1 or 2 end,insertAfter=function() end},{__index=function() return {size=v(1440,900)} end}),
    registerSettingsPage=function(page) registered=registered+1;check(page.name=='Fetcher Hotkeys','settings page is discoverable in Scripts') end,
    removeSettingsPage=function() removed=removed+1 end,getMousePosition=function() return v(10,10) end}
local templates={textNormal={props={textColor={}}},textHeader={props={textColor={}}},borders={},bordersThick={},textEditLine={}}
package.loaded['openmw.ui']=ui
package.loaded['openmw.util']={vector2=v,color={rgb=function(...) return {...} end}}
package.loaded['openmw.async']={callback=function(_,f) return f end}
package.loaded['openmw.interfaces']={MWUI={templates=templates}}
package.loaded['openmw.input']={KEY=keys,getKeyName=function(k) return tostring(k) end}
local configStore={}
package.loaded['openmw.storage']={playerSection=function() return {getCopy=function() return nil end,set=function(_,k,val) configStore[k]=val end} end}
local prefix='scripts.multiplayer.fetcher.hotkeys.'
package.loaded[prefix..'shared.model']=Model
package.loaded[prefix..'client.voice-text']=VoiceText
package.loaded[prefix..'shared.voice-transcripts']=load(base..'shared/voice-transcripts.lua')
package.loaded[prefix..'client.style']=load(base..'client/style.lua')
local Settings=load(base..'client/settings.lua')
local pages=3
local options=Settings.new(function() end,function() return pages end,function(p) pages=p end)
check(registered==1 and options.config.up==keys.PageUp,'normal options settings registered with defaults')
options.recording='oneB';options:key(keys.LeftCtrl)
check(options.config.oneB==keys.LeftShift,'duplicate chord key cannot be configured')
options.recording='up';options:key(55);check(configStore.config.up==55,'key binding persists through settings storage')
options.recording='up';options:key(keys.Escape);check(options.config.up==55,'Escape cancels rebinding')
options:destroy();check(removed==1,'settings page removed when package unloads')
local View=load(base..'client/view.lua')
local entries={};for i=1,3000 do entries[i]={id='sound/vo/'..i..'.mp3',path='sound/vo/'..i..'.mp3',label='hello '..i} end
local bindings,drops,previews={},0,0
local view=View.new{config=function() return {scale=1,offset=18} end,page=function() return 1,3 end,visible=function() return true end,
    binding=function() end,describe=function(b) return b and {label=b.id} end,keyLabel=tostring,
    entries=function() return entries end,bind=function(index,b) bindings[#bindings+1]={index=index,binding=b} end,
    cycle=function() end,dropItem=function() drops=drops+1;return false end,preview=function() previews=previews+1 end,
    stopPreview=function() end,hint=function() end,openMode=function() return true end,closeMode=function() end}
view:drawBar();check(#view.rects==10,'ten visible slots')
view:open('voices');check(#view.rows<20 and #view.matches==3000,'voice list virtualizes thousands of entries')
local r=view.rects[3]
view.drag={kind='voice',id=entries[1].id};view:release(v(r.x+2,r.y+2))
check(#bindings==1 and bindings[1].index==3,'dragged voice binds the slot under release, even with mouse capture')
view.drag={kind='voice',id=entries[1].id};view:release(v(0,0));check(#bindings==1,'release outside hotbar cancels assignment')
view.overList=true;view:wheel(-1);view:frame(.2)
check(previews==1 and view.selected==2,'scrolling previews selected line with debounce')
view.query='hello 3000';view:entriesChanged();check(#view.matches==1,'search filters entire catalogue')
entries[3000].text='The moon is a strange place for an outlander.'
view.query='OUTLANDER';view:entriesChanged()
check(#view.matches==1 and view.matches[1].id==entries[3000].id,'spoken words are searched case-insensitively')
check(view.rows[1].layout.content[2].props.text==entries[3000].text,'actual words are shown beside the type and ID')
check(view.detail.layout.props.text==entries[3000].text,'selected full transcript is shown below the list')
view:setHint('preview');view:destroy();check(not view.bar and not view.browser,'HUD and browser cleaned up')

-- Drive the real player controller with its engine adapters stubbed. This
-- catches menu/chat leakage, duplicate key repeats and incorrect item use.
package.loaded[prefix..'client.settings']=Settings
package.loaded[prefix..'client.view']=View
local mode,textInput,connectedNow=nil,false,true
local pressed,outbound,used={}, {}, {}
local quickKeysEnabled=true
local item={recordId='potion',count=5,id='item-1',isValid=function() return true end}
item.type={record=function() return {name='Potion',icon='icons/potion.dds'} end}
local inventory={getAll=function() return {item} end,find=function(_,id) return id=='potion' and item or nil end}
local player={}
local interfaces=package.loaded['openmw.interfaces']
interfaces.UI={getMode=function() return mode end,isHudVisible=function() return true end,
    addMode=function(m) mode=m end,removeMode=function() mode=nil end}
package.loaded['openmw.self']=player
package.loaded['openmw.types']={Actor={inventory=function() return inventory end},NPC={record=function() return {race='elf',isMale=true} end},
    Player={CONTROL_SWITCH={Controls='playercontrols'},getControlSwitch=function(_,key) check(key=='playercontrols','uses native control switch key');return true end}}
package.loaded['openmw.core']={sendGlobalEvent=function(event,data) used[#used+1]={event=event,data=data} end}
local nativeInput=package.loaded['openmw.input']
nativeInput.isKeyPressed=function(key) return pressed[key]==true end
nativeInput.isTextInputActive=function() return textInput end
ui.setQuickKeysEnabled=function(value) quickKeysEnabled=value end
ui.getDraggedItem=function() return nil end
ui.cancelItemDrag=function() end
ui.showMessage=function() end
ui.screenSize=function() return v(1440,900) end
package.loaded['openmw.vfs']={fileExists=function() return true end}
package.loaded['openmw.ambient']={stopSoundFile=function() end,playSoundFile=function() end}
package.loaded['openmw.mp']={isConnected=function() return connectedNow end,
    sendToServer=function(event,data) outbound[#outbound+1]={event=event,data=data} end}
local controller=load(base..'client/player.lua')
local handlers=controller.engineHandlers;local api=controller.interface
handlers.onActive();handlers.onFrame(.01)
check(outbound[#outbound].data.op=='load','client requests authoritative state on connect')
local function deliver(state)
    controller.eventHandlers.Hotkeys_Message{version=1,op='state',token='character:100',state=state}
end
deliver{pages=3,base=1,slots={['1']={kind='item',id='potion'},['11']={kind='voice',id='sound/vo/test.mp3'},['21']={kind='item',id='potion'}}}
check(not quickKeysEnabled,'hotbar replaces vanilla quick keys without double activation')
local count=#outbound
textInput=true;handlers.onKeyPress{code=keys._1}
check(#used==0 and #outbound==count,'typing chat cannot trigger slots')
textInput=false;mode='MainMenu';handlers.onKeyPress{code=keys._1};check(#used==0,'options cannot trigger slots')
mode=nil;pressed[keys._1]=true;handlers.onKeyPress{code=keys._1}
check(#used==1 and used[1].event=='UseItem' and used[1].data.actor==player and item.count==5,'item hotkeys use normal item action without consuming assignment')
handlers.onKeyPress{code=keys._1};check(#used==1,'OS key repeat cannot repeat a slot action')
handlers.onKeyRelease{code=keys._1};pressed[keys._1]=nil
pressed[keys.LeftCtrl]=true;pressed[keys.LeftShift]=true
handlers.onKeyPress{code=keys._1};check(outbound[#outbound].data.op=='play','held +1 chooses second-page voice slot')
handlers.onKeyRelease{code=keys._1};pressed[keys.LeftAlt]=true
handlers.onKeyPress{code=keys._1};check(#used==2,'held +2 overrides +1 and selects third page')
handlers.onKeyRelease{code=keys._1};pressed={}
handlers.onKeyPress{code=keys._1};check(#used==3,'releasing chords returns to base page')
handlers.onKeyRelease{code=keys._1}
api.openVoices();check(mode=='Interface','browser captures cursor through normal UI mode')
check(api.openItems==nil,'removed item window is not exposed through the player interface')
api.close();check(mode==nil,'owned UI mode is restored on close')
mode='Interface';api.openVoices();api.close();check(mode=='Interface','closing browser preserves previously open inventory')
local activeBar
for _,element in ipairs(created) do if not element.destroyed and element.layout.layer=='FetcherHotbar' then activeBar=element end end
local cancelled
ui.getDraggedItem=function() return item end
ui.cancelItemDrag=function(expected) cancelled=expected end
check(api.acceptItemDrop(activeBar.layout.props.position+v(72,27)),'native inventory cursor still assigns directly to the HUD')
check(outbound[#outbound].data.op=='bind' and outbound[#outbound].data.binding.kind=='item' and cancelled==item and item.count==5,
    'inventory-to-HUD assignment cancels only the cursor and does not transfer or consume the stack')
ui.getDraggedItem=function() return nil end
mode=nil
check(api.setPageCount(16) and not api.setPageCount(17),'page-count API validates configured bounds')
connectedNow=false;handlers.onFrame(.01)
check(quickKeysEnabled,'disconnect restores vanilla quick keys')

local captionFallback={sheet={['sound/vo/test.mp3']={'Spreadsheet wording','Alternate phrasing'},['sound/vo/sheet.mp3']={'A sheet-only line'}},
    scripts={['sound/vo/script.mp3']='A script caption'},automatic={['sound/vo/auto.mp3']='Automatic words'}}
local records={}
for i=1,450 do records[i]={sound='Vo/test'..i..'.mp3',text='Installed line '..i} end
records[1]={sound='Sound\\Vo\\TEST.mp3',text='Native spoken text'}
records[2]={sound='other/test.mp3',text='Different directory, same basename'}
local captions=VoiceText.new({voice={records={{infos=records}}}},captionFallback)
captions:frame();check(not captions.done,'subtitle indexing yields instead of blocking on a large catalogue')
for i=1,4 do captions:frame() end
check(captions.done,'subtitle indexing completes incrementally')
local text,source,aliases=captions:lookup('Vo/TEST.mp3')
check(text=='Native spoken text' and source=='Game subtitles','installed subtitle overrides fallback spelling')
check(aliases:find('Alternate phrasing',1,true),'spreadsheet alternatives remain searchable')
check(captions:lookup('other/test.mp3')=='Different directory, same basename','full paths prevent basename collisions')
check(captions:lookup('Vo/sheet.mp3')=='A sheet-only line','spreadsheet supplies missing installed subtitles')
check(captions:lookup('Vo/script.mp3')=='A script caption','script captions cover scripted Say audio')
local automatic,automaticSource=captions:lookup('Vo/auto.mp3')
check(automatic=='Automatic words' and automaticSource:find('unverified',1,true),'machine transcription is identified honestly')
check(captions:lookup('Vo/missing.mp3')=='','missing words are never invented')
check(not VoiceText.path('https://host/audio.mp3') and not VoiceText.path('../vo/audio.mp3'),'external and traversing paths rejected')
check(VoiceText.clean('<font color="x">Hello<br>there</font>\n')=='Hello there','subtitle formatting is removed from searchable text')
check(VoiceText.display('#ABCDEFhello')=='##ABCDEFhello','subtitle text cannot inject MyGUI colors')
local decorated=captions:decorate{id='sound/vo/test.mp3',path='sound/vo/test.mp3',label='attack 001',category='attack'}
check(decorated.search:find('native spoken',1,true) and decorated.search:find('alternate phrasing',1,true),'search indexes actual words and fallback aliases')

print(string.format('Hotkeys: %d checks passed',checks))
