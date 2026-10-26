local M={MAX_PAGES=16,SLOTS=10}
function M.integer(n,min,max) return type(n)=='number' and n==n and n%1==0 and n>=min and n<=max end
function M.text(s,max) return type(s)=='string' and #s>0 and #s<=max and not s:find('[%z\1-\31\127]') end
function M.binding(b)
    if type(b)~='table' then return nil end
    for k in pairs(b) do if k~='kind' and k~='id' then return nil end end
    if (b.kind~='item' and b.kind~='voice') or not M.text(b.id,512) then return nil end
    return {kind=b.kind,id=b.id:lower()}
end
function M.state(value)
    local result={version=1,pages=3,base=1,slots={}}
    if type(value)~='table' then return result end
    if M.integer(value.pages,1,M.MAX_PAGES) then result.pages=value.pages end
    if M.integer(value.base,1,result.pages) then result.base=value.base end
    for key,binding in pairs(type(value.slots)=='table' and value.slots or {}) do
        local n=tonumber(key)
        if M.integer(n,1,M.SLOTS*M.MAX_PAGES) then result.slots[tostring(n)]=M.binding(binding) end
    end
    return result
end
function M.wrap(page,pages) return (page-1)%pages+1 end
function M.page(base,pages,one,two) return M.wrap(base+(two and 2 or one and 1 or 0),pages) end
function M.index(page,slot) return (page-1)*M.SLOTS+slot end
function M.chord(keys,pressed)
    return type(keys)=='table' and M.integer(keys[1],1,512) and M.integer(keys[2],1,512)
        and keys[1]~=keys[2] and pressed(keys[1]) and pressed(keys[2]) or false
end
return M
