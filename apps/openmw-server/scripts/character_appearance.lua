-- Dedicated-server authority for the automatically delivered appearance editor.
local mp=require('mp')
local config=require('config')
local sessions,rates={},{}
local serial=0
local function send(guid,data) data.version=1;mp.send(guid,'AppearanceEditor_Message',data) end
local C={}
C.bodySlots={{key='neck',label='Neck',part=2},{key='chest',label='Chest',part=3},
 {key='groin',label='Groin',part=4},{key='hands',label='Hands',part=5},{key='wrists',label='Wrists',part=6},
 {key='forearms',label='Forearms',part=7},{key='upperarms',label='Upper arms',part=8},
 {key='feet',label='Feet',part=9},{key='ankles',label='Ankles',part=10},{key='knees',label='Knees',part=11},
 {key='upperlegs',label='Upper legs',part=12},{key='clavicles',label='Clavicles',part=13},{key='tail',label='Tail',part=14}}
local allowedParts={};for _,slot in ipairs(C.bodySlots) do allowedParts[slot.key]=true end
local function validId(s,optional)
 return type(s)=='string' and #s<=256 and (optional or #s>0) and not s:find('[%z\1-\31\127]')
end
function C.validBody(value)
 if value.bodyRace~=nil and not validId(value.bodyRace,true) then return false end
 if value.rig~=nil and (type(value.rig)~='number' or value.rig%1~=0 or value.rig<0 or value.rig>2) then return false end
 if value.tail~=nil and type(value.tail)~='boolean' then return false end
 if value.parts~=nil then
  if type(value.parts)~='table' then return false end
  for key,part in pairs(value.parts) do if not allowedParts[key] or not validId(part,true) then return false end end
 end
 return true
end
function C.valid(value)
    if type(value)~='table' or type(value.isMale)~='boolean' then return false end
    local count=0
    for key in pairs(value) do
        if key~='race' and key~='head' and key~='hair' and key~='isMale' and key~='bodyRace' and key~='rig' and key~='tail' and key~='parts' then return false end
        count=count+1
    end
    if count<4 or count>8 or not C.validBody(value) then return false end
    for _,key in ipairs({'race','head','hair'}) do
        local s=value[key]
        if type(s)~='string' or #s>256 or (key~='hair' and #s==0) or s:find('[%z\1-\31\127]') then return false end
    end
    return true
end

local valid=C.valid
local function close(guid,reason)
    local s=sessions[guid];sessions[guid]=nil
    if s then send(guid,{op='closed',token=s.token,error=reason}) end
end
local function request(data)
    if type(data)~='table' or data.version~=1 or type(data.pid)~='number' then return end
    local p=mp.getPlayer(data.pid);if not p then return end
    local now=mp.getUptime()
    if data.op=='open' then
        if type(data.request)~='number' or data.request%1~=0 or data.request<1 or data.request>9007199254740991 then return end
        if rates[p.guid] and now-rates[p.guid]<0.5 then return end
        rates[p.guid]=now
        local error
        if config.CHARACTER_APPEARANCE_ENABLED==false then error='The appearance editor is disabled on this server.'
        elseif not p.charGenComplete or p.characterId<=0 then error='Finish character creation before editing appearance.'
        elseif type(p.setAppearance)~='function' then error='This server needs the appearance API update.'
        elseif p.health<=0 then error='You cannot edit appearance while dead.'
        elseif sessions[p.guid] and sessions[p.guid].pending then error='An appearance save is still in progress.' end
        if error then send(p.guid,{op='error',request=data.request,error=error});return end
        serial=serial+1
        local token=string.format('%d:%d:%d',p.guid,p.characterId,serial)
        sessions[p.guid]={token=token,character=p.characterId,expires=now+600}
        send(p.guid,{op='open',request=data.request,token=token,
            selection={race=p.race,head=p.head,hair=p.hair,isMale=p.isMale,
                bodyRace=p.appearance and p.appearance.bodyRace or p.race,rig=p.appearance and p.appearance.rig or 0,
                tail=not p.appearance or p.appearance.tail~=false,parts=p.appearance and p.appearance.parts or {}}})
        return
    end
    local s=sessions[p.guid]
    if not s or s.token~=data.token or s.character~=p.characterId then return end
    if now>s.expires then close(p.guid,'Appearance session expired; reopen the editor.');return end
    if data.op=='cancel' then if not s.pending then sessions[p.guid]=nil end;return end
    if data.op=='heartbeat' then s.expires=now+600;return end
    if data.op~='apply' or s.pending then return end
    if not valid(data.selection) then send(p.guid,{op='saved',token=s.token,ok=false,error='Invalid appearance selection.'});return end
    s.pending=s.token..':save';s.selection=data.selection
    local ok,queued=pcall(p.setAppearance,p,data.selection,s.pending)
    if not ok or not queued then
        s.pending=nil
        send(p.guid,{op='saved',token=s.token,ok=false,error='The server could not queue this appearance edit.'})
    end
end
return {eventHandlers={
    AppearanceEditor_Request=request,
    AppearanceEditorApplyResult=function(data)
        -- Native completion events have no client sender. Network clients
        -- cannot forge completion or acknowledge an edit for another player.
        if type(data)~='table' or data.pid~=nil then return end
        local s=sessions[data.guid]
        if not s or s.pending~=data.requestId or s.character~=data.characterId then return end
        s.pending=nil
        send(data.guid,{op='saved',token=s.token,ok=data.ok==true,error=data.error,selection=s.selection})
        if data.ok then sessions[data.guid]=nil end
    end,
    OnPlayerDisconnect=function(data) sessions[data.guid]=nil;rates[data.guid]=nil end,
    OnPlayerCellChange=function(data) close(data.guid,'Appearance editor closed after changing cells.') end,
}}
