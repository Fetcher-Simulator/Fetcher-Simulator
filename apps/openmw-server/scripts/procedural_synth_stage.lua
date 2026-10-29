-- Bounded, session-owned stage jobs. Only the server chooses actor positions.
local Layout=require('procedural_synth_band_layout')
return function(mp,format,performers,emit,keyboardModel)
local S={jobs={},nextAction=0}
local function owns(registry,target)
    for _,e in ipairs(registry.entries) do if e.mpNum==target.mpNum and e.refId==target.refId then return true end end
    return false
end
local function busyActors(sessions,except,character)
    local registry=performers:registry(character)
    local busy={};for id,other in pairs(sessions) do if id~=except and other.characterId==character then
        for _,m in ipairs(other.band) do if m.target and m.target.kind=='npc' and registry and owns(registry,m.target) then busy[m.target.mpNum]=true end end
    end end;return busy
end
function S:prune(s,keep,sessions)
    local registry=performers:registry(s.characterId);if not registry then return 0,0 end
    local busy=busyActors(sessions,s.id,s.characterId);local removed,retained=0,0
    for index=#registry.entries,1,-1 do local entry=registry.entries[index]
        if not keep[entry.mpNum or entry.refId] then
            if busy[entry.mpNum] or not performers:removeEntry(registry,index,s.cell) then retained=retained+1
            else removed=removed+1 end
        end
    end
    mp.setCharacterStorageForCharacter(s.characterId,'ProceduralSynth','performers',registry)
    return removed,retained
end
function S:release(s,sessions)
    local job=self.jobs[s.characterId]
    if job and job.id==s.id then self.jobs[s.characterId]=nil end
    if s.managedBand then self:prune(s,{},sessions) end
end
function S:finish(job,error,sessions)
    self.jobs[job.character]=nil
    local removed,retained=0,0
    if not error and not job.restage then removed,retained=self:prune(job.session,job.used,sessions) end
    if not error and job.clear then job.session.managedBand=false end
    emit(job.owner,{op='bandSetup',id=job.id,epoch=job.epoch,requestId=job.requestId,
        targets=not error and job.targets or nil,routes=not error and job.routes or nil,removed=removed,retained=retained,
        restage=job.restage,clear=job.clear,error=error})
    local s=job.session
    if s and not error then
        local registry=performers:registry(job.character)
        for _,entry in ipairs(registry and registry.entries or {}) do
            if job.used[entry.mpNum] then entry.posture=job.layout=='seated' and 'seated' or nil end
        end
        if registry then mp.setCharacterStorageForCharacter(job.character,'ProceduralSynth','performers',registry) end
        performers:roster(s)
    end
end
function S:begin(s,project,requestId,sessions,restage,clear,ground)
    local function reject(message)
        emit(s.owner,{op='bandSetup',id=s.id,epoch=s.epoch,requestId=requestId,restage=restage,clear=clear,error=message});return false
    end
    if self.jobs[s.characterId] then return reject('Your NPC stage setup is still running.') end
    if not clear then for _,pending in pairs(performers.spawning) do if pending.character==s.characterId then
        return reject('Wait for the current NPC hire to finish before setting up the band.')
    end end end
    local leader=mp.getPlayer(s.owner);local registry=performers:registry(s.characterId)
    if not leader or not registry or not mp.teleportActorByInstanceId then
        return reject('NPC stage setup needs the matching server and an available roster.')
    end
    local anchor={x=leader.position.x,y=leader.position.y,z=leader.position.z,rz=leader.position.rz or 0}
    -- The placed stand has a fixed keybed. Put the participating leader at its
    -- playing side rather than stretching the arms toward a distant keyboard.
    if not restage and not clear and project.leaderPlays~=false and mp.teleportPlayer
        and type(keyboardModel)=='string' and keyboardModel:lower():gsub('\\','/'):find('keyboard%-stand%.nif$') then
        local home=mp.getObjectByMpNum(s.id);local p=home and home.position
        if not p then return reject('The leader keyboard is unavailable.') end
        local yaw=p.rz or 0
        anchor={x=p.x-math.sin(yaw)*50,y=p.y-math.cos(yaw)*50,z=p.z,rx=0,ry=0,rz=yaw}
    end
    local layout=project and project.stageLayout or s.stageLayout or 'standing'
    local plan=(restage or clear) and {} or Layout.plan(project)
    if restage then
        local seen={};for _,m in ipairs(s.band) do local t=m.target
            if t and t.kind=='npc' and owns(registry,t) and not seen[t.mpNum] then
                plan[#plan+1]={target=t};seen[t.mpNum]=true
            end
        end
    end
    if (restage and #plan==0) or #plan>format.maxMembers or not Layout.position(anchor,1,math.max(1,#plan)) then
        return reject(restage and 'Bind owned NPC performers before staging.' or 'Create or import song parts first.')
    end
    if ground then
        local a=type(ground)=='table' and ground.anchor;local h=type(ground)=='table' and ground.heights
        local function finite(x) return type(x)=='number' and x==x and math.abs(x)<1e8 end
        if type(a)~='table' or type(h)~='table' or #h~=#plan then return reject('Invalid stage floor check.') end
        for key in pairs(ground) do if key~='anchor' and key~='heights' then return reject('Invalid stage floor field.') end end
        for key in pairs(a) do if key~='x' and key~='y' and key~='z' and key~='rz' then return reject('Invalid stage anchor field.') end end
        for _,key in ipairs{'x','y','z','rz'} do
            if not finite(a[key]) then return reject('Invalid stage floor anchor.') end
            local difference=key=='rz' and math.abs((a[key]-anchor[key]+math.pi)%(2*math.pi)-math.pi) or math.abs(a[key]-anchor[key])
            if difference>(key=='rz' and .05 or 16) then return reject('Leader moved during the stage check. Retry.') end
        end
        for key in pairs(h) do if type(key)~='number' or key%1~=0 or key<1 or key>#plan then return reject('Invalid stage floor array.') end end
        for index=1,#plan do local z=h[index];if not finite(z) or math.abs(z-anchor.z)>64 then return reject('Stage floor height is out of range.') end end
    end
    local busy=busyActors(sessions,s.id,s.characterId)
    local pool={};for _,e in ipairs(registry.entries) do local actor=e.mpNum and mp.getActorByMpNum(e.mpNum)
        if actor and actor.refId==e.refId and not actor.isDead and not busy[e.mpNum] then pool[#pool+1]=e end
    end
    if restage then for _,part in ipairs(plan) do if busy[part.target.mpNum] then
        return reject('Release the performer from the other active band before restaging.')
    end end end
    if not restage and #plan>#pool+format.maxMembers-#registry.entries then
        return reject('Not enough available owned performers. Dismiss missing/dead NPCs or release their other band first.')
    end
    if not clear then s.stageLayout=layout end
    if not restage and not clear then s.managedBand=true end
    if not restage and not clear and project.leaderPlays~=false and mp.teleportPlayer
        and type(keyboardModel)=='string' and keyboardModel:lower():gsub('\\','/'):find('keyboard%-stand%.nif$')
        and not mp.teleportPlayer(s.owner,s.cell,anchor) then return reject('Could not align the leader with the keyboard.') end
    self.jobs[s.characterId]={id=s.id,epoch=s.epoch,owner=s.owner,character=s.characterId,requestId=requestId,session=s,
        anchor=anchor,ground=ground and ground.heights,cell=s.cell,layout=layout,plan=plan,routes=plan.routes,pool=pool,targets={},used={},cursor=1,restage=restage,clear=clear,expires=mp.getUptime()+45}
    return true
end
function S:tick(sessions)
    local now=mp.getUptime();if now<self.nextAction then return end
    for _,job in pairs(self.jobs) do
        local s=sessions[job.id]
        if not s or s.epoch~=job.epoch or not mp.getPlayer(job.owner) then self.jobs[job.character]=nil
        elseif now>job.expires then self:finish(job,'NPC setup timed out. Retry to reuse completed hires.',sessions)
        elseif job.moving then
            -- Native teleports are queued. A successful enqueue is not proof
            -- that the actor actually moved (or was eligible to teleport).
            local pending=job.moving;local actor=mp.getActorByMpNum(pending.target.mpNum)
            local p=actor and actor.position;local q=pending.position
            if actor and actor.refId==pending.target.refId and actor.cell==job.cell and p
                and (p.x-q.x)^2+(p.y-q.y)^2<=12^2 and math.abs(p.z-q.z)<=16 then
                job.moving=nil;job.cursor=pending.index+1
            elseif now>=pending.deadline then
                if pending.attempts<2 and actor and actor.refId==pending.target.refId and not actor.isDead
                    and actor.actorInstanceId and mp.teleportActorByInstanceId(actor.actorInstanceId,job.cell,q,job.owner) then
                    pending.attempts=pending.attempts+1;pending.deadline=now+2
                else self:finish(job,'An NPC did not reach its stage slot. Refresh the roster and retry; playback was not started.',sessions) end
            end
            return
        elseif not job.waiting then
            if job.cursor>#job.plan then self:finish(job,nil,sessions);return end
            self.nextAction=now+.1
            local index=job.cursor;local part=job.plan[index];local entry
            if part.target then entry=part.target else
                -- Prefer this song's current NPC, then reuse the remaining pool.
                local old=s.band[index] and s.band[index].target
                for _,e in ipairs(job.pool) do if not job.used[e.mpNum] and old and e.mpNum==old.mpNum and e.refId==old.refId then entry=e;break end end
                if not entry then for _,e in ipairs(job.pool) do if not job.used[e.mpNum] then entry=e;break end end end
            end
            local position=Layout.position(job.anchor,index,#job.plan)
            position.z=job.ground and job.ground[index] or position.z
            if entry then
                position.isTeleporting=true
                local actor=mp.getActorByMpNum(entry.mpNum)
                if not actor or actor.refId~=entry.refId or actor.isDead or not actor.actorInstanceId
                    or busyActors(sessions,job.id,job.character)[entry.mpNum] or not mp.teleportActorByInstanceId(actor.actorInstanceId,job.cell,position,job.owner) then
                    self:finish(job,'An owned performer could not be staged. Refresh the roster and retry.',sessions);return
                end
                job.used[entry.mpNum]=true
                job.targets[index]={kind='npc',mpNum=entry.mpNum,refId=entry.refId,instrument=part.instrument or entry.instrument}
                job.moving={index=index,target=job.targets[index],position=position,attempts=1,deadline=now+2}
            else
                job.waiting=true
                local ok=performers:hire(s,index,{instrument=part.instrument,position=position,complete=function(target)
                    if self.jobs[job.character]~=job then return false end
                    job.targets[index]=target;job.used[target.mpNum]=true;job.waiting=nil
                    job.moving={index=index,target=target,position=position,attempts=1,deadline=mp.getUptime()+2}
                    return true
                end})
                if not ok then self:finish(job,'An NPC could not be hired. Retry to reuse completed hires.',sessions) end
            end
            return
        end
    end
end
return S
end
