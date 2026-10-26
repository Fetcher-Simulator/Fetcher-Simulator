-- Dedicated-server authority. All data belongs to the authenticated character.
local mp=require('mp')
local speech=require('speech_commands')
local store=mp.storage.globalSection('FetcherHotkeys')
local sessions,rates={},{}
local serial=0
local function int(n,a,b) return type(n)=='number' and n==n and n%1==0 and n>=a and n<=b end
local function valid(b)
    if type(b)~='table' or (b.kind~='item' and b.kind~='voice') or type(b.id)~='string'
        or #b.id==0 or #b.id>512 or b.id:find('[%z\1-\31\127]') then return false end
    for key in pairs(b) do if key~='kind' and key~='id' then return false end end
    return true
end
local function state(p)
    local saved=store:getCopy(tostring(p.characterId))
    local result={version=1,pages=3,base=1,slots={}}
    if type(saved)~='table' then return result end
    if int(saved.pages,1,16) then result.pages=saved.pages end
    if int(saved.base,1,result.pages) then result.base=saved.base end
    for key,b in pairs(type(saved.slots)=='table' and saved.slots or {}) do
        if int(tonumber(key),1,160) and valid(b) then result.slots[tostring(tonumber(key))]={kind=b.kind,id=b.id:lower()} end
    end
    return result
end
local function send(p,data) data.version=1;mp.send(p.guid,'Hotkeys_Message',data) end
local function voices(p)
    local list=speech.catalogue(p);local byId={}
    for _,entry in ipairs(list) do byId[entry.id]=entry end
    return list,byId
end
local function request(d)
    if type(d)~='table' or d.version~=1 or type(d.pid)~='number' then return end
    local p=mp.getPlayer(d.pid)
    if not p or not p.charGenComplete or not int(p.characterId,1,9007199254740991) then return end
    local now=mp.getUptime();local r=rates[p.guid]
    if not r or now-r.start>=1 then r={start=now,count=0};rates[p.guid]=r end
    r.count=r.count+1;if r.count>24 then return end
    if d.op=='load' then
        if r.lastLoad and now-r.lastLoad<0.5 then return end
        r.lastLoad=now;serial=serial+1
        local token=tostring(p.characterId)..':'..tostring(serial)..':'..tostring(p.guid)
        sessions[p.guid]={character=p.characterId,token=token}
        send(p,{op='state',token=token,state=state(p)});return
    end
    local s=sessions[p.guid]
    if not s or s.character~=p.characterId or s.token~=d.token then return end
    if d.op=='catalogue' then
        if r.lastCatalogue and now-r.lastCatalogue<0.5 then return end
        r.lastCatalogue=now
        local list=voices(p)
        -- Bounded messages avoid a large catalogue becoming one oversized event.
        send(p,{op='catalogueBegin',token=s.token,race=p.race,isMale=p.isMale})
        for i=1,#list,32 do
            local batch={};for j=i,math.min(i+31,#list) do batch[#batch+1]=list[j] end
            send(p,{op='catalogueRows',token=s.token,rows=batch})
        end
        send(p,{op='catalogueEnd',token=s.token});return
    end
    if d.op=='play' then
        if p.health<=0 or type(d.id)~='string' or #d.id>512 then return end
        local _,byId=voices(p);local entry=byId[d.id]
        if entry then speech.playPath(p,entry.path,'hotkeys')
        else send(p,{op='error',text='That voice line is unavailable for your current race/sex. Reopen /voicelines.'}) end
        return
    end
    local saved=state(p)
    if d.op=='bind' then
        if not int(d.index,1,160) then return end
        local b=d.binding
        if b~=nil then
            if not valid(b) then return end
            if b.kind=='voice' then local _,byId=voices(p);if not byId[b.id] then return end end
            b={kind=b.kind,id=b.id:lower()}
        end
        saved.slots[tostring(d.index)]=b
    elseif d.op=='pages' then
        if not int(d.pages,1,16) then return end
        saved.pages=d.pages;saved.base=math.min(saved.base,d.pages)
    elseif d.op=='page' then
        if not int(d.page,1,saved.pages) then return end
        saved.base=d.page
    else return end
    store:set(tostring(p.characterId),saved)
    send(p,{op='state',token=s.token,state=saved})
end
return {eventHandlers={Hotkeys_Request=request,
    OnPlayerDisconnect=function(d) sessions[d.guid]=nil;rates[d.guid]=nil end}}
