local mp=require('openmw.mp')
local types=require('openmw.types')
local player=require('openmw.self')
local input=require('openmw.input')
local ui=require('openmw.ui')
local I=require('openmw.interfaces')
local Catalogue=require('scripts.multiplayer.fetcher.character-appearance.shared.catalogue')
local Editor=require('scripts.multiplayer.fetcher.character-appearance.client.editor')
local editor,token,opening
local serial,heartbeat,clock=0,0,0
local function send(op,extra)
    local data={version=1,op=op,token=token}
    for key,value in pairs(extra or {}) do data[key]=value end
    mp.sendToServer('AppearanceEditor_Request',data)
end
local function close(notify)
    if notify and token and mp.isConnected() then send('cancel') end
    opening=nil;token=nil
    if editor then local old=editor;editor=nil;old:close() end
end
local function open()
    if editor then if not editor.pending then close(true) end;return end
    if opening then return end
    if not mp.isConnected() or I.UI.getMode()~=nil then return end
    if types.NPC.isWerewolf(player) or types.Player.isInVehicle(player) or not types.Actor.isOnGround(player) then
        ui.showMessage('Stand on solid ground, outside a vehicle or werewolf form, to edit appearance.');return
    end
    serial=serial+1;opening=serial;send('open',{request=opening});heartbeat=clock
end
local function message(data)
    if type(data)~='table' or data.version~=1 then return end
    if data.op=='toggle' then open();return end
    if data.op=='open' then
        if data.request~=opening or editor or not Catalogue.valid(data.selection) then return end
        if I.UI.getMode()~=nil then token=data.token;close(true);return end
        opening=nil;token=data.token
        local ok,result=pcall(function()
            local catalogue=Catalogue.new(types.NPC.races.records,types.BodyPart.records,types.BodyPart.PART,data.selection)
            return Editor.new(catalogue,data.selection,function(selection) send('apply',{selection=selection}) end,
                function() close(true) end)
        end)
        if not ok then close(true);ui.showMessage('Appearance editor: '..tostring(result));return end
        editor=result;heartbeat=clock
    elseif data.op=='error' and data.request==opening then
        opening=nil;ui.showMessage(data.error or 'The server could not open the appearance editor.')
    elseif data.token==token and token then
        if data.op=='closed' then close(false);if data.error then ui.showMessage(data.error) end
        elseif data.op=='saved' and editor then
            if data.ok then close(false);ui.showMessage('Appearance saved to your character.')
            else editor:result(data) end
        end
    end
end
return {interfaceName='FetcherAppearance',interface={
    open=open,
    close=function() if not editor or not editor.pending then close(true) end end,
    state=function() return editor and {selection=Catalogue.copy(editor.selection),pending=editor.pending==true,
        allRaces=editor.catalogue.allRaces,page=editor.page} or nil end,
    page=function(name) if editor and (name=='character' or name=='body') then editor:pageTo(name);return true end;return false end,
    setAllRaces=function(enabled)
        if not editor or editor.pending or type(enabled)~='boolean' then return false end
        if editor.catalogue.allRaces~=enabled then return editor:toggleParts() end
        return true
    end,
    preview=function(selection)
        if not editor or editor.pending or not editor.catalogue:contains(selection) then return false end
        editor.selection=Catalogue.copy(selection);editor:preview();return true
    end,
    apply=function() return editor and editor:submit() or false end,
},eventHandlers={AppearanceEditor_Message=message},engineHandlers={
    onKeyPress=function(event)
        if event.code==input.KEY.Escape and editor and not editor.pending then close(true) end
    end,
    onMouseWheel=function(delta) if editor then editor:wheel(delta) end end,
    onMouseButtonRelease=function(button) if button==1 and editor then editor:dragEnd() end end,
    onFrame=function(dt)
        clock=clock+dt
        if opening and clock-heartbeat>10 then opening=nil;ui.showMessage('Appearance editor did not respond. Check that its server script is installed.') end
        if not editor then return end
        if not mp.isConnected() or I.UI.getMode()~='Interface' or types.NPC.isWerewolf(player) then close(true);return end
        editor:frame()
        if clock-heartbeat>20 then send('heartbeat');heartbeat=clock end
    end,
    onInactive=function() close(false) end,
    onLoad=function() close(false) end,
}}
