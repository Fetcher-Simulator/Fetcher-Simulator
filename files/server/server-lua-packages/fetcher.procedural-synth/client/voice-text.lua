-- Resolve display/search text by complete VFS path, using the /voicelines resolver.
-- Metadata never grants file access; sample discovery and native VFS validation do.
local T={};T.__index=T
function T.path(value)
    if type(value)~='string' then return nil end
    local path=value:gsub('\\','/'):lower():gsub('^%s+',''):gsub('%s+$','')
    if path=='' or #path>512 or path:find(':',1,true) or path:find('[%z\1-\31\127]') then return nil end
    for part in path:gmatch('[^/]+') do if part=='.' or part=='..' then return nil end end
    if path:sub(1,1)=='/' or path:find('//',1,true) then return nil end
    return path:sub(1,6)=='sound/' and path or 'sound/'..path
end
function T.clean(value)
    if type(value)~='string' then return '' end
    return (value:gsub('<[Bb][Rr]%s*/?>',' '):gsub('</?[Ff][Oo][Nn][Tt][^>]*>','')
        :gsub('[%z\1-\31\127]',' '):gsub('%s+',' '):gsub('^%s+',''):gsub('%s+$','')):sub(1,4096)
end
function T.display(value) return (value or ''):gsub('#','##') end
local function add(list,text)
    text=T.clean(text);if text=='' then return false end
    for _,v in ipairs(list) do if v==text then return false end end
    list[#list+1]=text;return true
end
function T.new(dialogue,fallback)
    local self=setmetatable({fallback=fallback or {},native={},revision=0,done=false},T)
    self.scan=coroutine.create(function()
        local count=0
        for _,kind in ipairs{'voice','greeting','persuasion','topic'} do
            local group=dialogue and dialogue[kind]
            if group then for _,record in pairs(group.records) do for _,info in pairs(record.infos) do
                local path=T.path(info.sound)
                if path then
                    local values=self.native[path] or {}
                    if add(values,info.text) then self.native[path]=values;self.revision=self.revision+1 end
                end
                count=count+1;if count%200==0 then coroutine.yield() end
            end end end
        end
        self.done=true
    end)
    return self
end
function T:frame()
    if self.done then return false end
    local before=self.revision
    local ok,problem=coroutine.resume(self.scan)
    if not ok then self.done=true;print('[FetcherStudio] Subtitle scan stopped: '..tostring(problem)) end
    return before~=self.revision
end
function T:lookup(path)
    path=T.path(path);if not path then return '',nil,'' end
    local native=self.native[path] or {}
    local sheet=(self.fallback.sheet or {})[path] or {}
    local script=(self.fallback.scripts or {})[path]
    local automatic=(self.fallback.automatic or {})[path]
    local text,source
    if #native>0 then text,source=native[1],'Game subtitles'
    elseif #sheet>0 then text,source=table.concat(sheet,' / '),'Voice spreadsheet'
    elseif script then text,source=script,'Script subtitles'
    elseif automatic then text,source=automatic,'Automatic transcription (unverified)'
    else text='' end
    local alternatives={}
    for _,value in ipairs(native) do add(alternatives,value) end
    for _,value in ipairs(sheet) do add(alternatives,value) end
    if script then add(alternatives,script) end
    if automatic then add(alternatives,automatic) end
    return T.clean(text),source,table.concat(alternatives,' ')
end
function T:decorate(entry)
    local out={};for k,v in pairs(entry) do out[k]=v end
    local text,source,alternatives=self:lookup(entry.path or entry.id)
    out.text=text;out.textSource=source
    out.search=table.concat({entry.label or '',entry.category or '',entry.path or entry.id or '',text,alternatives},' '):lower()
    return out
end
return T
