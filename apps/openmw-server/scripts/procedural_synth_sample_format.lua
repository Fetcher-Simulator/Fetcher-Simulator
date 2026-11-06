-- Versioned references and editing recipes, never audio data or filesystem URLs.
return function(schema)
local M={maxEntries=64}
local function number(v,a,b) return type(v)=='number' and v==v and v>=a and v<=b end
local function integer(v,a,b) return number(v,a,b) and v%1==0 end
local function keys(t,allowed)
    if type(t)~='table' then return false end
    for k in pairs(t) do if not allowed[k] then return false end end;return true
end
local function array(t,maximum)
    if type(t)~='table' or #t>maximum then return false end
    local count=0;for k in pairs(t) do if not integer(k,1,maximum) then return false end;count=count+1 end
    return count==#t
end
function M.name(value) return type(value)=='string' and #value<=64 and value:find('%S')~=nil and not value:find('%c') end
function M.recipe(value)
    if not keys(value,{version=true,kind=true,source=true,span=true,markers=true,settings=true}) or value.version~=1
        or value.kind~='sample' and value.kind~='kit' or not number(value.span,.01,30) then return nil end
    local source=schema.sample(value.source)
    if not source or not source.hash or schema.isBank(source) or value.span>source.duration+1e-7 then return nil end
    local span=math.floor(value.span*1000+.5)/1000
    if source.loop and source.loop.finish>span+1e-7 then return nil end
    local out={version=1,kind=value.kind,source=source,span=span}
    if value.kind=='sample' then if value.markers~=nil or value.settings~=nil then return nil end;return out end
    if not array(value.markers,15) or #value.markers<1 then return nil end
    out.markers={};local previous=0
    for n,point in ipairs(value.markers) do
        if not number(point,.01,span-.01) then return nil end
        point=math.floor(point*1000+.5)/1000
        if point-previous<.01-1e-7 or point>span-.01+1e-7 then return nil end
        out.markers[n]=point;previous=point
    end
    local settings=value.settings or {sensitivity=.5,spacing=.08,snap=true}
    if not keys(settings,{sensitivity=true,spacing=true,snap=true}) or not number(settings.sensitivity,0,1)
        or not number(settings.spacing,.01,2) or type(settings.snap)~='boolean' then return nil end
    out.settings={sensitivity=settings.sensitivity,spacing=settings.spacing,snap=settings.snap};return out
end
function M.library(value)
    if not keys(value,{version=true,nextId=true,entries=true}) or value.version~=1 or not integer(value.nextId,1,2147483647)
        or not array(value.entries,M.maxEntries) then return nil end
    local out={version=1,nextId=value.nextId,entries={}};local ids={}
    for _,entry in ipairs(value.entries) do
        if not keys(entry,{id=true,revision=true,name=true,recipe=true}) or not integer(entry.id,1,value.nextId-1)
            or ids[entry.id] or not integer(entry.revision,1,2147483646) or not M.name(entry.name) then return nil end
        local recipe=M.recipe(entry.recipe);if not recipe then return nil end
        ids[entry.id]=true;out.entries[#out.entries+1]={id=entry.id,revision=entry.revision,name=entry.name,recipe=recipe}
    end
    return out
end
return M
end
