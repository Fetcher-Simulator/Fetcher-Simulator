local root=arg[1] or '.'
local base=root..'/files/server/server-lua-packages/fetcher.character-appearance/'
local function load(path) return assert(loadfile(path))() end
local checks=0
local function check(value,message) checks=checks+1;assert(value,message) end
local C=load(base..'shared/catalogue.lua')
local races={{id='modded_dragon',name='A mod race',isPlayable=true},{id='npc_only',name='Hidden',isPlayable=false},
    {id='elf',name='Elf',isPlayable=true}}
local parts={}
for i=1,2000 do parts[#parts+1]={id='dragon_head_'..i,race='modded_dragon',part=0,isFemale=false,
    isPlayable=true,isVampire=false,type='skin',model='meshes/custom/head'..i..'.nif'} end
local function part(id,race,kind,female,playable,vampire)
    parts[#parts+1]={id=id,race=race,part=kind,isFemale=female,isPlayable=playable,isVampire=vampire,type='skin',model='meshes/'..id..'.nif'}
end
part('female_head','modded_dragon',0,true,true,false)
part('male_hair','modded_dragon',1,false,true,false)
part('vampire','modded_dragon',0,false,true,true)
part('hidden_head','modded_dragon',0,false,false,false)
part('elf_head','elf',0,false,true,false)
local original={race='modded_dragon',head='dragon_head_1',hair='male_hair',isMale=true}
local catalogue=C.new(races,parts,{Head=0,Hair=1},original)
check(#catalogue.races==2,'only playable/current races listed')
local heads,hair=catalogue:choices('MODDED_DRAGON',true)
check(#heads==2000 and #hair==2,'all custom heads indexed without fixed cap; hair has bald option')
check(catalogue:contains(original),'original playable selection resolves')
check(not catalogue:contains({race=original.race,head='female_head',hair='',isMale=true}),'wrong sex rejected')
local female=catalogue:choose(original,original.race,false)
check(female.head=='female_head' and female.hair=='','race/sex changes select compatible parts')
local elf=catalogue:choose(original,'elf',true)
check(elf.head=='elf_head' and elf.hair=='','cross-race selections do not retain incompatible parts')
check(C.valid({race='elf',head='elf_head',hair='',isMale=true}),'bald is valid')
check(not C.valid({race='elf',head='elf_head',hair='',isMale=true,stats={}}),'extra mutation fields rejected')
check(not C.valid({race='elf',head='bad\0id',hair='',isMale=true}),'control characters rejected')
check(not C.valid({race=string.rep('x',257),head='elf_head',hair='',isMale=true}),'oversized IDs rejected')
local current=C.new(races,parts,{Head=0,Hair=1},{race='npc_only'})
check(#current.races==3,'current non-playable race retained')

-- Server controller: authority comes from the stamped sender, not payload IDs.
local now=10
local messages,queued={},{}
local players={}
for i=1,2 do
    players[i]={guid=i,characterId=i*100,race=original.race,head=original.head,hair=original.hair,
        isMale=true,charGenComplete=true,health=100}
    players[i].setAppearance=function(p,selection,request)
        queued[#queued+1]={guid=p.guid,character=p.characterId,selection=selection,request=request};return true
    end
end
package.loaded.mp={getUptime=function() return now end,getPlayer=function(guid) return players[guid] end,
    send=function(guid,event,data) messages[#messages+1]={guid=guid,event=event,data=data} end}
package.loaded.config={}
local server=load(root..'/apps/openmw-server/scripts/character_appearance.lua').eventHandlers
local function request(pid,op,token,selection,nonce)
    server.AppearanceEditor_Request({pid=pid,version=1,op=op,token=token,selection=selection,request=nonce or 1,guid=999})
end
request(1,'open');local token=messages[#messages].data.token
check(token and messages[#messages].guid==1,'open addressed only to authenticated sender')
check(messages[#messages].data.selection.head==original.head,'open returns authoritative current appearance')
local count=#messages;request(1,'open');check(#messages==count,'open requests rate limited')
request(2,'apply',token,elf);check(#queued==0,'another player cannot use a session token')
request(1,'apply',token,{race='elf',head='elf_head',hair='',isMale=true,stats=99})
check(#queued==0 and messages[#messages].data.ok==false,'malformed apply cannot reach native API')
request(1,'apply',token,elf)
check(#queued==1 and queued[1].guid==1 and queued[1].character==100,'save bound to authenticated character')
request(1,'apply',token,original);check(#queued==1,'duplicate apply suppressed while pending')
count=#messages
server.AppearanceEditorApplyResult({pid=2,guid=1,characterId=100,requestId=queued[1].request,ok=true})
check(#messages==count,'clients cannot forge native completion')
server.AppearanceEditorApplyResult({guid=1,characterId=200,requestId=queued[1].request,ok=true})
check(#messages==count,'completion for another character ignored')
server.AppearanceEditorApplyResult({guid=1,characterId=100,requestId=queued[1].request,ok=false,error='No matching head'})
check(messages[#messages].data.ok==false and messages[#messages].data.error=='No matching head','native validation failure reported without closing session')
request(1,'apply',token,elf);check(#queued==2,'rejected save can be corrected and retried')
server.AppearanceEditorApplyResult({guid=1,characterId=100,requestId=queued[2].request,ok=true})
check(messages[#messages].data.ok==true and messages[#messages].guid==1,'save acknowledged after native success')
request(1,'apply',token,elf);check(#queued==2,'completed session cannot be replayed')
now=12;request(1,'open');token=messages[#messages].data.token
request(1,'cancel',token);request(1,'apply',token,elf);check(#queued==2,'cancel never persists a draft')
now=14;request(1,'open');token=messages[#messages].data.token
players[1].characterId=101;request(1,'apply',token,elf);check(#queued==2,'changing character invalidates session')
players[1].characterId=100;server.OnPlayerDisconnect({guid=1})
request(1,'apply',token,elf);check(#queued==2,'disconnect clears pending session')
now=20;request(1,'open');token=messages[#messages].data.token;now=621
request(1,'apply',token,elf);check(#queued==2 and messages[#messages].data.op=='closed','expired session cannot apply')
players[1].charGenComplete=false;now=623;request(1,'open')
check(messages[#messages].data.op=='error','initial chargen cannot be edited concurrently')
players[1].charGenComplete=true;players[1].health=0;now=625;request(1,'open')
check(messages[#messages].data.op=='error','dead players cannot open editor')
players[1].health=100;now=627;request(1,'open');token=messages[#messages].data.token
server.OnPlayerCellChange({guid=1});request(1,'apply',token,elf)
check(#queued==2 and messages[#messages].data.op=='closed','cell change closes session')

-- Camera and virtualized UI use the real modules against a small engine stub.
local vector={};vector.__index=vector
vector.__add=function(a,b) return setmetatable({x=a.x+b.x,y=a.y+b.y,z=(a.z or 0)+(b.z or 0)},vector) end
vector.__sub=function(a,b) return setmetatable({x=a.x-b.x,y=a.y-b.y,z=(a.z or 0)-(b.z or 0)},vector) end
vector.__mul=function(a,n) return setmetatable({x=a.x*n,y=a.y*n,z=(a.z or 0)*n},vector) end
local function v(x,y,z) return setmetatable({x=x,y=y,z=z},vector) end
local uiCount=0
local ui={TYPE={Image='Image',Widget='Widget',Text='Text',TextEdit='TextEdit'},
    texture=function() return {} end,content=function(value) return value end,
    screenSize=function() return v(1920,1080) end,layers={[1]={size=v(1920,1080)},indexOf=function() return 1 end}}
function ui.create(layout)
    uiCount=uiCount+1
    return {layout=layout,update=function() end,destroy=function(self) self.destroyed=true end}
end
local mode
local I={UI={getMode=function() return mode end,addMode=function(value) mode=value end,removeMode=function() mode=nil end}}
local cameraMode='ThirdPerson';local yaw,pitch=.2,.3;local position=v(1,2,3)
local switches={look=true,view=true,vanity=false}
local previews={}
local player={position=v(0,0,0),rotation={getYaw=function() return 0 end}}
local types={Player={CONTROL_SWITCH={Looking='look',ViewMode='view',VanityMode='vanity'},
    getControlSwitch=function(p,key) check(p==player,'control switch targets self');return switches[key] end,
    setControlSwitch=function(p,key,value) check(p==player,'control mutation targets self');switches[key]=value end,
    previewAppearance=function(p,value) check(p==player,'preview targets self');previews[#previews+1]={value=value} end}}
package.loaded['openmw.ui']=ui
package.loaded['openmw.util']={vector2=v,vector3=v,color={rgb=function(...) return {...} end}}
package.loaded['openmw.async']={callback=function(_,fn) return fn end}
package.loaded['openmw.interfaces']=I;package.loaded['openmw.self']=player;package.loaded['openmw.types']=types
package.loaded['openmw.camera']={MODE={Static='Static',ThirdPerson='ThirdPerson'},
    getMode=function() return cameraMode end,getPosition=function() return position end,
    getYaw=function() return yaw end,getPitch=function() return pitch end,getTrackedPosition=function() return v(0,0,128) end,
    setMode=function(value) cameraMode=value end,setStaticPosition=function(value) position=value end,
    setYaw=function(value) yaw=value end,setPitch=function(value) pitch=value end}
local preview=load(base..'client/preview.lua')
package.loaded['scripts.multiplayer.fetcher.character-appearance.shared.catalogue']=C
package.loaded['scripts.multiplayer.fetcher.character-appearance.client.preview']=preview
local dropdown=load(base..'client/dropdown.lua')
package.loaded['scripts.multiplayer.fetcher.character-appearance.client.dropdown']=dropdown
local editorType=load(base..'client/editor.lua')
local editor=editorType.new(catalogue,original,function() end,function() end)
check(mode=='Interface' and switches.look==false and switches.view==false,'editor captures input without changing unrelated switches')
editor:frame();check(cameraMode=='Static' and math.abs(yaw-math.pi)<.0001,'camera faces editing player')
editor:choose(original.race,false)
check(previews[#previews].value.head=='female_head','compatible choice previews through native API')
local beforeWidgets=uiCount
dropdown.open(editor,heads,original.head,function() end,250)
check(#editor.dropdown.rows==8 and uiCount-beforeWidgets==12,'2000 choices create only eight reusable rows')
editor.dropdown.search.layout.events.textChanged('dragon_head_1999')
check(#editor.dropdown.matches==1 and editor.dropdown.rows[1].layout.props.visible,'search reaches late custom records')
editor.dropdown:wheel(-30);check(editor.dropdown.offset==0,'filtered scrolling stays in bounds')
editor:close()
check(mode==nil and cameraMode=='ThirdPerson' and yaw==.2 and pitch==.3,'closing restores original mode and camera angles')
check(switches.look==true and switches.view==true and switches.vanity==false,'closing restores each original control switch')
check(previews[#previews].value==nil,'cancel restores authoritative appearance baseline')
local countPreviews=#previews;editor:close();check(#previews==countPreviews,'closing twice is harmless')
local clientRequests,notices={},{}
ui.showMessage=function(text) notices[#notices+1]=text end
package.loaded['openmw.mp']={isConnected=function() return true end,
    sendToServer=function(event,data) clientRequests[#clientRequests+1]={event=event,data=data} end}
package.loaded['openmw.core']={}
package.loaded['openmw.input']={KEY={F6=6,Escape=27}}
types.NPC={races={records=races},isWerewolf=function() return false end}
types.BodyPart={records=parts,PART={Head=0,Hair=1}}
types.Actor={isOnGround=function() return true end}
types.Player.isInVehicle=function() return false end
package.loaded['scripts.multiplayer.fetcher.character-appearance.client.editor']=editorType
local controller=load(base..'client/player.lua')
controller.engineHandlers.onKeyPress({code=6})
check(clientRequests[#clientRequests].data.op=='open','F6 requests a server-owned session before opening')
controller.eventHandlers.AppearanceEditor_Message({version=1,op='open',request=1,token='client-token',selection=original})
check(controller.interface.state() and mode=='Interface','matching server response opens the side panel')
controller.interface.preview(female)
check(controller.interface.state().selection.head=='female_head','mod interface uses the same validated preview path')
controller.interface.close()
check(mode==nil and clientRequests[#clientRequests].data.op=='cancel','close cancels the session and restores UI')
controller.eventHandlers.AppearanceEditor_Message({version=1,op='open',request=1,token='late-token',selection=original})
check(controller.interface.state()==nil,'late open response cannot reopen a closed editor')
controller.engineHandlers.onKeyPress({code=6})
controller.eventHandlers.AppearanceEditor_Message({version=1,op='open',request=2,token='save-token',selection=original})
check(controller.interface.apply() and controller.interface.state().pending,'apply waits for native server completion')
controller.eventHandlers.AppearanceEditor_Message({version=1,op='saved',token='save-token',ok=false,error='Not permitted'})
check(not controller.interface.state().pending and mode=='Interface','save rejection keeps editing available')
controller.interface.preview(female);controller.interface.apply()
controller.eventHandlers.AppearanceEditor_Message({version=1,op='saved',token='save-token',ok=true})
check(controller.interface.state()==nil and mode==nil and notices[#notices]=='Appearance saved to your character.','successful save closes and restores camera')
controller.engineHandlers.onKeyPress({code=6})
local create=ui.create
ui.create=function(layout) if layout.layer then error('simulated layout failure') end;return create(layout) end
controller.eventHandlers.AppearanceEditor_Message({version=1,op='open',request=3,token='failure-token',selection=original})
ui.create=create
check(controller.interface.state()==nil and mode==nil and switches.look==true and cameraMode=='ThirdPerson','layout failure restores controls and camera')
print(string.format('Character appearance: %d checks passed',checks))
