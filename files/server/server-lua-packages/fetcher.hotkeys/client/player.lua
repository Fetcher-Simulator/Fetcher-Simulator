local mp=require('openmw.mp')
local self=require('openmw.self')
local types=require('openmw.types')
local core=require('openmw.core')
local input=require('openmw.input')
local ui=require('openmw.ui')
local vfs=require('openmw.vfs')
local ambient=require('openmw.ambient')
local I=require('openmw.interfaces')
local Model=require('scripts.multiplayer.fetcher.hotkeys.shared.model')
local View=require('scripts.multiplayer.fetcher.hotkeys.client.view')
local Settings=require('scripts.multiplayer.fetcher.hotkeys.client.settings')
local VoiceText=require('scripts.multiplayer.fetcher.hotkeys.client.voice-text')
local fallback=require('scripts.multiplayer.fetcher.hotkeys.shared.voice-transcripts')
local texts
local state=Model.state()
local settings,view,token,previewPath,queuedOpen
local catalogue,voiceIndex,itemIndex={},{},{}
local connected=false
local inventorySignature
local clock,lastLoad,nextInventory,nextIdentity=0,-10,0,0
local textRevision,nextTextRefresh=-1,0
local held,oldPage,lastVisible,lastSize,identity={},nil,nil,nil,nil
local function send(op,extra)
    if not mp.isConnected() then return end
    local d={version=1,op=op,token=token}
    for key,value in pairs(extra or {}) do d[key]=value end
    mp.sendToServer('Hotkeys_Request',d)
end
local function stopPreview()
    if previewPath then ambient.stopSoundFile(previewPath);previewPath=nil end
end
local function preview(entry)
    stopPreview()
    if entry and vfs.fileExists(entry.path) then
        previewPath=entry.path;ambient.playSoundFile(previewPath,{scale=false,volume=1})
        return true
    end
    return false
end
local function activePage()
    local c=settings.config
    local mode=I.UI.getMode()
    local allowed=(mode==nil or mode=='Interface') and not input.isTextInputActive()
    return Model.page(state.base,state.pages,allowed and Model.chord({c.oneA,c.oneB},input.isKeyPressed),
        allowed and Model.chord({c.twoA,c.twoB},input.isKeyPressed)),state.pages
end
local function modeAllowed() local mode=I.UI.getMode();return mode==nil or mode=='Interface' end
local function visible() return settings.config.enabled and token~=nil and modeAllowed() and I.UI.isHudVisible() end
local function redraw() if view then view:drawBar() end end
local function bind(index,binding)
    if not token or not Model.integer(index,1,160) then return false end
    local b=binding and Model.binding(binding) or nil
    if binding and not b then return false end
    if b and b.kind=='voice' and not voiceIndex[b.id] then return false end
    send('bind',{index=index,binding=b});return true
end
local function cycle(delta)
    if not token then return end
    state.base=Model.wrap(state.base+delta,state.pages)
    send('page',{page=state.base});redraw()
end
local function refreshInventory()
    itemIndex={}
    for _,item in ipairs(types.Actor.inventory(self):getAll()) do
        if item.count>0 then
            local id=item.recordId:lower();local info=itemIndex[id]
            if info then info.count=info.count+item.count
            else
                local record=item.type.record(item)
                local icon=record.icon
                if icon and icon~='' and not vfs.fileExists(icon) then icon='icons/'..icon end
                info={id=id,label=record.name or id,count=item.count,icon=icon and vfs.fileExists(icon) and icon or nil}
                itemIndex[id]=info
            end
        end
    end
    local parts={}
    for _,info in pairs(itemIndex) do parts[#parts+1]=info.id..':'..info.count end
    table.sort(parts)
    local signature=table.concat(parts,'|');local changed=signature~=inventorySignature
    inventorySignature=signature;return changed
end
local function owned(item)
    if not item or not item:isValid() or item.count<=0 then return false end
    for _,candidate in ipairs(types.Actor.inventory(self):getAll()) do if candidate==item then return true end end
    return false
end
local function dropItem(index)
    if I.UI.getMode()~='Interface' then return false end
    local item=ui.getDraggedItem()
    if item and owned(item) then
        if bind(index,{kind='item',id=item.recordId}) then ui.cancelItemDrag(item);return true end
    end
    -- Inventory Extender keeps a separate Lua cursor. This is optional; the
    -- package works with the vanilla inventory and never transfers a stack.
    local ie=I.InventoryExtender
    local context=ie and ie.getContext and ie.getContext()
    local drag=context and context.dragAndDrop
    item=drag and drag.draggingObject
    if item and owned(item) and bind(index,{kind='item',id=item.recordId}) then drag:stopDrag();return true end
    return false
end
local function activate(slot)
    if not token or not settings.config.enabled or I.UI.getMode()~=nil or input.isTextInputActive()
        or not types.Player.getControlSwitch(self,types.Player.CONTROL_SWITCH.Controls) then return false end
    local page=activePage();local b=state.slots[tostring(Model.index(page,slot))]
    if not b then return false end
    if b.kind=='voice' then send('play',{id=b.id});return true end
    local item=types.Actor.inventory(self):find(b.id)
    if item and item.count>0 then
        core.sendGlobalEvent('UseItem',{object=item,actor=self});return true
    end
    ui.showMessage('You no longer have that hotbar item.');return false
end
local function open()
    if not view or not modeAllowed() then return end
    if view.mode then view:close();return end
    if not token then queuedOpen=true;send('load');return end
    send('catalogue');view:open();redraw()
end
local function init()
    if settings then return end
    texts=VoiceText.new(core.dialogue,fallback);textRevision=-1
    settings=Settings.new(function()
        ui.setQuickKeysEnabled(not (settings.config.enabled and token~=nil));redraw()
    end,function() return state.pages end,function(pages) if token then send('pages',{pages=pages}) end end)
    view=View.new{
        config=function() return settings.config end,page=activePage,visible=visible,
        binding=function(index) return state.slots[tostring(index)] end,
        keyLabel=function(slot) return input.getKeyName(settings.config['slot'..slot]) end,
        describe=function(b)
            if not b then return nil end
            if b.kind=='voice' then return voiceIndex[b.id] or {label='Unavailable voice: '..b.id} end
            return itemIndex[b.id] or {label=b.id,count=0}
        end,
        entries=function() return catalogue end,
        bind=bind,cycle=cycle,dropItem=dropItem,preview=preview,stopPreview=stopPreview,
        hint=function(text) view:setHint(text) end,
        openMode=function() if I.UI.getMode()==nil then I.UI.addMode('Interface',{windows={}});return true end;return false end,
        closeMode=function() if I.UI.getMode()=='Interface' then I.UI.removeMode('Interface') end end,
    }
end
local function clear()
    stopPreview();if view then view:destroy();view=nil end
    if settings then settings:destroy();settings=nil end
    token=nil;connected=false;catalogue={};voiceIndex={};itemIndex={};held={};queuedOpen=nil;identity=nil;texts=nil
    ui.setQuickKeysEnabled(true)
end
local function message(d)
    if type(d)~='table' or d.version~=1 then return end
    init()
    if d.op=='voices' then open();return end
    if d.op=='state' and type(d.token)=='string' then
        local first=token~=d.token
        token=d.token;state=Model.state(d.state)
        ui.setQuickKeysEnabled(not settings.config.enabled);settings:draw();redraw()
        if first then send('catalogue') end
        if queuedOpen then queuedOpen=nil;open() end
    elseif d.op=='error' then ui.showMessage(d.text or 'Hotkey request failed.')
    elseif d.token==token and token then
        if d.op=='catalogueBegin' then catalogue={};voiceIndex={};stopPreview()
        elseif d.op=='catalogueRows' then
            for _,entry in ipairs(d.rows or {}) do
                if type(entry.id)=='string' and type(entry.path)=='string' and vfs.fileExists(entry.path) and not voiceIndex[entry.id] then
                    local captioned=texts:decorate(entry)
                    catalogue[#catalogue+1]=captioned;voiceIndex[entry.id]=captioned
                end
            end
        elseif d.op=='catalogueEnd' then view:entriesChanged();redraw() end
    end
end
return {interfaceName='FetcherHotkeys',interface={
    openVoices=open,
    close=function() if view then view:close() end end,
    previewVoice=function(id) return preview(voiceIndex[id]) end,stopPreview=stopPreview,
    state=function() return Model.state(state) end,
    bind=bind,activate=activate,cycle=cycle,
    setPageCount=function(pages)
        if not token or not Model.integer(pages,1,16) then return false end
        send('pages',{pages=pages});return true
    end,
    acceptItemDrop=function(position)
        local index=view and view:slotAt(position)
        return index and dropItem(index) or false
    end,
    catalogue=function() local out={};for _,e in ipairs(catalogue) do out[#out+1]={id=e.id,label=e.label,path=e.path,text=e.text,textSource=e.textSource} end;return out end,
},eventHandlers={Hotkeys_Message=message},engineHandlers={
    onActive=init,onInactive=clear,onLoad=clear,
    onKeyPress=function(e)
        if not settings then return end
        if settings.recording and I.UI.getMode()~='MainMenu' then settings.recording=nil;settings:draw() end
        if settings:key(e.code) then return end
        if e.code==input.KEY.Escape and view.mode then view:close();return end
        if not modeAllowed() or input.isTextInputActive() or not settings.config.enabled then return end
        if held[e.code] then return end;held[e.code]=true
        if e.code==settings.config.up then cycle(1);return elseif e.code==settings.config.down then cycle(-1);return end
        if I.UI.getMode()~=nil then
            if view.mode=='voices' and (e.code==input.KEY.UpArrow or e.code==input.KEY.DownArrow) then
                view:select(view.selected+(e.code==input.KEY.UpArrow and -1 or 1),true)
            end
            return
        end
        for slot=1,10 do if e.code==settings.config['slot'..slot] then activate(slot);return end end
    end,
    onKeyRelease=function(e) held[e.code]=nil end,
    onMouseWheel=function(delta) if view then view:wheel(delta) end end,
    onMouseButtonRelease=function(button) if view and button==1 then view:release(ui.getMousePosition()) end end,
    onFrame=function(dt)
        clock=clock+dt
        if not mp.isConnected() then if connected then clear() end;return end
        init()
        texts:frame()
        if textRevision~=texts.revision and clock>=nextTextRefresh then
            textRevision=texts.revision;nextTextRefresh=clock+.25
            for index,entry in ipairs(catalogue) do
                local updated=texts:decorate(entry);catalogue[index]=updated;voiceIndex[entry.id]=updated
            end
            view:entriesChanged()
        end
        if settings.recording and I.UI.getMode()~='MainMenu' then settings.recording=nil;settings:draw() end
        if not connected then connected=true;token=nil;lastLoad=-10 end
        if not token and clock-lastLoad>3 then lastLoad=clock;send('load') end
        for key in pairs(held) do if not input.isKeyPressed(key) then held[key]=nil end end
        if clock>=nextInventory then
            nextInventory=clock+.5
            if refreshInventory() then redraw() end
        end
        if clock>=nextIdentity then
            nextIdentity=clock+1
            local record=types.NPC.record(self);local current=tostring(record.race)..':'..tostring(record.isMale)
            if identity and identity~=current then catalogue={};voiceIndex={};send('catalogue') end
            identity=current
        end
        local page=activePage();local shown=visible();local screen=ui.screenSize()
        if page~=oldPage or shown~=lastVisible or screen~=lastSize then oldPage=page;lastVisible=shown;lastSize=screen;redraw() end
        if view.mode and I.UI.getMode()~='Interface' then view:close() end
        view:frame(dt)
    end,
}}
