return function(mp,format)
local M={}
local function integer(n,a,b) return type(n)=='number' and n==n and n%1==0 and n>=a and n<=b end
local function summary(library)
    local out={};for _,entry in ipairs(library.entries) do out[#out+1]={id=entry.id,name=entry.name,revision=entry.revision,
        kind=entry.recipe.kind,span=entry.recipe.span,slices=entry.recipe.markers and #entry.recipe.markers+1 or 1} end;return out
end
function M.handle(character,data)
    assert(integer(data.requestId,1,2147483647),'Invalid sample-library request.')
    local stored=mp.getCharacterStorageForCharacter(character,'ProceduralSynth','samples')
    if stored==nil then stored={version=1,nextId=1,entries={}} end
    local library=assert(format.library(stored),'Unsupported sample library; existing data preserved.')
    local out={op='sampleResult',requestId=data.requestId,action=data.op};local selected,index
    for n,entry in ipairs(library.entries) do if entry.id==data.entryId then selected,index=entry,n;break end end
    local changed=false
    if data.op=='sampleOpen' then
        out.entry=assert(selected,'Sample no longer exists. Refresh the library.')
    elseif data.op=='sampleSave' then
        assert(integer(data.entryId,0,2147483647) and format.name(data.name),'Enter a sample name of 1-64 bytes.')
        local recipe=assert(format.recipe(data.recipe),'Invalid sample or slice-kit recipe.')
        if data.entryId==0 then
            assert(#library.entries<format.maxEntries and library.nextId<2147483647,'Sample library is full (64 entries).')
            selected={id=library.nextId,revision=0};library.nextId=library.nextId+1;library.entries[#library.entries+1]=selected
        else assert(selected and selected.revision==data.entryRevision,'Sample changed; reopen it or save a new copy.') end
        assert(selected.revision<2147483646,'Sample revision limit reached. Save a new copy.')
        selected.name=data.name;selected.recipe=recipe;selected.revision=selected.revision+1;out.entry=selected;changed=true
    elseif data.op=='sampleRename' or data.op=='sampleDelete' then
        assert(selected and selected.revision==data.entryRevision,'Sample changed; reopen it before renaming or deleting.')
        if data.op=='sampleRename' then
            assert(format.name(data.name),'Enter a sample name of 1-64 bytes.');assert(selected.revision<2147483646,'Sample revision limit reached.')
            selected.name=data.name;selected.revision=selected.revision+1;out.entry=selected
        else table.remove(library.entries,index);out.deletedId=selected.id end
        changed=true
    elseif data.op~='sampleList' then error('Unknown sample-library request.') end
    if changed then assert(mp.setCharacterStorageForCharacter(character,'ProceduralSynth','samples',library),'Sample storage failed; prior data preserved.') end
    out.entries=summary(library);return out
end
return M
end
