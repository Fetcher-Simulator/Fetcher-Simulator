-- Private character documents. Each row replacement is one SQLite storage write.
return function(mp,format)
    local M={maxProjects=16,maxRecovery=3}
    local function int(n,a,b) return type(n)=='number' and n==n and n%1==0 and n>=a and n<=b end
    local function name(s) return type(s)=='string' and #s<=64 and s:find('%S') and not s:find('%c') end
    local function array(t,max)
        if type(t)~='table' or #t>max then return false end
        local count=0;for k in pairs(t) do if not int(k,1,max) then return false end;count=count+1 end
        return count==#t
    end
    local function get(id,key) return mp.getCharacterStorageForCharacter(id,'ProceduralSynth',key) end
    local function put(id,key,value) assert(mp.setCharacterStorageForCharacter(id,'ProceduralSynth',key,value),'Project storage failed; prior data preserved.') end
    local function library(id)
        local value=get(id,'library')
        if value==nil then
            value={version=1,nextId=1,entries={}}
            local old=get(id,'project')
            if old~=nil then
                local p=assert(format.project(old),'Unsupported legacy project; existing data preserved.')
                value.entries[1]={id=1,revision=1,project=p};value.nextId=2
                put(id,'library',value) -- Keep the original legacy row untouched.
            end
        end
        assert(type(value)=='table' and value.version==1 and int(value.nextId,1,2147483647) and array(value.entries,M.maxProjects),'Unsupported project library; existing data preserved.')
        local ids={}
        for _,e in ipairs(value.entries) do
            assert(type(e)=='table' and int(e.id,1,value.nextId-1) and not ids[e.id] and int(e.revision,1,2147483646),'Invalid project library; existing data preserved.')
            ids[e.id]=true;e.project=assert(format.project(e.project),'Unsupported library project; existing data preserved.')
        end
        return value
    end
    local function recovery(id)
        local value=get(id,'recovery');if value==nil then value={version=1,nextId=1,entries={}} end
        assert(type(value)=='table' and value.version==1 and int(value.nextId,1,2147483647) and array(value.entries,M.maxRecovery),'Unsupported recovery data; existing data preserved.')
        local ids,tokens={},{}
        for _,e in ipairs(value.entries) do
            assert(type(e)=='table' and int(e.id,1,value.nextId-1) and not ids[e.id] and type(e.token)=='string' and #e.token<=64 and not tokens[e.token]
                and int(e.projectId,0,2147483647) and int(e.generation,0,2147483647),'Invalid recovery data; existing data preserved.')
            ids[e.id]=true;tokens[e.token]=true;e.project=assert(format.project(e.project),'Unsupported recovery project; existing data preserved.')
        end
        return value
    end
    local function find(entries,id) for n,e in ipairs(entries) do if e.id==id then return e,n end end end
    local function summary(lib,rec)
        local out={entries={},recoveries={}}
        for _,e in ipairs(lib.entries) do out.entries[#out.entries+1]={id=e.id,name=e.project.name,revision=e.revision} end
        for n=#rec.entries,1,-1 do local e=rec.entries[n];out.recoveries[#out.recoveries+1]={id=e.id,name=e.project.name,projectId=e.projectId,generation=e.generation} end
        return out
    end
    function M.handle(character,data)
        assert(int(data.requestId,1,2147483647),'Invalid library request.')
        local op=data.op;local out={op='libResult',requestId=data.requestId,action=op}
        if op=='libSnapshot' then
            assert(type(data.token)=='string' and #data.token>0 and #data.token<=64 and int(data.saveRevision,0,2147483647) and int(data.projectId,0,2147483647),'Invalid recovery request.')
            local p=assert(format.project(data.project),'Invalid recovery project.')
            local rec=recovery(character);local previous,index
            for n,e in ipairs(rec.entries) do if e.token==data.token then previous,index=e,n;break end end
            if not previous or previous.generation<data.saveRevision then
                if index then table.remove(rec.entries,index) end
                assert(rec.nextId<2147483647,'Recovery ID limit reached.')
                rec.entries[#rec.entries+1]={id=rec.nextId,token=data.token,projectId=data.projectId,generation=data.saveRevision,project=p}
                rec.nextId=rec.nextId+1;if #rec.entries>M.maxRecovery then table.remove(rec.entries,1) end
                put(character,'recovery',rec)
            end
            out.saveRevision=data.saveRevision;return out
        end
        local lib=library(character);local rec=recovery(character)
        if op=='libOpen' then
            local e=assert(find(lib.entries,data.projectId),'Project no longer exists.')
            out.project=e.project;out.projectId=e.id;out.projectRevision=e.revision
        elseif op=='libSave' then
            local p=assert(format.project(data.project),'Invalid project.');assert(name(p.name),'Enter a project name of 1-64 bytes.')
            assert(int(data.projectId,0,2147483647) and int(data.saveRevision,0,2147483647),'Invalid project save.')
            local e
            if data.projectId==0 then
                assert(#lib.entries<M.maxProjects and lib.nextId<2147483647,'Project library is full (16 projects).')
                e={id=lib.nextId,revision=0};lib.nextId=lib.nextId+1;lib.entries[#lib.entries+1]=e
            else
                e=assert(find(lib.entries,data.projectId),'Project no longer exists.')
                assert(e.revision==data.projectRevision,'Project changed since loading; use Save As or reopen it.')
            end
            e.project=p;e.revision=e.revision+1;put(character,'library',lib)
            out.projectId=e.id;out.projectRevision=e.revision;out.saveRevision=data.saveRevision
        elseif op=='libRename' or op=='libDelete' then
            local e,index=find(lib.entries,data.projectId);assert(e,'Project no longer exists.')
            assert(e.revision==data.projectRevision,'Project changed; refresh the library first.')
            if op=='libRename' then assert(name(data.name),'Enter a project name of 1-64 bytes.');e.project.name=data.name;e.revision=e.revision+1;out.name=data.name;out.projectRevision=e.revision
            else table.remove(lib.entries,index) end
            out.projectId=e.id;put(character,'library',lib)
        elseif op=='libRecover' then
            local e=assert(find(rec.entries,data.recoveryId),'Recovery draft no longer exists.')
            out.project=e.project;out.projectId=0 -- Restore as an unsaved copy; never overwrite its named source.
        elseif op~='libList' then error('Unknown library request.') end
        local meta=summary(lib,rec);out.entries=meta.entries;out.recoveries=meta.recoveries
        return out
    end
    return M
end
