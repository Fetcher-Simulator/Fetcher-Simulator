-- Bounded, session-owned stage jobs. Only the server chooses actor positions.
local Layout=require('procedural_synth_band_layout')
return function(mp,format,performers,emit)
local S={jobs={},nextAction=0}
local function owns(registry,target)
    for _,e in ipairs(registry.entries) do if e.mpNum==target.mpNum and e.refId==target.refId then return true end end
    return false
end
function S:finish(job,error)
    self.jobs[job.character]=nil
    emit(job.owner,{op='bandSetup',id=job.id,epoch=job.epoch,requestId=job.requestId,
        targets=not error and job.targets or nil,restage=job.restage,error=error})
    local s=job.session;if s and not error then performers:roster(s) end
end
function S:begin(s,project,requestId,sessions,restage)
    local function reject(message)
        emit(s.owner,{op='bandSetup',id=s.id,epoch=s.epoch,requestId=requestId,restage=restage,error=message});return false
    end
    if self.jobs[s.characterId] then return reject('Your NPC stage setup is still running.') end
    local leader=mp.getPlayer(s.owner);local registry=performers:registry(s.characterId)
    if not leader or not registry or not mp.teleportActorByInstanceId then
        return reject('NPC stage setup needs the matching server and an available roster.')
    end
    local anchor={x=leader.position.x,y=leader.position.y,z=leader.position.z,rz=leader.position.rz or 0}
    local plan=restage and {} or Layout.plan(project)
    if restage then
        local seen={};for _,m in ipairs(s.band) do local t=m.target
            if t and t.kind=='npc' and owns(registry,t) and not seen[t.mpNum] then
                plan[#plan+1]={target=t};seen[t.mpNum]=true
            end
        end
    end
    if #plan==0 or #plan>format.maxMembers or not Layout.position(anchor,1,#plan) then
        return reject(restage and 'Bind owned NPC performers before staging.' or 'Create or import song parts first.')
    end
    local busy={};for id,other in pairs(sessions) do if id~=s.id then
        for _,m in ipairs(other.band) do if m.target and m.target.kind=='npc' then busy[m.target.mpNum]=true end end
    end end
    local pool={};for _,e in ipairs(registry.entries) do local actor=e.mpNum and mp.getActorByMpNum(e.mpNum)
        if actor and actor.refId==e.refId and not actor.isDead and not busy[e.mpNum] then pool[#pool+1]=e end
    end
    if restage then for _,part in ipairs(plan) do if busy[part.target.mpNum] then
        return reject('Release the performer from the other active band before restaging.')
    end end end
    if not restage and #plan>#pool+format.maxMembers-#registry.entries then
        return reject('Not enough available owned performers. Dismiss missing/dead NPCs or release their other band first.')
    end
    self.jobs[s.characterId]={id=s.id,epoch=s.epoch,owner=s.owner,character=s.characterId,requestId=requestId,session=s,
        anchor=anchor,cell=s.cell,plan=plan,pool=pool,targets={},used={},cursor=1,restage=restage,expires=mp.getUptime()+45}
    return true
end
function S:tick(sessions)
    local now=mp.getUptime();if now<self.nextAction then return end
    for _,job in pairs(self.jobs) do
        local s=sessions[job.id]
        if not s or s.epoch~=job.epoch or not mp.getPlayer(job.owner) then self.jobs[job.character]=nil
        elseif now>job.expires then self:finish(job,'NPC setup timed out. Existing hires remain yours; retry to reuse them.')
        elseif not job.waiting then
            if job.cursor>#job.plan then self:finish(job);return end
            self.nextAction=now+.1
            local index=job.cursor;local part=job.plan[index];local entry
            if part.target then entry=part.target else
                -- Prefer this song's current NPC, then reuse the remaining pool.
                local old=s.band[index] and s.band[index].target
                for _,e in ipairs(job.pool) do if not job.used[e.mpNum] and old and e.mpNum==old.mpNum and e.refId==old.refId then entry=e;break end end
                if not entry then for _,e in ipairs(job.pool) do if not job.used[e.mpNum] then entry=e;break end end end
            end
            local position=Layout.position(job.anchor,index,#job.plan)
            if entry then
                position.isTeleporting=true
                local actor=mp.getActorByMpNum(entry.mpNum)
                if not actor or actor.refId~=entry.refId or actor.isDead or not actor.actorInstanceId
                    or not mp.teleportActorByInstanceId(actor.actorInstanceId,job.cell,position) then
                    self:finish(job,'An owned performer could not be staged. Refresh the roster and retry.');return
                end
                job.used[entry.mpNum]=true
                job.targets[index]={kind='npc',mpNum=entry.mpNum,refId=entry.refId,instrument=part.instrument or entry.instrument}
                job.cursor=index+1
            else
                job.waiting=true
                local ok=performers:hire(s,index,{instrument=part.instrument,position=position,complete=function(target)
                    if self.jobs[job.character]~=job then return end
                    job.targets[index]=target;job.used[target.mpNum]=true;job.waiting=nil;job.cursor=index+1
                end})
                if not ok then self:finish(job,'An NPC could not be hired. Completed hires remain yours; retry to reuse them.') end
            end
            return
        end
    end
end
return S
end
